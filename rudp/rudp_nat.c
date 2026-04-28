/*
 * rudp_nat.c — UDP Hole Punching / NAT Traversal Implementation
 * ==============================================================
 *
 * Protocol between peers and rendezvous server (all over UDP):
 *
 *   REGISTER <peer_id> <room>         → server replies: WAIT / PEER <ip> <port>
 *   NAT probe packets (RUDP_FLAG_NAT) exchanged directly between peers
 */

#include "rudp_nat.h"
#include "rudp.h"
#include "rudp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

/* ─── Internal: raw UDP helper for rendezvous exchange ─────────── */
static int _udp_sendrecv(int fd,
                         const struct sockaddr_in *srv,
                         const char *msg, size_t msg_len,
                         char *reply, size_t reply_max,
                         uint32_t timeout_ms)
{
    /* Send */
    if (sendto(fd, msg, msg_len, 0,
               (struct sockaddr*)srv, sizeof(*srv)) < 0) {
        perror("[NAT] sendto rendezvous");
        return -1;
    }

    /* Wait for reply with timeout */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = { .tv_sec  = timeout_ms / 1000,
                          .tv_usec = (timeout_ms % 1000) * 1000 };
    int rv = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (rv <= 0) {
        if (rv == 0) fprintf(stderr, "[NAT] rendezvous timeout\n");
        else perror("[NAT] select");
        return -1;
    }

    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    ssize_t n = recvfrom(fd, reply, (ssize_t)reply_max - 1, 0,
                         (struct sockaddr*)&from, &flen);
    if (n < 0) { perror("[NAT] recvfrom"); return -1; }
    reply[n] = '\0';
    return (int)n;
}

/* ─── rudp_nat_rendezvous ──────────────────────────────────────── */
int rudp_nat_rendezvous(rudp_socket_t *sock,
                        const char    *server_ip,
                        uint16_t       server_port,
                        const rudp_peer_info_t *my_info,
                        struct sockaddr_in     *peer_out)
{
    (void)sock; /* rendezvous uses its own raw fd */
    /* We need the underlying fd — use a fresh raw UDP socket for
     * the rendezvous exchange so we don't disturb the RUDP state. */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("[NAT] socket"); return -1; }

    /* Bind to same local port so the server sees our real public endpoint */
    struct sockaddr_in local = { .sin_family = AF_INET,
                                 .sin_addr.s_addr = INADDR_ANY,
                                 .sin_port = 0 };
    bind(fd, (struct sockaddr*)&local, sizeof(local));

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &srv.sin_addr) <= 0) {
        fprintf(stderr, "[NAT] invalid server IP: %s\n", server_ip);
        close(fd); return -1;
    }

    /* Build REGISTER message */
    char msg[256];
    snprintf(msg, sizeof(msg), "REGISTER %s %s",
             my_info->peer_id, my_info->room);

    char reply[256];
    /* Retry up to 5 times waiting for the other peer to register */
    for (int attempt = 0; attempt < 10; attempt++) {
        printf("[NAT] Registering with rendezvous server %s:%u (attempt %d)...\n",
               server_ip, server_port, attempt + 1);

        if (_udp_sendrecv(fd, &srv, msg, strlen(msg),
                          reply, sizeof(reply), 3000) < 0) {
            close(fd); return -1;
        }

        /* Parse reply */
        if (strncmp(reply, "PEER ", 5) == 0) {
            /* PEER <ip> <port> */
            char peer_ip[INET_ADDRSTRLEN];
            uint16_t peer_port;
            if (sscanf(reply + 5, "%s %hu", peer_ip, &peer_port) == 2) {
                memset(peer_out, 0, sizeof(*peer_out));
                peer_out->sin_family = AF_INET;
                peer_out->sin_port   = htons(peer_port);
                inet_pton(AF_INET, peer_ip, &peer_out->sin_addr);
                printf("[NAT] Peer found at %s:%u\n", peer_ip, peer_port);
                close(fd);
                return 0;
            }
        } else if (strncmp(reply, "WAIT", 4) == 0) {
            printf("[NAT] Waiting for second peer to join room '%s'...\n",
                   my_info->room);
            sleep_ms(2000);
        } else {
            fprintf(stderr, "[NAT] Unknown reply: %s\n", reply);
        }
    }

    close(fd);
    fprintf(stderr, "[NAT] Rendezvous failed after 10 attempts\n");
    return -1;
}

