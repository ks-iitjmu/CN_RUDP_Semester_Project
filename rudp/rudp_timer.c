/*
 * rudp_timer.c — RUDP Retransmission Timer Implementation
 */

#include "rudp_timer.h"
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>

/* ─── RTT Estimator (Jacobson/Karels Algorithm) ───────────────── */

#define RTT_ALPHA  0.125   /* SRTT smoothing factor          */
#define RTT_BETA   0.25    /* RTTVAR smoothing factor        */
#define RTT_K      4.0     /* RTO = SRTT + K * RTTVAR        */
#define RTT_MIN    50.0    /* minimum RTO (ms)               */
#define RTT_MAX    6000.0  /* maximum RTO (ms)               */

void rtt_init(rudp_rtt_t *r, double initial_ms) {
    r->srtt   = initial_ms;
    r->rttvar = initial_ms / 2.0;
    r->rto    = initial_ms + RTT_K * (initial_ms / 2.0);
    if (r->rto < RTT_MIN) r->rto = RTT_MIN;
    if (r->rto > RTT_MAX) r->rto = RTT_MAX;
}

void rtt_update(rudp_rtt_t *r, double measured_ms) {
    /* Jacobson/Karels RTTVAR and SRTT update */
    double err = measured_ms - r->srtt;
    r->srtt   += RTT_ALPHA * err;
    r->rttvar  = (1.0 - RTT_BETA) * r->rttvar + RTT_BETA * fabs(err);
    r->rto     = r->srtt + RTT_K * r->rttvar;

    /* Clamp RTO */
    if (r->rto < RTT_MIN) r->rto = RTT_MIN;
    if (r->rto > RTT_MAX) r->rto = RTT_MAX;
}

double rtt_get_rto(const rudp_rtt_t *r) {
    return r->rto;
}

/* ─── Per-Packet Timer ─────────────────────────────────────────── */

void timer_start(rudp_timer_t *t, uint32_t seq, uint32_t timeout_ms) {
    clock_now(&t->deadline);
    timespec_add_ms(&t->deadline, timeout_ms);
    t->seq    = seq;
    t->active = true;
}

bool timer_expired(const rudp_timer_t *t) {
    if (!t->active) return false;
    struct timespec now;
    clock_now(&now);
    return timespec_cmp(&now, &t->deadline) >= 0;
}

void timer_cancel(rudp_timer_t *t) {
    t->active = false;
}

/* ─── Utility ──────────────────────────────────────────────────── */

void clock_now(struct timespec *out) {
    clock_gettime(CLOCK_MONOTONIC, out);
}

double elapsed_ms(const struct timespec *since) {
    struct timespec now;
    clock_now(&now);
    double sec  = (double)(now.tv_sec  - since->tv_sec);
    double nsec = (double)(now.tv_nsec - since->tv_nsec);
    return sec * 1000.0 + nsec / 1e6;
}

void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ; /* restart on signal */
}

void timespec_add_ms(struct timespec *ts, uint32_t ms) {
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    ts->tv_sec  += ms / 1000;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

int timespec_cmp(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec != b->tv_sec)
        return (a->tv_sec > b->tv_sec) ? 1 : -1;
    if (a->tv_nsec != b->tv_nsec)
        return (a->tv_nsec > b->tv_nsec) ? 1 : -1;
    return 0;
}
