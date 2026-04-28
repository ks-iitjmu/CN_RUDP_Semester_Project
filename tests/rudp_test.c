/*
 * tests/rudp_test.c — RUDP Unit / Integration Test
 * ==================================================
 * Runs a loopback test: spawns a server thread and a client thread
 * in the same process, sends N messages over RUDP, verifies receipt.
 *
 * Usage: ./rudp_test [--count <N>] [--size <bytes>] [--loss <pct>]
 *
 * The --loss flag uses tc netem (requires sudo) to inject loss.
 * Without it, just tests clean loopback delivery.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include "../rudp/rudp.h"
#include "../rudp/rudp_timer.h"

#define TEST_PORT   9500
#define TEST_MAGIC  0xDEADBEEF

typedef struct {
    uint32_t magic;
    uint32_t seq;
    char     data[64];
} test_msg_t;

static uint32_t g_count  = 100;
static uint32_t g_size   = sizeof(test_msg_t);
static uint32_t g_recv   = 0;
static uint32_t g_errors = 0;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ─── Server Thread ────────────────────────────────────────────── */
static void *server_thread(void *arg) {
    (void)arg;
    rudp_socket_t *srv = rudp_socket(NULL);
    assert(srv != NULL);
    assert(rudp_bind(srv, TEST_PORT) == 0);

    printf("[Test/Server] Bound to port %u, waiting...\n", TEST_PORT);
    rudp_accept(srv, NULL);

    test_msg_t msg;
    for (uint32_t i = 0; i < g_count; i++) {
        ssize_t n = rudp_recv(srv, &msg, sizeof(msg));
        if (n < 0) {
            fprintf(stderr, "[Test/Server] recv error #%u\n", i);
            pthread_mutex_lock(&g_lock);
            g_errors++;
            pthread_mutex_unlock(&g_lock);
            continue;
        }

        /* Validate magic */
        if (msg.magic != TEST_MAGIC) {
            fprintf(stderr, "[Test/Server] Bad magic on seq=%u\n", msg.seq);
            pthread_mutex_lock(&g_lock);
            g_errors++;
            pthread_mutex_unlock(&g_lock);
            continue;
        }

        pthread_mutex_lock(&g_lock);
        g_recv++;
        pthread_mutex_unlock(&g_lock);
    }

    rudp_close(srv);
    return NULL;
}

/* ─── Client Thread ────────────────────────────────────────────── */
static void *client_thread(void *arg) {
    (void)arg;
    sleep_ms(100); /* let server start */

    rudp_socket_t *cli = rudp_socket(NULL);
    assert(cli != NULL);
    assert(rudp_connect(cli, "127.0.0.1", TEST_PORT) == 0);

    printf("[Test/Client] Connected. Sending %u messages...\n", g_count);

    uint32_t send_errors = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        test_msg_t msg = {
            .magic = TEST_MAGIC,
            .seq   = i + 1,
        };
        snprintf(msg.data, sizeof(msg.data), "Hello from seq %u", i + 1);

        ssize_t n = rudp_send(cli, &msg, sizeof(msg));
        if (n < 0) {
            fprintf(stderr, "[Test/Client] send error #%u\n", i);
            send_errors++;
        }
    }

    printf("[Test/Client] Done sending. Send errors: %u\n", send_errors);
    rudp_print_stats(cli);
    rudp_close(cli);
    return NULL;
}

/* ─── Main ────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--count") == 0) g_count = (uint32_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--size")  == 0) g_size  = (uint32_t)atoi(argv[i+1]);
    }

    printf("╔═══════════════════════════════════════╗\n");
    printf("║  RUDP Loopback Integration Test       ║\n");
    printf("║  Messages: %-5u  Payload: %-5lu bytes ║\n",
           g_count, sizeof(test_msg_t));
    printf("╚═══════════════════════════════════════╝\n\n");

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    pthread_t stid, ctid;
    pthread_create(&stid, NULL, server_thread, NULL);
    pthread_create(&ctid, NULL, client_thread, NULL);

    pthread_join(stid, NULL);
    pthread_join(ctid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec)
                   + (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    printf("\n");
    printf("╔══════════════ Test Results ══════════════╗\n");
    printf("║ Sent:      %-5u                          ║\n", g_count);
    printf("║ Received:  %-5u                          ║\n", g_recv);
    printf("║ Errors:    %-5u                          ║\n", g_errors);
    printf("║ Time:      %.3f s                        ║\n", elapsed);

    if (g_recv == g_count && g_errors == 0) {
        printf("║ Result:    ✓ PASS                        ║\n");
    } else {
        printf("║ Result:    ✗ FAIL (lost=%u err=%u)        ║\n",
               g_count - g_recv, g_errors);
    }
    printf("╚══════════════════════════════════════════╝\n");

    return (g_recv == g_count && g_errors == 0) ? 0 : 1;
}
