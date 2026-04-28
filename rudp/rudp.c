/*
 * rudp.c — Core RUDP Protocol Implementation
 * ============================================
 * Group 3: Dhruv, Krishiv, Piyush, Vivek, Kunal
 *
 * Architecture:
 *   Each rudp_socket spawns two background pthreads:
 *   1. _recv_thread  — continuously recvfrom() the raw UDP socket,
 *                      processes ACKs and places data into recv window
 *   2. _retransmit_thread — scans send window for timed-out packets
 *                      and re-sends them
 *
 * Thread safety: all window access is through mutex-protected functions
 * in rudp_buffer.c. The raw socket fd is read by _recv_thread and
 * written by send paths (protected by send_lock mutex).
 */

#include "rudp.h"
#include "rudp_buffer.h"
#include "rudp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>

/* ─── Default Configuration ────────────────────────────────────── */
const rudp_config_t RUDP_DEFAULT_CONFIG = {
    .timeout_ms  = RUDP_TIMEOUT_MS,
    .max_retries = RUDP_MAX_RETRIES,
    .window_size = RUDP_WINDOW_SIZE,
};

/* ─── Internal Socket Structure ────────────────────────────────── */
struct rudp_socket {
    int                 fd;          /* raw UDP socket fd              */
    struct sockaddr_in  local;       /* local bound address            */
    struct sockaddr_in  peer;        /* remote peer (after connect)    */
    bool                connected;   /* handshake complete?            */
    bool                nonblocking;

    rudp_config_t       cfg;

    /* Sequence number state */
    atomic_uint         send_seq;    /* next seq to assign (send side) */
    atomic_uint         recv_ack;    /* last ACK we sent to peer       */

    /* Windows */
    rudp_send_window_t  send_win;
    rudp_recv_window_t  recv_win;

    /* RTT estimator */
    rudp_rtt_t          rtt;

    /* Background threads */
    pthread_t           recv_tid;
    pthread_t           retransmit_tid;
    atomic_bool         running;

    /* Raw send lock (multiple writers possible) */
    pthread_mutex_t     send_lock;

    /* Statistics */
    rudp_stats_t        stats;
    pthread_mutex_t     stats_lock;
};

/* ─── Utility: Build & Send a Raw Packet ──────────────────────── */
static ssize_t _raw_send(rudp_socket_t *sock,
                         const rudp_packet_t *pkt, size_t pkt_len,
                         const struct sockaddr_in *dest)
{
    pthread_mutex_lock(&sock->send_lock);
    ssize_t n = sendto(sock->fd, pkt, pkt_len, 0,
                       (struct sockaddr*)dest, sizeof(*dest));
    pthread_mutex_unlock(&sock->send_lock);
    return n;
}

/* ─── Build a Packet ───────────────────────────────────────────── */
static size_t _build_packet(rudp_packet_t *pkt,
                             uint8_t flags,
                             uint32_t seq, uint32_t ack,
                             const void *payload, size_t payload_len)
{
    memset(pkt, 0, RUDP_HEADER_SIZE);
    pkt->hdr.version_flags_hi = (uint8_t)((RUDP_VERSION << 4) | ((flags >> 4) & 0x0F));
    pkt->hdr.flags_lo          = (uint8_t)(flags & 0x0F);
    pkt->hdr.payload_len       = htons((uint16_t)payload_len);
    pkt->hdr.seq               = htonl(seq);
    pkt->hdr.ack               = htonl(ack);
    pkt->hdr.reserved          = 0;

    if (payload && payload_len > 0) {
        if (payload_len > RUDP_MAX_PAYLOAD) payload_len = RUDP_MAX_PAYLOAD;
        memcpy(pkt->payload, payload, payload_len);
    }

    /* Compute checksum over header+payload, then store it */
    pkt->hdr.checksum = 0;
    pkt->hdr.checksum = rudp_checksum(pkt, RUDP_HEADER_SIZE + payload_len);

    return RUDP_HEADER_SIZE + payload_len;
}

