/*
 * transport.h — Protocol-Agnostic Transport Abstraction Layer
 * ============================================================
 * Provides a unified send/recv interface over UDP, TCP, or RUDP.
 * The game only calls transport_send() / transport_recv() — zero
 * protocol awareness in application code.
 *
 * Usage:
 *   transport_t *t = transport_init(PROTO_RUDP, "192.168.1.5", 9000);
 *   transport_send(t, data, len);
 *   transport_recv(t, buf, maxlen);
 *   transport_close(t);
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>

/* ─── Protocol Selector ────────────────────────────────────────── */
typedef enum {
    PROTO_UDP  = 0,
    PROTO_TCP  = 1,
    PROTO_RUDP = 2,
} transport_proto_t;

static inline const char *proto_name(transport_proto_t p) {
    switch (p) {
        case PROTO_UDP:  return "UDP";
        case PROTO_TCP:  return "TCP";
        case PROTO_RUDP: return "RUDP";
        default:         return "UNKNOWN";
    }
}

/* ─── Transport Handle ─────────────────────────────────────────── */
typedef struct transport transport_t;

/* ─── API ──────────────────────────────────────────────────────── */

/**
 * transport_server() — Create a server-side transport.
 * Binds to local port and waits for incoming connection (TCP/RUDP)
 * or is ready for recvfrom (UDP). Blocks until a client connects.
 */
transport_t *transport_server(transport_proto_t proto, uint16_t port);

/**
 * transport_client() — Create a client-side transport.
 * Connects to remote host:port.
 */
transport_t *transport_client(transport_proto_t proto,
                               const char *host, uint16_t port);

/**
 * transport_send() — Send data reliably (or best-effort for UDP).
 * Returns bytes sent, -1 on error.
 */
ssize_t transport_send(transport_t *t, const void *buf, size_t len);

/**
 * transport_recv() — Receive data.
 * Returns bytes received, 0 on graceful close, -1 on error.
 */
ssize_t transport_recv(transport_t *t, void *buf, size_t maxlen);

/**
 * transport_sendto() — Send to explicit destination (UDP/RUDP only).
 */
ssize_t transport_sendto(transport_t *t, const void *buf, size_t len,
                          const struct sockaddr_in *dest);

/**
 * transport_recvfrom() — Receive + get sender address.
 */
ssize_t transport_recvfrom(transport_t *t, void *buf, size_t maxlen,
                            struct sockaddr_in *from);

/**
 * transport_close() — Close and free transport.
 */
void transport_close(transport_t *t);

/**
 * transport_get_proto() — Return active protocol.
 */
transport_proto_t transport_get_proto(const transport_t *t);

/**
 * transport_parse_proto() — Parse "udp"/"tcp"/"rudp" string.
 * Returns -1 on unknown string.
 */
int transport_parse_proto(const char *name, transport_proto_t *out);

#endif /* TRANSPORT_H */
