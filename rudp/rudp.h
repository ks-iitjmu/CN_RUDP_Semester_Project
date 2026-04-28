/*
 * rudp.h — Custom Reliable UDP (RUDP) Protocol
 * =============================================
 * Group 3: Dhruv, Krishiv, Piyush, Vivek, Kunal
 * Network Computing Semester Project
 *
 * RUDP sits on top of raw UDP sockets and adds:
 *   - Sequence numbers & cumulative ACKs
 *   - Retransmission with configurable timeout + retry limit
 *   - Sliding-window flow control (stop-and-wait per packet for simplicity)
 *   - UDP Hole Punching for NAT traversal
 *
 * Packet wire format (big-endian, 12 bytes fixed header):
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Version (4)  |   Flags (8)   |         Payload Length        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Sequence Number                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   Acknowledgment Number                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            Checksum (16)      |         Reserved (16)         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Payload ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Total header = 16 bytes. Max payload = RUDP_MAX_PAYLOAD bytes.
 */

#ifndef RUDP_H
#define RUDP_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>

/* ─── Version ─────────────────────────────────────────────────── */
#define RUDP_VERSION      1

/* ─── Flags (8-bit field) ──────────────────────────────────────── */
#define RUDP_FLAG_SYN     0x01   /* Connection initiation         */
#define RUDP_FLAG_ACK     0x02   /* Acknowledgment                */
#define RUDP_FLAG_FIN     0x04   /* Connection teardown           */
#define RUDP_FLAG_RST     0x08   /* Connection reset              */
#define RUDP_FLAG_PING    0x10   /* Keep-alive / RTT probe        */
#define RUDP_FLAG_PONG    0x20   /* Keep-alive reply              */
#define RUDP_FLAG_NAT     0x40   /* NAT hole-punch packet         */
#define RUDP_FLAG_DATA    0x80   /* Data payload present          */

/* ─── Limits ───────────────────────────────────────────────────── */
#define RUDP_MAX_PAYLOAD  1400   /* bytes (fits in typical MTU)   */
#define RUDP_HEADER_SIZE  16     /* bytes                         */
#define RUDP_MAX_PACKET   (RUDP_HEADER_SIZE + RUDP_MAX_PAYLOAD)
#define RUDP_WINDOW_SIZE  16     /* max unACKed packets in flight */
#define RUDP_MAX_RETRIES  10     /* retransmit attempts before ERR*/
#define RUDP_TIMEOUT_MS   200    /* initial retransmit timeout ms */
#define RUDP_MAX_PEERS    8      /* max peers tracked             */

/* ─── Packet Header ────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  version_flags_hi;  /* bits 7-4: version, bits 3-0: flags hi */
    uint8_t  flags_lo;          /* lower 4 bits of flags (flags 4-7)      */
    uint16_t payload_len;       /* payload length in bytes (network order) */
    uint32_t seq;               /* sequence number (network order)         */
    uint32_t ack;               /* acknowledgment number (network order)   */
    uint16_t checksum;          /* internet checksum of entire packet      */
    uint16_t reserved;          /* padding — set to 0                      */
} rudp_hdr_t;

typedef struct {
    rudp_hdr_t hdr;
    uint8_t    payload[RUDP_MAX_PAYLOAD];
} rudp_packet_t;
#pragma pack(pop)

/* ─── Convenience Macros on Header ────────────────────────────── */
#define RUDP_GET_VERSION(h) (((h)->version_flags_hi >> 4) & 0x0F)
#define RUDP_GET_FLAGS(h)   ((uint16_t)(((h)->version_flags_hi & 0x0F) << 4) \
                              | ((h)->flags_lo & 0x0F))
#define RUDP_SET_VERSION_FLAGS(h, v, f) \
    do { \
        (h)->version_flags_hi = (uint8_t)((((v) & 0x0F) << 4) | (((f) >> 4) & 0x0F)); \
        (h)->flags_lo         = (uint8_t)((f) & 0x0F); \
    } while (0)

/* Simpler per-flag test */
static inline uint8_t rudp_flag(const rudp_hdr_t *h, uint8_t flag) {
    uint8_t all_flags = (uint8_t)(((h->version_flags_hi & 0x0F) << 4) | (h->flags_lo & 0x0F));
    return all_flags & flag;
}

/* ─── Configuration ────────────────────────────────────────────── */
typedef struct {
    uint32_t timeout_ms;     /* initial retransmit timeout  */
    uint32_t max_retries;    /* max retransmit attempts     */
    uint32_t window_size;    /* sliding window size         */
} rudp_config_t;

extern const rudp_config_t RUDP_DEFAULT_CONFIG;

/* ─── Socket Handle ────────────────────────────────────────────── */
typedef struct rudp_socket rudp_socket_t;

/* ─── Public API ───────────────────────────────────────────────── */

/**
 * rudp_socket() — Create a new RUDP socket.
 * @cfg: configuration (pass NULL for defaults)
 * Returns allocated rudp_socket_t on success, NULL on error.
 */
rudp_socket_t *rudp_socket(const rudp_config_t *cfg);

/**
 * rudp_bind() — Bind socket to local port.
 * @port: local UDP port (host byte order)
 */
int rudp_bind(rudp_socket_t *sock, uint16_t port);

/**
 * rudp_connect() — Initiate RUDP handshake to remote peer.
 * @addr: remote address string (IPv4 dotted or hostname)
 * @port: remote port (host byte order)
 */
int rudp_connect(rudp_socket_t *sock, const char *addr, uint16_t port);

/**
 * rudp_accept() — Wait for an incoming RUDP connection (server side).
 * @peer: filled with the remote address on success
 */
int rudp_accept(rudp_socket_t *sock, struct sockaddr_in *peer);

/**
 * rudp_send() — Reliably send data to connected peer.
 * Blocks until ACKed or max retries exceeded.
 * Returns bytes sent on success, -1 on error.
 */
ssize_t rudp_send(rudp_socket_t *sock, const void *buf, size_t len);

/**
 * rudp_sendto() — Send reliable data to arbitrary peer (connectionless).
 */
ssize_t rudp_sendto(rudp_socket_t *sock, const void *buf, size_t len,
                    const struct sockaddr_in *dest);

/**
 * rudp_recv() — Receive data (blocks until data arrives).
 * Returns bytes read, 0 on graceful close, -1 on error.
 */
ssize_t rudp_recv(rudp_socket_t *sock, void *buf, size_t maxlen);

/**
 * rudp_recvfrom() — Receive data + fill sender address.
 */
ssize_t rudp_recvfrom(rudp_socket_t *sock, void *buf, size_t maxlen,
                      struct sockaddr_in *from);

/**
 * rudp_close() — Graceful FIN exchange then free resources.
 */
int rudp_close(rudp_socket_t *sock);

/**
 * rudp_set_nonblocking() — Toggle non-blocking mode.
 */
int rudp_set_nonblocking(rudp_socket_t *sock, bool nonblock);

/**
 * rudp_get_stats() — Fill stats struct for monitoring.
 */
typedef struct {
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t packets_retransmitted;
    uint64_t acks_sent;
    uint64_t acks_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    double   rtt_ms;          /* smoothed RTT estimate */
} rudp_stats_t;

void rudp_get_stats(const rudp_socket_t *sock, rudp_stats_t *out);
void rudp_print_stats(const rudp_socket_t *sock);

/* ─── Utility ──────────────────────────────────────────────────── */
uint16_t rudp_checksum(const void *data, size_t len);
void     rudp_print_packet(const rudp_packet_t *pkt, const char *label);

#endif /* RUDP_H */
