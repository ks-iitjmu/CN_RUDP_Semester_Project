/*
 * bench_sender.c — Protocol Benchmark Sender
 * ===========================================
 * Sends N packets of a given size over UDP, TCP, or RUDP and logs timestamps.
 *
 * Usage:
 *   ./bench_sender --proto udp|tcp|rudp --host <ip> --port <p>
 *                  --count <N> --size <bytes> [--delay <ms>]
 *
 * Output: sender.log  (seq<TAB>send_timestamp_us)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../transport/transport.h"
#include "../rudp/rudp_timer.h"

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --proto [udp|tcp|rudp] --host <ip> --port <p>\n"
        "           --count <N> --size <bytes> [--delay <ms>]\n",
        prog);
    exit(1);
}

int main(int argc, char *argv[]) {
    const char       *host  = "127.0.0.1";
    uint16_t          port  = 9001;
    uint32_t          count = 1000;
    uint32_t          size  = 512;
    uint32_t          delay = 0; /* ms between sends */
    transport_proto_t proto = PROTO_RUDP;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--proto")  == 0) transport_parse_proto(argv[i+1], &proto);
        if (strcmp(argv[i], "--host")   == 0) host  = argv[i+1];
        if (strcmp(argv[i], "--port")   == 0) port  = (uint16_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--count")  == 0) count = (uint32_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--size")   == 0) size  = (uint32_t)atoi(argv[i+1]);
        if (strcmp(argv[i], "--delay")  == 0) delay = (uint32_t)atoi(argv[i+1]);
    }
    if (size > 1400) size = 1400;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  Benchmark Sender                            ║\n");
    printf("║  Protocol: %-6s  Host: %-16s     ║\n", proto_name(proto), host);
    printf("║  Port: %-5u  Count: %-6u  Size: %-4u bytes ║\n", port, count, size);
    printf("╚══════════════════════════════════════════════╝\n\n");

    transport_t *t = transport_client(proto, host, port);
    if (!t) { fprintf(stderr, "Failed to connect\n"); return 1; }

    FILE *log = fopen("sender.log", "w");
    if (!log) { perror("sender.log"); transport_close(t); return 1; }
    fprintf(log, "seq\tsend_us\n");

    uint8_t *buf = calloc(1, size);
    if (!buf) { fclose(log); transport_close(t); return 1; }

    /* Embed sequence number in first 4 bytes of payload */
    uint64_t t_start = now_us();
    uint32_t lost = 0;

    for (uint32_t seq = 1; seq <= count; seq++) {
        memcpy(buf, &seq, sizeof(seq));
        uint64_t ts = now_us();
        ssize_t n = transport_send(t, buf, size);
        fprintf(log, "%u\t%lu\n", seq, (unsigned long)ts);
        if (n < 0) { lost++; fprintf(stderr, "[Sender] send error seq=%u\n", seq); }

        if (delay > 0) sleep_ms(delay);

        if (seq % 100 == 0)
            printf("[Sender] Sent %u/%u packets...\r", seq, count);
        fflush(stdout);
    }

    uint64_t t_end = now_us();
    double elapsed_s = (double)(t_end - t_start) / 1e6;
    double throughput = (double)(count * size) / elapsed_s / 1024.0;

    fclose(log);
    free(buf);
    transport_close(t);

    printf("\n\n[Sender] Done.\n");
    printf("  Sent:      %u packets\n",   count);
    printf("  Lost (TX): %u\n",           lost);
    printf("  Elapsed:   %.3f s\n",       elapsed_s);
    printf("  Throughput: %.2f KB/s\n",   throughput);
    printf("  Log:        sender.log\n\n");

    return 0;
}
