/*
 * tools/rudp_nat_punch_main.c — CLI NAT Hole Punching Utility
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../rudp/rudp_nat.h"
#include "../rudp/rudp_timer.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <Rendezvous_IP> <Rendezvous_Port> [PeerID] [Room]\n", argv[0]);
        return 1;
    }

    const char *srv_ip   = argv[1];
    uint16_t    srv_port = (uint16_t)atoi(argv[2]);
    const char *peer_id  = (argc >= 4) ? argv[3] : "player_default";
    const char *room     = (argc >= 5) ? argv[4] : "game_room_1";

    printf("╔════════════════════════════════════════════╗\n");
    printf("║   RUDP NAT Hole Punching Utility           ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");

    rudp_peer_info_t my_info;
    snprintf(my_info.peer_id, sizeof(my_info.peer_id), "%s", peer_id);
    snprintf(my_info.room,    sizeof(my_info.room),    "%s", room);

    struct sockaddr_in peer_addr;
    printf("[NAT] Connecting to rendezvous %s:%u...\n", srv_ip, srv_port);
    printf("[NAT] Reporting as PeerID: %s in Room: %s\n", peer_id, room);

    if (rudp_nat_rendezvous(NULL, srv_ip, srv_port, &my_info, &peer_addr) < 0) {
        printf("[NAT] Rendezvous failed.\n");
        return 1;
    }

    char p_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, p_ip, sizeof(p_ip));
    printf("[NAT] Peer discovered at %s:%u\n", p_ip, ntohs(peer_addr.sin_port));

    printf("[NAT] Attempting to punch hole (Timeout 10s)...\n");
    if (rudp_nat_punch(NULL, &peer_addr, 10000) < 0) {
        printf("[NAT] Punching failed. Both peers must run this simultaneously!\n");
        return 1;
    }

    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║   SUCCESS: HOLE PUNCHED!                   ║\n");
    printf("║   Target IP:   %-15s             ║\n", p_ip);
    printf("║   Target Port: %-5u                       ║\n", ntohs(peer_addr.sin_port));
    printf("╚════════════════════════════════════════════╝\n\n");
    printf("You can now enter this IP/Port in your Raylib Game to connect!\n");

    return 0;
}