/* ─── Send an ACK for seq ──────────────────────────────────────── */
static void _send_ack(rudp_socket_t *sock, uint32_t ack_seq,
                      const struct sockaddr_in *dest)
{
    rudp_packet_t ack_pkt;
    size_t len = _build_packet(&ack_pkt, RUDP_FLAG_ACK,
                               0, ack_seq, NULL, 0);
    _raw_send(sock, &ack_pkt, len, dest);

    pthread_mutex_lock(&sock->stats_lock);
    sock->stats.acks_sent++;
    pthread_mutex_unlock(&sock->stats_lock);
}

/* ─── Receive Thread ───────────────────────────────────────────── */
static void *_recv_thread(void *arg) {
    rudp_socket_t *sock = (rudp_socket_t*)arg;
    uint8_t buf[RUDP_MAX_PACKET];

    while (atomic_load(&sock->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock->fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; /* 50ms */
        int rv = select(sock->fd + 1, &rfds, NULL, NULL, &tv);
        if (rv <= 0) continue;

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sock->fd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&from, &flen);
        if (n < RUDP_HEADER_SIZE) continue;

        rudp_packet_t *pkt = (rudp_packet_t*)buf;
        rudp_hdr_t    *hdr = &pkt->hdr;

        /* ── Verify checksum ── */
        uint16_t rcv_cksum = hdr->checksum;
        hdr->checksum = 0;
        uint16_t calc_cksum = rudp_checksum(pkt, (size_t)n);
        hdr->checksum = rcv_cksum;
        if (rcv_cksum != calc_cksum) {
            fprintf(stderr, "[RUDP] Checksum mismatch seq=%u — dropping\n",
                    ntohl(hdr->seq));
            continue;
        }

        uint8_t flags = (uint8_t)(((hdr->version_flags_hi & 0x0F) << 4)
                                   | (hdr->flags_lo & 0x0F));

        /* ── ACK packet: mark send window ── */
        if (flags & RUDP_FLAG_ACK) {
            uint32_t ack_seq = ntohl(hdr->ack);

            /* RTT measurement for the ACKed packet */
            pthread_mutex_lock(&sock->send_win.lock);
            for (uint32_t i = 0; i < sock->send_win.window_size; i++) {
                rudp_send_entry_t *e = &sock->send_win.entries[i];
                if (e->in_use && ntohl(e->pkt.hdr.seq) == ack_seq) {
                    double rtt_m = elapsed_ms(&e->sent_at);
                    rtt_update(&sock->rtt, rtt_m);
                    pthread_mutex_lock(&sock->stats_lock);
                    sock->stats.rtt_ms = sock->rtt.srtt;
                    sock->stats.acks_recv++;
                    pthread_mutex_unlock(&sock->stats_lock);
                    break;
                }
            }
            pthread_mutex_unlock(&sock->send_win.lock);

            send_window_ack(&sock->send_win, ack_seq);
        }

        /* ── PING: reply with PONG ── */
        if (flags & RUDP_FLAG_PING) {
            rudp_packet_t pong;
            size_t plen = _build_packet(&pong, RUDP_FLAG_PONG,
                                        0, 0, NULL, 0);
            _raw_send(sock, &pong, plen, &from);
        }

        /* ── FIN: remote closed ── */
        if (flags & RUDP_FLAG_FIN) {
            /* Send FIN|ACK back */
            rudp_packet_t fin_ack;
            size_t flen2 = _build_packet(&fin_ack, RUDP_FLAG_FIN | RUDP_FLAG_ACK,
                                         0, ntohl(hdr->seq), NULL, 0);
            _raw_send(sock, &fin_ack, flen2, &from);
            atomic_store(&sock->running, false);
            break;
        }

        /* ── SYN: connection request ── */
        if ((flags & RUDP_FLAG_SYN) && !(flags & RUDP_FLAG_ACK)) {
            /* Server-side: store peer, set initial recv seq, reply SYN|ACK */
            sock->peer = from;
            atomic_store(&sock->recv_ack, ntohl(hdr->seq));
            uint32_t my_seq = atomic_fetch_add(&sock->send_seq, 1);
            rudp_packet_t syn_ack;
            size_t slen = _build_packet(&syn_ack, RUDP_FLAG_SYN | RUDP_FLAG_ACK,
                                        my_seq, ntohl(hdr->seq), NULL, 0);
            _raw_send(sock, &syn_ack, slen, &from);
            sock->connected = true;
            printf("[RUDP] Connection accepted from %s:%u\n",
                   inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        }

        /* ── SYN|ACK: complete client handshake ── */
        if ((flags & RUDP_FLAG_SYN) && (flags & RUDP_FLAG_ACK)) {
            send_window_ack(&sock->send_win, ntohl(hdr->ack));
            sock->connected = true;
        }

        /* ── DATA: place in recv window, send ACK ── */
        if (flags & RUDP_FLAG_DATA) {
            uint32_t seq     = ntohl(hdr->seq);
            size_t   pay_len = ntohs(hdr->payload_len);

            int r = recv_window_insert(&sock->recv_win, pkt, pay_len, &from);
            
            uint32_t cum_ack = recv_window_get_cum_ack(&sock->recv_win);
            atomic_store(&sock->recv_ack, cum_ack);
            _send_ack(sock, cum_ack, &from);

            if (r == 0) {
                pthread_mutex_lock(&sock->stats_lock);
                sock->stats.packets_recv++;
                sock->stats.bytes_recv += pay_len;
                pthread_mutex_unlock(&sock->stats_lock);
            }
        }
    }

    return NULL;
}

/* ─── Retransmit Thread ────────────────────────────────────────── */
static void *_retransmit_thread(void *arg) {
    rudp_socket_t *sock = (rudp_socket_t*)arg;

    while (atomic_load(&sock->running)) {
        sleep_ms(1); /* check every 1ms */

        uint32_t rto = (uint32_t)rtt_get_rto(&sock->rtt);

        pthread_mutex_lock(&sock->send_win.lock);
        for (uint32_t i = 0; i < sock->send_win.window_size; i++) {
            rudp_send_entry_t *e = &sock->send_win.entries[i];
            if (!e->in_use || e->acked) continue;

            double age = elapsed_ms(&e->sent_at);
            if (age < (double)rto) continue;

            /* Check retry limit */
            if (e->retries >= sock->cfg.max_retries) {
                fprintf(stderr, "[RUDP] Max retries exceeded for seq=%u\n",
                        ntohl(e->pkt.hdr.seq));
                e->in_use = false;
                pthread_cond_broadcast(&sock->send_win.space_avail);
                continue;
            }

            /* Retransmit */
            e->retries++;
            clock_now(&e->sent_at);
            pthread_mutex_unlock(&sock->send_win.lock);

            _raw_send(sock, &e->pkt, e->pkt_len, &e->dest);

            pthread_mutex_lock(&sock->stats_lock);
            sock->stats.packets_retransmitted++;
            pthread_mutex_unlock(&sock->stats_lock);

            pthread_mutex_lock(&sock->send_win.lock);
        }
        pthread_mutex_unlock(&sock->send_win.lock);
    }

    return NULL;
}

/* ──────────────────────────────────────────────────────────────── */
/*  PUBLIC API                                                        */
/* ──────────────────────────────────────────────────────────────── */

rudp_socket_t *rudp_socket(const rudp_config_t *cfg) {
    rudp_socket_t *sock = calloc(1, sizeof(rudp_socket_t));
    if (!sock) return NULL;

    sock->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock->fd < 0) { perror("rudp_socket"); free(sock); return NULL; }

    int opt = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sock->cfg = cfg ? *cfg : RUDP_DEFAULT_CONFIG;

    send_window_init(&sock->send_win, sock->cfg.window_size);
    recv_window_init(&sock->recv_win, sock->cfg.window_size);

    pthread_mutex_init(&sock->send_lock, NULL);
    pthread_mutex_init(&sock->stats_lock, NULL);

    rtt_init(&sock->rtt, (double)sock->cfg.timeout_ms);

    atomic_init(&sock->send_seq, 1);
    atomic_init(&sock->recv_ack, 0);
    atomic_init(&sock->running, true);

    /* Start background threads */
    pthread_create(&sock->recv_tid,       NULL, _recv_thread,       sock);
    pthread_create(&sock->retransmit_tid, NULL, _retransmit_thread, sock);

    return sock;
}