/* ─── rudp_nat_punch ───────────────────────────────────────────── */
int rudp_nat_punch(rudp_socket_t *sock,
                   const struct sockaddr_in *peer,
                   uint32_t timeout_ms)
{
    (void)sock; /* hole punching uses its own raw fd */
    /* Create a raw UDP socket for hole punching probes */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    /* Build a minimal RUDP_FLAG_NAT packet */
    rudp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr.version_flags_hi = (uint8_t)((RUDP_VERSION << 4) | ((RUDP_FLAG_NAT >> 4) & 0x0F));
    pkt.hdr.flags_lo          = (uint8_t)(RUDP_FLAG_NAT & 0x0F);
    pkt.hdr.payload_len       = 0;
    pkt.hdr.seq               = 0;
    pkt.hdr.ack               = 0;

    char peer_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer->sin_addr, peer_str, sizeof(peer_str));
    printf("[NAT] Punching hole to %s:%u...\n", peer_str, ntohs(peer->sin_port));

    struct timespec start;
    clock_now(&start);

    bool punched = false;
    while (elapsed_ms(&start) < (double)timeout_ms) {
        /* Send probe */
        sendto(fd, &pkt, RUDP_HEADER_SIZE, 0,
               (struct sockaddr*)peer, sizeof(*peer));

        /* Check for incoming reply */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100ms */
        int rv = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rv > 0) {
            char buf[RUDP_MAX_PACKET];
            struct sockaddr_in from;
            socklen_t flen = sizeof(from);
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                                 (struct sockaddr*)&from, &flen);
            if (n >= RUDP_HEADER_SIZE) {
                rudp_hdr_t *h = (rudp_hdr_t*)buf;
                if (rudp_flag(h, RUDP_FLAG_NAT)) {
                    printf("[NAT] Hole punch successful! Bidirectional path open.\n");
                    punched = true;
                    break;
                }
            }
        }
    }

    close(fd);
    return punched ? 0 : -1;
}

/* ─── rudp_nat_punch_direct ────────────────────────────────────── */
int rudp_nat_punch_direct(rudp_socket_t *sock,
                          const char    *peer_ip,
                          uint16_t       peer_port,
                          uint32_t       timeout_ms)
{
    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_port   = htons(peer_port);
    if (inet_pton(AF_INET, peer_ip, &peer.sin_addr) <= 0) {
        fprintf(stderr, "[NAT] invalid peer IP: %s\n", peer_ip);
        return -1;
    }
    return rudp_nat_punch(sock, &peer, timeout_ms);
}

/* ──────────────────────────────────────────────────────────────── */
/*  RENDEZVOUS SERVER                                                */
/* ──────────────────────────────────────────────────────────────── */

typedef struct {
    char               peer_id[32];
    char               room[64];
    struct sockaddr_in addr;
    int                active;
} _rend_peer_t;

#define MAX_REND_PEERS 64

int rudp_rendezvous_server_run(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("rendezvous socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons(port);
    if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("rendezvous bind"); close(fd); return -1;
    }

    printf("[Rendezvous] Server listening on UDP port %u\n", port);

    _rend_peer_t peers[MAX_REND_PEERS];
    memset(peers, 0, sizeof(peers));

    char buf[512];
    while (1) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&from, &flen);
        if (n <= 0) continue;
        buf[n] = '\0';

        char peer_id[32], room[64];
        if (sscanf(buf, "REGISTER %31s %63s", peer_id, room) != 2)
            continue;

        char from_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));
        uint16_t from_port = ntohs(from.sin_port);
        printf("[Rendezvous] REGISTER peer=%s room=%s from=%s:%u\n",
               peer_id, room, from_ip, from_port);

        /* Find a matching peer in same room */
        int match = -1;
        int free_slot = -1;
        for (int i = 0; i < MAX_REND_PEERS; i++) {
            if (!peers[i].active) {
                if (free_slot == -1) free_slot = i;
                continue;
            }
            if (strcmp(peers[i].room, room) == 0 &&
                strcmp(peers[i].peer_id, peer_id) != 0) {
                match = i;
                break;
            }
        }

        if (match >= 0) {
            /* Reply to both with each other's endpoints */
            char reply[256];

            /* Tell the new peer about the existing peer */
            char match_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peers[match].addr.sin_addr,
                      match_ip, sizeof(match_ip));
            snprintf(reply, sizeof(reply), "PEER %s %u",
                     match_ip, ntohs(peers[match].addr.sin_port));
            sendto(fd, reply, strlen(reply), 0,
                   (struct sockaddr*)&from, flen);

            /* Tell the existing peer about the new peer */
            snprintf(reply, sizeof(reply), "PEER %s %u",
                     from_ip, from_port);
            sendto(fd, reply, strlen(reply), 0,
                   (struct sockaddr*)&peers[match].addr,
                   sizeof(peers[match].addr));

            /* Remove matched peer from table */
            peers[match].active = 0;
            printf("[Rendezvous] Matched %s ↔ %s in room %s\n",
                   peer_id, peers[match].peer_id, room);
        } else {
            /* Store and tell peer to wait */
            if (free_slot >= 0) {
                snprintf(peers[free_slot].peer_id, sizeof(peers[free_slot].peer_id), "%s", peer_id);
                snprintf(peers[free_slot].room,    sizeof(peers[free_slot].room),    "%s", room);
                peers[free_slot].addr   = from;
                peers[free_slot].active = 1;
            }
            const char *wait = "WAIT";
            sendto(fd, wait, strlen(wait), 0,
                   (struct sockaddr*)&from, flen);
            printf("[Rendezvous] Stored %s, waiting for peer in room %s\n",
                   peer_id, room);
        }
    }

    close(fd);
    return 0;
}
