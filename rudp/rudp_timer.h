/*
 * rudp_timer.h — RUDP Retransmission Timer
 * =========================================
 * Tracks per-packet deadlines for retransmission.
 * Uses CLOCK_MONOTONIC for high-resolution, drift-free timing.
 */

#ifndef RUDP_TIMER_H
#define RUDP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ─── RTT Estimator (Jacobson/Karels) ─────────────────────────── */
typedef struct {
    double srtt;      /* smoothed RTT (ms)                */
    double rttvar;    /* RTT variance (ms)                */
    double rto;       /* retransmit timeout (ms)          */
} rudp_rtt_t;

void   rtt_init(rudp_rtt_t *r, double initial_ms);
void   rtt_update(rudp_rtt_t *r, double measured_ms);
double rtt_get_rto(const rudp_rtt_t *r);

/* ─── Per-Packet Timer ─────────────────────────────────────────── */
typedef struct {
    struct timespec deadline;   /* absolute time to retransmit   */
    uint32_t        seq;        /* packet sequence number        */
    bool            active;     /* timer running?                */
} rudp_timer_t;

/* Start timer for a packet with given timeout in ms. */
void   timer_start(rudp_timer_t *t, uint32_t seq, uint32_t timeout_ms);

/* Returns true if the timer has expired. */
bool   timer_expired(const rudp_timer_t *t);

/* Cancel (deactivate) a timer. */
void   timer_cancel(rudp_timer_t *t);

/* ─── Utility ──────────────────────────────────────────────────── */

/* Current time as timespec (CLOCK_MONOTONIC). */
void   clock_now(struct timespec *out);

/* Elapsed milliseconds since a timespec. */
double elapsed_ms(const struct timespec *since);

/* Sleep for given number of milliseconds. */
void   sleep_ms(uint32_t ms);

/* Add milliseconds to a timespec. */
void   timespec_add_ms(struct timespec *ts, uint32_t ms);

/* Compare two timespecs: returns <0, 0, or >0. */
int    timespec_cmp(const struct timespec *a, const struct timespec *b);

#endif /* RUDP_TIMER_H */