int rudp_bind(rudp_socket_t *sock, uint16_t port) {
    memset(&sock->local, 0, sizeof(sock->local));
    sock->local.sin_family      = AF_INET;
    sock->local.sin_addr.s_addr = INADDR_ANY;
    sock->local.sin_port        = htons(port);

    if (bind(sock->fd, (struct sockaddr*)&sock->local,
             sizeof(sock->local)) < 0) {
        perror("rudp_bind");
        return -1;
    }
    printf("[RUDP] Bound to port %u\n", port);
    return 0;
}

int rudp_connect(rudp_socket_t *sock, const char *addr, uint16_t port) {
    memset(&sock->peer, 0, sizeof(sock->peer));
    sock->peer.sin_family = AF_INET;
    sock->peer.sin_port   = htons(port);
    if (inet_pton(AF_INET, addr, &sock->peer.sin_addr) <= 0) {
        fprintf(stderr, "[RUDP] Invalid address: %s\n", addr);
        return -1;
    }

    /* Send SYN */
    uint32_t syn_seq = atomic_fetch_add(&sock->send_seq, 1);
    rudp_packet_t syn_pkt;
    size_t syn_len = _build_packet(&syn_pkt, RUDP_FLAG_SYN,
                                   syn_seq, 0, NULL, 0);

    /* Add to send window so retransmit thread handles it */
    send_window_add(&sock->send_win, &syn_pkt, syn_len, &sock->peer);
    _raw_send(sock, &syn_pkt, syn_len, &sock->peer);

    printf("[RUDP] SYN sent to %s:%u (seq=%u)...\n", addr, port, syn_seq);

    /* Wait for SYN|ACK (recv thread sets connected = true) */
    struct timespec start;
    clock_now(&start);
    while (!sock->connected) {
        if (elapsed_ms(&start) > 5000.0) {
            fprintf(stderr, "[RUDP] Connect timeout\n");
            return -1;
        }
        sleep_ms(10);
    }

    printf("[RUDP] Connected to %s:%u\n", addr, port);
    return 0;
}

