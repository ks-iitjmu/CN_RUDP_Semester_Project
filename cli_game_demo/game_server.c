/*
 * game_server.c — P2P Multiplayer Game: Position Broadcaster (Hub Node)
 * ======================================================================
 * This acts as a "relay" peer that broadcasts all received positions
 * to every other connected peer. In a truly serverless setup, you can
 * run multiple clients and one of them can double as the relay.
 *
 * Usage:  ./game_server [--proto udp|tcp|rudp] [--port <port>]
 *
 * Default: RUDP on port 9000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include "../transport/transport.h"

/* ─── Game Position Packet ─────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  player_id;
    float    x;
    float    y;
    uint64_t timestamp_us;  /* microseconds since epoch */
} game_pos_t;
#pragma pack(pop)

/* ─── Peer Table ───────────────────────────────────────────────── */
#define MAX_PLAYERS 8
static struct {
    struct sockaddr_in addr;
    uint8_t            player_id;
    int                active;
    game_pos_t         last_pos;
} peers[MAX_PLAYERS];
static int peer_count = 0;
static pthread_mutex_t peer_lock = PTHREAD_MUTEX_INITIALIZER;

static int udp_srv_fd = -1;
static volatile int running = 1;

static void sig_handler(int s) { (void)s; running = 0; }

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

/* Broadcast position to all peers except sender */
static void broadcast(const game_pos_t *pos, const struct sockaddr_in *sender) {
    pthread_mutex_lock(&peer_lock);
    for (int i = 0; i < peer_count; i++) {
        if (!peers[i].active) continue;
        if (peers[i].addr.sin_addr.s_addr == sender->sin_addr.s_addr &&
            peers[i].addr.sin_port        == sender->sin_port)
            continue;
        sendto(udp_srv_fd, pos, sizeof(*pos), 0,
               (struct sockaddr*)&peers[i].addr, sizeof(peers[i].addr));
    }
    pthread_mutex_unlock(&peer_lock);
}

/* Register or update a peer */
static uint8_t register_peer(const struct sockaddr_in *addr) {
    pthread_mutex_lock(&peer_lock);
    for (int i = 0; i < peer_count; i++) {
        if (peers[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            peers[i].addr.sin_port        == addr->sin_port) {
            pthread_mutex_unlock(&peer_lock);
            return peers[i].player_id;
        }
    }
    if (peer_count < MAX_PLAYERS) {
        peers[peer_count].addr      = *addr;
        peers[peer_count].player_id = (uint8_t)(peer_count + 1);
        peers[peer_count].active    = 1;
        uint8_t id = peers[peer_count].player_id;
        peer_count++;
        printf("[Server] New player %u joined from %s:%u  (total: %d)\n",
               id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), peer_count);
        pthread_mutex_unlock(&peer_lock);
        return id;
    }
    pthread_mutex_unlock(&peer_lock);
    return 0; /* full */
}

int main(int argc, char *argv[]) {
    uint16_t port = 9000;
    transport_proto_t proto = PROTO_RUDP;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--port")  == 0) port = (uint16_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--proto") == 0) transport_parse_proto(argv[i+1], &proto);
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════╗\n");
    printf("║   RUDP P2P Game Server (Hub Mode)    ║\n");
    printf("║   Protocol: %-6s   Port: %-5u     ║\n", proto_name(proto), port);
    printf("╚══════════════════════════════════════╝\n\n");

    /* For simplicity, use raw UDP for game_server (same interface for all) */
    udp_srv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);
    int opt = 1;
    setsockopt(udp_srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(udp_srv_fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("bind"); return 1;
    }
    printf("[Server] Listening on UDP port %u...\n\n", port);

    while (running) {
        game_pos_t pos;
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);

        ssize_t n = recvfrom(udp_srv_fd, &pos, sizeof(pos), 0,
                             (struct sockaddr*)&from, &flen);
        if (n != sizeof(pos)) continue;

        uint8_t pid = register_peer(&from);
        pos.player_id = pid;

        uint64_t latency = now_us() - pos.timestamp_us;
        printf("[Server] Player %u  pos=(%.1f, %.1f)  latency=%luµs\n",
               pid, pos.x, pos.y, (unsigned long)latency);

        broadcast(&pos, &from);
    }

    printf("\n[Server] Shutting down.\n");
    close(udp_srv_fd);
    return 0;
}
