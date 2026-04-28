/*
 * rudp_nat.h — UDP Hole Punching for RUDP NAT Traversal
 * =======================================================
 * Implements a simple rendezvous-based hole-punching scheme:
 *
 *   1. Both peers register their (public IP, port) with a
 *      lightweight rendezvous server.
 *   2. The rendezvous server exchanges endpoint info between peers.
 *   3. Both peers simultaneously send RUDP_FLAG_NAT packets to
 *      each other's public endpoint, opening firewall pinholes.
 *   4. Once the hole is open, normal RUDP traffic flows directly.
 *
 * On a LAN / hotspot, you can skip the rendezvous server and
 * call rudp_nat_punch_direct() with the known local IP instead.
 */

#ifndef RUDP_NAT_H
#define RUDP_NAT_H

#include "rudp.h"
#include <netinet/in.h>
#include <stdint.h>

/* ─── Rendezvous Peer Info ─────────────────────────────────────── */
typedef struct {
    char     ip[INET_ADDRSTRLEN];
    uint16_t port;
    char     room[64];      /* room/session identifier          */
    char     peer_id[32];   /* unique peer name                 */
} rudp_peer_info_t;

/* ─── NAT API ──────────────────────────────────────────────────── */

/**
 * rudp_nat_rendezvous() — Register with rendezvous server and
 * obtain the remote peer's public endpoint.
 *
 * @sock          : bound RUDP socket to use
 * @server_ip     : rendezvous server IP or hostname
 * @server_port   : rendezvous server port
 * @my_info       : local peer_id + room to register
 * @peer_out      : filled with remote peer's endpoint on success
 *
 * Returns 0 on success, -1 on error (timeout / server unreachable).
 */
int rudp_nat_rendezvous(rudp_socket_t *sock,
                        const char   *server_ip,
                        uint16_t      server_port,
                        const rudp_peer_info_t *my_info,
                        struct sockaddr_in     *peer_out);

/**
 * rudp_nat_punch() — Perform UDP hole punching toward `peer`.
 * Sends a burst of RUDP_FLAG_NAT probes to open the NAT pinhole.
 * Then waits for the peer's NAT probes to arrive.
 *
 * @sock      : bound RUDP socket
 * @peer      : remote peer's public endpoint
 * @timeout_ms: how long to try before giving up
 *
 * Returns 0 when bidirectional path is confirmed, -1 on timeout.
 */
int rudp_nat_punch(rudp_socket_t *sock,
                   const struct sockaddr_in *peer,
                   uint32_t timeout_ms);

/**
 * rudp_nat_punch_direct() — LAN/hotspot shortcut.
 * Skips rendezvous; directly punches toward a known local IP.
 */
int rudp_nat_punch_direct(rudp_socket_t *sock,
                          const char    *peer_ip,
                          uint16_t       peer_port,
                          uint32_t       timeout_ms);

/* ─── Rendezvous Server (run standalone) ───────────────────────── */

/**
 * rudp_rendezvous_server_run() — Simple room-based matchmaking.
 * Blocks forever serving peer registrations on `port`.
 * Run this on a machine reachable by all peers (e.g., hotspot host).
 */
int rudp_rendezvous_server_run(uint16_t port);

#endif /* RUDP_NAT_H */