int rudp_accept(rudp_socket_t *sock, struct sockaddr_in *peer) {
    printf("[RUDP] Waiting for connection...\n");
    struct timespec start;
    clock_now(&start);

    while (!sock->connected) {
        if (elapsed_ms(&start) > 30000.0) {
            fprintf(stderr, "[RUDP] Accept timeout\n");
            return -1;
        }
        sleep_ms(10);
    }

    if (peer) *peer = sock->peer;
    return 0;
}

ssize_t rudp_send(rudp_socket_t *sock, const void *buf, size_t len) {
    if (!sock->connected) {
        fprintf(stderr, "[RUDP] Not connected\n");
        return -1;
    }
    return rudp_sendto(sock, buf, len, &sock->peer);
}

ssize_t rudp_sendto(rudp_socket_t *sock, const void *buf, size_t len,
                    const struct sockaddr_in *dest)
{
    if (len > RUDP_MAX_PAYLOAD) len = RUDP_MAX_PAYLOAD;

    uint32_t seq = atomic_fetch_add(&sock->send_seq, 1);
    rudp_packet_t pkt;
    size_t pkt_len = _build_packet(&pkt,
                                   RUDP_FLAG_DATA | RUDP_FLAG_ACK,
                                   seq,
                                   atomic_load(&sock->recv_ack),
                                   buf, len);

    /* Add to send window (blocks if full) */
    int slot = send_window_add(&sock->send_win, &pkt, pkt_len, dest);
    if (slot < 0) return -1;

    /* Initial transmit */
    ssize_t n = _raw_send(sock, &pkt, pkt_len, dest);
    if (n < 0) return -1;

    pthread_mutex_lock(&sock->stats_lock);
    sock->stats.packets_sent++;
    sock->stats.bytes_sent += len;
    pthread_mutex_unlock(&sock->stats_lock);

    /* The background _retransmit_thread handles retries, and the _recv_thread
       processes ACKs to free up window slots asynchronously.
       Flow control is handled by send_window_add() which blocks if the window is full. */

    return (ssize_t)len;
}

ssize_t rudp_recv(rudp_socket_t *sock, void *buf, size_t maxlen) {
    return rudp_recvfrom(sock, buf, maxlen, NULL);
}

ssize_t rudp_recvfrom(rudp_socket_t *sock, void *buf, size_t maxlen,
                      struct sockaddr_in *from)
{
    rudp_packet_t pkt;
    size_t payload_len;
    struct sockaddr_in sender;

    if (recv_window_get(&sock->recv_win, &pkt, &payload_len, &sender) < 0)
        return -1;

