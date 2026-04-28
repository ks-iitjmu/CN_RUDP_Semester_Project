/*
 * bench_receiver.c — Protocol Benchmark Receiver
 * ================================================
 * Receives N packets over UDP, TCP, or RUDP and logs arrival timestamps.
 *
 * Usage:
 *   ./bench_receiver --proto udp|tcp|rudp --port <p> [--count <N>] [--timeout <s>]
 *
 * Output: receiver.log  (seq<TAB>recv_timestamp_us)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "../transport/transport.h"

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --proto [udp|tcp|rudp] --port <p> [--count <N>] [--timeout <s>]\n",
        prog);
    exit(1);
}

int main(int argc, char *argv[]) {
    uint16_t          port    = 9001;
    uint32_t          count   = 1000;
    uint32_t          timeout = 30;   /* seconds */
    transport_proto_t proto   = PROTO_RUDP;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--proto")   == 0) transport_parse_proto(argv[i+1], &proto);
        if (strcmp(argv[i], "--port")    == 0) port    = (uint16_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--count")   == 0) count   = (uint32_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--timeout") == 0) timeout = (uint32_t)atoi(argv[i+1]);
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  Benchmark Receiver                          ║\n");
    printf("║  Protocol: %-6s  Port: %-5u               ║\n", proto_name(proto), port);
    printf("║  Expecting: %-6u packets                    ║\n", count);
    printf("╚══════════════════════════════════════════════╝\n\n");

    transport_t *t = transport_server(proto, port);
    if (!t) { fprintf(stderr, "Failed to bind\n"); return 1; }

    FILE *log = fopen("receiver.log", "w");
    if (!log) { perror("receiver.log"); transport_close(t); return 1; }
    fprintf(log, "seq\trecv_us\n");

    uint8_t buf[1500];
    uint32_t recv_count = 0;
    uint32_t dup_count  = 0;
    uint32_t seen[65536]; /* simple bitmap for dup detection */
    memset(seen, 0, sizeof(seen));

    time_t deadline = time(NULL) + (time_t)timeout;

    while (running && recv_count < count && time(NULL) < deadline) {
        ssize_t n = transport_recv(t, buf, sizeof(buf));
        if (n < 4) continue;

        uint64_t ts = now_us();
        uint32_t seq;
        memcpy(&seq, buf, sizeof(seq));

        /* Duplicate detection */
        if (seq < 65536 && seen[seq]) {
            dup_count++;
            continue;
        }
        if (seq < 65536) seen[seq] = 1;

        fprintf(log, "%u\t%lu\n", seq, (unsigned long)ts);
        recv_count++;

        if (recv_count % 100 == 0)
            printf("[Receiver] Got %u/%u packets...\r", recv_count, count);
        fflush(stdout);
    }

    fclose(log);
    transport_close(t);

    printf("\n\n[Receiver] Done.\n");
    printf("  Received:   %u / %u packets\n", recv_count, count);
    printf("  Duplicates: %u\n",              dup_count);
    printf("  Loss:       %u (%.1f%%)\n",     count - recv_count,
           100.0f * (float)(count - recv_count) / (float)count);
    printf("  Log:        receiver.log\n\n");

    return 0;
}
