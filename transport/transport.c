/*
 * transport.c — Transport Abstraction Layer Implementation
 * ========================================================
 * Dispatches to raw UDP socket calls, POSIX TCP sockets,
 * or our custom RUDP library based on the protocol selector.
 */

#define _POSIX_C_SOURCE 200112L   /* for getaddrinfo, strcasecmp */
#include "transport.h"
#include "../rudp/rudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

/* ─── Internal Transport Struct ────────────────────────────────── */
struct transport {
    transport_proto_t  proto;

    /* UDP / TCP */
    int                fd;            /* raw socket fd                */
    struct sockaddr_in peer;          /* remote addr (UDP/TCP)        */
    socklen_t          peer_len;

    /* RUDP */
    rudp_socket_t     *rudp;
};

/* ─── Resolve hostname to IPv4 ──────────────────────────────────  */
static int _resolve(const char *host, struct in_addr *out) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -1;
    *out = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

/* ──────────────────────────────────────────────────────────────── */
/*  SERVER SIDE                                                       */
/* ──────────────────────────────────────────────────────────────── */

transport_t *transport_server(transport_proto_t proto, uint16_t port) {
    transport_t *t = calloc(1, sizeof(transport_t));
    if (!t) return NULL;
    t->proto = proto;

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons(port);

    switch (proto) {

    case PROTO_UDP: {
        t->fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (t->fd < 0) { perror("UDP socket"); free(t); return NULL; }
        int opt = 1;
        setsockopt(t->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(t->fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
            perror("UDP bind"); close(t->fd); free(t); return NULL;
        }
        printf("[Transport/UDP] Listening on port %u\n", port);
        break;
    }

    case PROTO_TCP: {
        int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (srv_fd < 0) { perror("TCP socket"); free(t); return NULL; }
        int opt = 1;
        setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(srv_fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
            perror("TCP bind"); close(srv_fd); free(t); return NULL;
        }
        listen(srv_fd, 5);
        printf("[Transport/TCP] Waiting for connection on port %u...\n", port);
        socklen_t plen = sizeof(t->peer);
        t->fd = accept(srv_fd, (struct sockaddr*)&t->peer, &plen);
        close(srv_fd);
        if (t->fd < 0) { perror("TCP accept"); free(t); return NULL; }
        printf("[Transport/TCP] Client connected from %s:%u\n",
               inet_ntoa(t->peer.sin_addr), ntohs(t->peer.sin_port));
        break;
    }

    case PROTO_RUDP: {
        t->rudp = rudp_socket(NULL);
        if (!t->rudp) { free(t); return NULL; }
        if (rudp_bind(t->rudp, port) < 0) {
            rudp_close(t->rudp); free(t); return NULL;
        }
        if (rudp_accept(t->rudp, &t->peer) < 0) {
            rudp_close(t->rudp); free(t); return NULL;
        }
        break;
    }
    }

    return t;
}

/* ──────────────────────────────────────────────────────────────── */
/*  CLIENT SIDE                                                       */
/* ──────────────────────────────────────────────────────────────── */

transport_t *transport_client(transport_proto_t proto,
                               const char *host, uint16_t port) {
    transport_t *t = calloc(1, sizeof(transport_t));
    if (!t) return NULL;
    t->proto    = proto;
    t->peer_len = sizeof(t->peer);

    t->peer.sin_family = AF_INET;
    t->peer.sin_port   = htons(port);
    if (_resolve(host, &t->peer.sin_addr) < 0) {
        fprintf(stderr, "[Transport] Cannot resolve %s\n", host);
        free(t); return NULL;
    }

    switch (proto) {

    case PROTO_UDP: {
        t->fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (t->fd < 0) { perror("UDP socket"); free(t); return NULL; }
        /* "connect" just sets default dest for send() */
        connect(t->fd, (struct sockaddr*)&t->peer, sizeof(t->peer));
        printf("[Transport/UDP] Ready → %s:%u\n", host, port);
        break;
    }

    case PROTO_TCP: {
        t->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (t->fd < 0) { perror("TCP socket"); free(t); return NULL; }
        if (connect(t->fd, (struct sockaddr*)&t->peer,
                    sizeof(t->peer)) < 0) {
            perror("TCP connect"); close(t->fd); free(t); return NULL;
        }
        printf("[Transport/TCP] Connected → %s:%u\n", host, port);
        break;
    }

    case PROTO_RUDP: {
        t->rudp = rudp_socket(NULL);
        if (!t->rudp) { free(t); return NULL; }
        /* Bind to ephemeral port first */
        if (rudp_bind(t->rudp, 0) < 0) {
            rudp_close(t->rudp); free(t); return NULL;
        }
        if (rudp_connect(t->rudp, host, port) < 0) {
            rudp_close(t->rudp); free(t); return NULL;
        }
        break;
    }
    }

    return t;
}

/* ──────────────────────────────────────────────────────────────── */
/*  SEND / RECV                                                       */
/* ──────────────────────────────────────────────────────────────── */

ssize_t transport_send(transport_t *t, const void *buf, size_t len) {
    switch (t->proto) {
    case PROTO_UDP:
        return send(t->fd, buf, len, 0);
    case PROTO_TCP:
        return send(t->fd, buf, len, MSG_NOSIGNAL);
    case PROTO_RUDP:
        return rudp_send(t->rudp, buf, len);
    }
    return -1;
}

ssize_t transport_recv(transport_t *t, void *buf, size_t maxlen) {
    switch (t->proto) {
    case PROTO_UDP: {
        socklen_t slen = sizeof(t->peer);
        return recvfrom(t->fd, buf, maxlen, 0,
                        (struct sockaddr*)&t->peer, &slen);
    }
    case PROTO_TCP:
        return recv(t->fd, buf, maxlen, 0);
    case PROTO_RUDP:
        return rudp_recv(t->rudp, buf, maxlen);
    }
    return -1;
}

ssize_t transport_sendto(transport_t *t, const void *buf, size_t len,
                          const struct sockaddr_in *dest) {
    switch (t->proto) {
    case PROTO_UDP:
        return sendto(t->fd, buf, len, 0,
                      (struct sockaddr*)dest, sizeof(*dest));
    case PROTO_TCP:
        return send(t->fd, buf, len, MSG_NOSIGNAL); /* ignore dest */
    case PROTO_RUDP:
        return rudp_sendto(t->rudp, buf, len, dest);
    }
    return -1;
}

ssize_t transport_recvfrom(transport_t *t, void *buf, size_t maxlen,
                            struct sockaddr_in *from) {
    switch (t->proto) {
    case PROTO_UDP: {
        socklen_t flen = sizeof(*from);
        return recvfrom(t->fd, buf, maxlen, 0,
                        (struct sockaddr*)from, &flen);
    }
    case PROTO_TCP: {
        ssize_t n = recv(t->fd, buf, maxlen, 0);
        if (from) *from = t->peer;
        return n;
    }
    case PROTO_RUDP:
        return rudp_recvfrom(t->rudp, buf, maxlen, from);
    }
    return -1;
}

void transport_close(transport_t *t) {
    if (!t) return;
    switch (t->proto) {
    case PROTO_UDP:
    case PROTO_TCP:
        close(t->fd);
        break;
    case PROTO_RUDP:
        if (t->rudp) rudp_close(t->rudp);
        break;
    }
    free(t);
}

transport_proto_t transport_get_proto(const transport_t *t) {
    return t->proto;
}

int transport_parse_proto(const char *name, transport_proto_t *out) {
    if (strcasecmp(name, "udp")  == 0) { *out = PROTO_UDP;  return 0; }
    if (strcasecmp(name, "tcp")  == 0) { *out = PROTO_TCP;  return 0; }
    if (strcasecmp(name, "rudp") == 0) { *out = PROTO_RUDP; return 0; }
    return -1;
}