    size_t copy_len = (payload_len < maxlen) ? payload_len : maxlen;
    memcpy(buf, pkt.payload, copy_len);
    if (from) *from = sender;

    return (ssize_t)copy_len;
}

int rudp_close(rudp_socket_t *sock) {
    if (!sock) return 0;

    if (sock->connected) {
        /* Send FIN */
        rudp_packet_t fin_pkt;
        size_t flen = _build_packet(&fin_pkt, RUDP_FLAG_FIN,
                                    atomic_fetch_add(&sock->send_seq, 1),
                                    0, NULL, 0);
        _raw_send(sock, &fin_pkt, flen, &sock->peer);
        sleep_ms(100); /* wait briefly for FIN|ACK */
    }

    atomic_store(&sock->running, false);
    pthread_join(sock->recv_tid,       NULL);
    pthread_join(sock->retransmit_tid, NULL);

    send_window_destroy(&sock->send_win);
    recv_window_destroy(&sock->recv_win);
    pthread_mutex_destroy(&sock->send_lock);
    pthread_mutex_destroy(&sock->stats_lock);

    close(sock->fd);
    free(sock);
    return 0;
}

int rudp_set_nonblocking(rudp_socket_t *sock, bool nonblock) {
    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    sock->nonblocking = nonblock;
    return fcntl(sock->fd, F_SETFL, flags);
}

void rudp_get_stats(const rudp_socket_t *sock, rudp_stats_t *out) {
    pthread_mutex_lock((pthread_mutex_t*)&sock->stats_lock);
    *out = sock->stats;
    pthread_mutex_unlock((pthread_mutex_t*)&sock->stats_lock);
}

void rudp_print_stats(const rudp_socket_t *sock) {
    rudp_stats_t s;
    rudp_get_stats(sock, &s);
    printf("\n╔══════════════ RUDP Statistics ══════════════╗\n");
    printf("║ Packets Sent:          %20lu ║\n", s.packets_sent);
    printf("║ Packets Received:      %20lu ║\n", s.packets_recv);
    printf("║ Retransmissions:       %20lu ║\n", s.packets_retransmitted);
    printf("║ ACKs Sent:             %20lu ║\n", s.acks_sent);
    printf("║ ACKs Received:         %20lu ║\n", s.acks_recv);
    printf("║ Bytes Sent:            %20lu ║\n", s.bytes_sent);
    printf("║ Bytes Received:        %20lu ║\n", s.bytes_recv);
    printf("║ Smoothed RTT (ms):     %20.2f ║\n", s.rtt_ms);
    printf("╚═════════════════════════════════════════════╝\n");
}

/* ─── Checksum (Internet Checksum RFC 1071) ────────────────────── */
uint16_t rudp_checksum(const void *data, size_t len) {
    const uint16_t *buf = (const uint16_t*)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1)
        sum += *(uint8_t*)buf;

    sum  = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

/* ─── Debug Printer ────────────────────────────────────────────── */
void rudp_print_packet(const rudp_packet_t *pkt, const char *label) {
    const rudp_hdr_t *h = &pkt->hdr;
    uint8_t flags = (uint8_t)(((h->version_flags_hi & 0x0F) << 4)
                               | (h->flags_lo & 0x0F));
    printf("[%s] RUDP v%u flags=0x%02X [%s%s%s%s%s%s%s%s] "
           "seq=%u ack=%u len=%u cksum=0x%04X\n",
           label ? label : "PKT",
           RUDP_GET_VERSION(h),
           flags,
           (flags & RUDP_FLAG_SYN)  ? "SYN "  : "",
           (flags & RUDP_FLAG_ACK)  ? "ACK "  : "",
           (flags & RUDP_FLAG_FIN)  ? "FIN "  : "",
           (flags & RUDP_FLAG_RST)  ? "RST "  : "",
           (flags & RUDP_FLAG_PING) ? "PING " : "",
           (flags & RUDP_FLAG_PONG) ? "PONG " : "",
           (flags & RUDP_FLAG_NAT)  ? "NAT "  : "",
           (flags & RUDP_FLAG_DATA) ? "DATA"  : "",
           ntohl(h->seq), ntohl(h->ack),
           ntohs(h->payload_len),
           ntohs(h->checksum));
}
