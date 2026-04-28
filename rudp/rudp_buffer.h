/*
 * rudp_buffer.h — RUDP Sliding-Window Send/Recv Buffers
 * ======================================================
 * Provides a fixed-size circular buffer for:
 *   - Send window: unACKed packets waiting for confirmation
 *   - Recv window: reorder buffer for out-of-order packets
 */

#ifndef RUDP_BUFFER_H
#define RUDP_BUFFER_H

#include "rudp.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

/* ─── Send Buffer Entry ───────────────────────────────────────── */
typedef struct {
    rudp_packet_t   pkt;            /* full packet (hdr + payload)   */
    size_t          pkt_len;        /* total length (hdr + payload)  */
    struct timespec sent_at;        /* when it was last transmitted  */
    uint32_t        retries;        /* number of retransmissions     */
    bool            in_use;         /* slot occupied?                */
    bool            acked;          /* has ACK been received?        */
    struct sockaddr_in dest;        /* destination address           */
} rudp_send_entry_t;

/* ─── Recv Buffer Entry ───────────────────────────────────────── */
typedef struct {
    rudp_packet_t   pkt;
    size_t          payload_len;
    bool            in_use;
    struct sockaddr_in from;
} rudp_recv_entry_t;

/* ─── Send Window ─────────────────────────────────────────────── */
typedef struct {
    rudp_send_entry_t entries[RUDP_WINDOW_SIZE];
    uint32_t          base_seq;      /* oldest unACKed seq           */
    uint32_t          next_seq;      /* next seq to assign           */
    uint32_t          window_size;
    pthread_mutex_t   lock;
    pthread_cond_t    space_avail;   /* signalled when window opens  */
} rudp_send_window_t;

/* ─── Recv Window ─────────────────────────────────────────────── */
typedef struct {
    rudp_recv_entry_t entries[RUDP_WINDOW_SIZE];
    uint32_t          expected_seq;  /* next expected in-order seq   */
    uint32_t          window_size;
    pthread_mutex_t   lock;
    pthread_cond_t    data_avail;    /* signalled when data ready    */
} rudp_recv_window_t;

/* ─── Send Window API ─────────────────────────────────────────── */
int  send_window_init(rudp_send_window_t *w, uint32_t window_size);
void send_window_destroy(rudp_send_window_t *w);

/* Add packet to window (blocks if full). Returns slot index or -1. */
int  send_window_add(rudp_send_window_t *w, const rudp_packet_t *pkt,
                     size_t pkt_len, const struct sockaddr_in *dest);

/* Mark a sequence number as ACKed (cumulative). */
void send_window_ack(rudp_send_window_t *w, uint32_t ack_seq);

/* Get next packet to retransmit (timed-out, not yet ACKed). */
rudp_send_entry_t *send_window_get_timedout(rudp_send_window_t *w,
                                             uint32_t timeout_ms);

/* True if window is full. */
bool send_window_full(const rudp_send_window_t *w);

/* True if all packets ACKed. */
bool send_window_empty(const rudp_send_window_t *w);

/* ─── Recv Window API ─────────────────────────────────────────── */
int  recv_window_init(rudp_recv_window_t *w, uint32_t window_size);
void recv_window_destroy(rudp_recv_window_t *w);

/* Insert an incoming packet into reorder buffer. */
int  recv_window_insert(rudp_recv_window_t *w, const rudp_packet_t *pkt,
                        size_t payload_len, const struct sockaddr_in *from);

/* Retrieve the next in-order packet (blocks until available). */
int  recv_window_get(rudp_recv_window_t *w, rudp_packet_t *out,
                     size_t *payload_len, struct sockaddr_in *from);

/* Non-blocking peek — returns true if data ready. */
/* Returns the highest contiguous sequence number buffered (Cumulative ACK) */
uint32_t recv_window_get_cum_ack(rudp_recv_window_t *w);

#endif /* RUDP_BUFFER_H */
