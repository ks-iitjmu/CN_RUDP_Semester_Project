/*
 * tools/rudp_rendezvous_main.c
 * ============================
 * Standalone entry point for the RUDP NAT rendezvous server.
 * Just calls rudp_rendezvous_server_run() from rudp_nat.c.
 *
 * Usage: ./rudp_rendezvous [port]   (default port: 9999)
 *
 * Run this on a machine reachable by all peers (hotspot host or public IP).
 * Both game clients then pass --rendezvous <ip>:9999 to discover each other.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../rudp/rudp_nat.h"

int main(int argc, char *argv[]) {
    uint16_t port = 9999;
    if (argc > 1) port = (uint16_t)atoi(argv[1]);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   RUDP Rendezvous (NAT Traversal) Server ║\n");
    printf("║   Port: %-5u                            ║\n", port);
    printf("╚══════════════════════════════════════════╝\n\n");
    printf("Peers connect here to discover each other's public endpoint.\n");
    printf("Press Ctrl+C to stop.\n\n");

    return rudp_rendezvous_server_run(port);
}
