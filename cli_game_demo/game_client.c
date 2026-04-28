/*
 * game_client.c — P2P Multiplayer Game Client
 * ============================================
 * Connects to the game server, sends position updates every 50ms,
 * and receives remote positions to display.
 *
 * Usage:
 *   ./game_client --host <server_ip> [--port <port>] [--id <1-8>]
 *                 [--proto udp|tcp|rudp]
 *                 [--rendezvous <ip>] [--room <name>]   (for NAT)
 *
 * Controls (keyboard):
 *   WASD / arrow keys — move player
 *   Q               — quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include "../transport/transport.h"

/* ─── Game State ───────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  player_id;
    float    x;
    float    y;
    uint64_t timestamp_us;
} game_pos_t;
#pragma pack(pop)

#define MAX_PLAYERS 8
static game_pos_t players[MAX_PLAYERS];
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int running = 1;
static int client_fd  = -1;
static struct sockaddr_in server_addr;

static void sig_handler(int s) { (void)s; running = 0; }

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

/* ─── Non-blocking keyboard ────────────────────────────────────── */
static struct termios orig_termios;
static void disable_echo(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios t = orig_termios;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
static void restore_echo(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static int kbhit(void) {
    fd_set rfds;
    struct timeval tv = {0, 0};
    FD_ZERO(&rfds); FD_SET(STDIN_FILENO, &rfds);
    return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0;
}

/* ─── Render game state ────────────────────────────────────────── */
#define WORLD_W 40
#define WORLD_H 20

static void render(uint8_t my_id) {
    /* Clear screen */
    printf("\033[2J\033[H");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║     RUDP P2P Game  [You = Player %u]     ║\n", my_id);
    printf("╚══════════════════════════════════════════╝\n");

    /* Draw world border */
    char grid[WORLD_H + 2][WORLD_W + 3];
    memset(grid, ' ', sizeof(grid));

    /* Top/bottom border */
    for (int c = 0; c <= WORLD_W + 1; c++) {
        grid[0][c] = '-';
        grid[WORLD_H + 1][c] = '-';
    }
    /* Left/right border */
    for (int r = 0; r <= WORLD_H + 1; r++) {
        grid[r][0] = '|';
        grid[r][WORLD_W + 1] = '|';
        grid[r][WORLD_W + 2] = '\0';
    }

    /* Place players */
    pthread_mutex_lock(&state_lock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].player_id == 0) continue;
        int cx = 1 + (int)(players[i].x * WORLD_W / 100.0f);
        int cy = 1 + (int)(players[i].y * WORLD_H / 100.0f);
        cx = cx < 1 ? 1 : (cx > WORLD_W ? WORLD_W : cx);
        cy = cy < 1 ? 1 : (cy > WORLD_H ? WORLD_H : cy);
        grid[cy][cx] = (char)('0' + players[i].player_id);
    }
    pthread_mutex_unlock(&state_lock);

    for (int r = 0; r <= WORLD_H + 1; r++)
        printf("%s\n", grid[r]);

    printf("\nWASD = move  |  Q = quit\n");
}

/* ─── Recv thread ──────────────────────────────────────────────── */
static void *recv_thread(void *arg) {
    (void)arg;
    uint8_t buf[256];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);

    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        struct timeval tv = {0, 50000};
        if (select(client_fd + 1, &rfds, NULL, NULL, &tv) <= 0) continue;

        ssize_t n = recvfrom(client_fd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&from, &flen);
        if (n == sizeof(game_pos_t)) {
            game_pos_t *pos = (game_pos_t*)buf;
            if (pos->player_id > 0 && pos->player_id <= MAX_PLAYERS) {
                pthread_mutex_lock(&state_lock);
                players[pos->player_id - 1] = *pos;
                pthread_mutex_unlock(&state_lock);
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    const char       *host  = "127.0.0.1";
    uint16_t          port  = 9000;
    uint8_t           my_id = 0;          /* 0 = assigned by server */
    transport_proto_t proto = PROTO_RUDP;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--host")  == 0) host  = argv[i+1];
        if (strcmp(argv[i], "--port")  == 0) port  = (uint16_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--id")    == 0) my_id = (uint8_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--proto") == 0) transport_parse_proto(argv[i+1], &proto);
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Create raw UDP socket (game traffic is connectionless broadcast) */
    client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) { perror("socket"); return 1; }

    /* Bind to any port so server can send back to us */
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    bind(client_fd, (struct sockaddr*)&local, sizeof(local));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid host: %s\n", host); return 1;
    }

    printf("[Client] Protocol: %s  Server: %s:%u\n",
           proto_name(proto), host, port);

    if (my_id == 0) my_id = 1; /* default to P1 if not set */

    /* Register own initial position */
    pthread_mutex_lock(&state_lock);
    players[my_id - 1].player_id = my_id;
    players[my_id - 1].x = 50.0f;
    players[my_id - 1].y = 50.0f;
    pthread_mutex_unlock(&state_lock);

    /* Start recv thread */
    pthread_t rtid;
    pthread_create(&rtid, NULL, recv_thread, NULL);

    disable_echo();

    float  x = 50.0f, y = 50.0f;
    float  speed = 3.0f;
    struct timespec last_send;
    clock_gettime(CLOCK_MONOTONIC, &last_send);

    while (running) {
        /* Read keyboard */
        if (kbhit()) {
            int ch = getchar();
            if (ch == 'q' || ch == 'Q') break;
            if (ch == 'w' || ch == 'W') y -= speed;
            if (ch == 's' || ch == 'S') y += speed;
            if (ch == 'a' || ch == 'A') x -= speed;
            if (ch == 'd' || ch == 'D') x += speed;
            /* Arrow keys */
            if (ch == '\033') {
                getchar(); /* [ */
                int arrow = getchar();
                if (arrow == 'A') y -= speed;
                if (arrow == 'B') y += speed;
                if (arrow == 'C') x += speed;
                if (arrow == 'D') x -= speed;
            }
            /* Clamp to world */
            x = x < 0 ? 0 : (x > 100 ? 100 : x);
            y = y < 0 ? 0 : (y > 100 ? 100 : y);
        }

        /* Send position every 50ms */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms_val = (now.tv_sec - last_send.tv_sec) * 1000L
                            + (now.tv_nsec - last_send.tv_nsec) / 1000000L;
        if (elapsed_ms_val >= 50) {
            game_pos_t pos = {
                .player_id    = my_id,
                .x            = x,
                .y            = y,
                .timestamp_us = now_us(),
            };
            sendto(client_fd, &pos, sizeof(pos), 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));

            pthread_mutex_lock(&state_lock);
            players[my_id - 1] = pos;
            pthread_mutex_unlock(&state_lock);

            last_send = now;
            render(my_id);
        }

        usleep(5000); /* 5ms loop */
    }

    running = 0;
    restore_echo();
    pthread_join(rtid, NULL);
    close(client_fd);
    printf("\n[Client] Disconnected.\n");
    return 0;
}
