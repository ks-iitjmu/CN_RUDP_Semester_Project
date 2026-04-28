/*
 * rudp_buffer.c — RUDP Sliding-Window Send/Recv Buffer Implementation
 */

#include "rudp_buffer.h"
#include "rudp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

/* ──────────────────────────────────────────────────────────────── */
/*  SEND WINDOW                                                      */
/* ──────────────────────────────────────────────────────────────── */

int send_window_init(rudp_send_window_t *w, uint32_t window_size) {
    memset(w, 0, sizeof(*w));
    w->window_size = (window_size <= RUDP_WINDOW_SIZE) ? window_size : RUDP_WINDOW_SIZE;
    w->base_seq    = 0;
    w->next_seq    = 0;
    if (pthread_mutex_init(&w->lock, NULL) != 0) return -1;
    if (pthread_cond_init(&w->space_avail, NULL) != 0) {
        pthread_mutex_destroy(&w->lock);
        return -1;
    }
    return 0;
}

void send_window_destroy(rudp_send_window_t *w) {
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->space_avail);
}

/* Count unACKed packets in window. */
static uint32_t _send_window_in_flight(const rudp_send_window_t *w) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < w->window_size; i++) {
        if (w->entries[i].in_use && !w->entries[i].acked)
            count++;
    }
    return count;
}

int send_window_add(rudp_send_window_t *w, const rudp_packet_t *pkt,
                    size_t pkt_len, const struct sockaddr_in *dest) {
    pthread_mutex_lock(&w->lock);

    /* Block while window is full */
    while (_send_window_in_flight(w) >= w->window_size)
        pthread_cond_wait(&w->space_avail, &w->lock);

    /* Find a free slot */
    int slot = -1;
    for (uint32_t i = 0; i < w->window_size; i++) {
        if (!w->entries[i].in_use) { slot = (int)i; break; }
    }

    if (slot == -1) {
        pthread_mutex_unlock(&w->lock);
        return -1;
    }

    rudp_send_entry_t *e = &w->entries[slot];
    memcpy(&e->pkt,  pkt,  sizeof(rudp_packet_t));
    e->pkt_len = pkt_len;
    e->retries = 0;
    e->in_use  = true;
    e->acked   = false;
    if (dest) e->dest = *dest;
    clock_now(&e->sent_at);

    pthread_mutex_unlock(&w->lock);
    return slot;
}

void send_window_ack(rudp_send_window_t *w, uint32_t ack_seq) {
    pthread_mutex_lock(&w->lock);

    /* Cumulative ACK: mark all seq <= ack_seq as acked */
    for (uint32_t i = 0; i < w->window_size; i++) {
        rudp_send_entry_t *e = &w->entries[i];
        if (e->in_use && !e->acked) {
            uint32_t seq = ntohl(e->pkt.hdr.seq);
            if (seq <= ack_seq) {
                e->acked  = true;
                e->in_use = false;
            }
        }
    }

    pthread_cond_broadcast(&w->space_avail);
    pthread_mutex_unlock(&w->lock);
}

rudp_send_entry_t *send_window_get_timedout(rudp_send_window_t *w,
                                             uint32_t timeout_ms) {
    pthread_mutex_lock(&w->lock);
    rudp_send_entry_t *result = NULL;

    for (uint32_t i = 0; i < w->window_size; i++) {
        rudp_send_entry_t *e = &w->entries[i];
        if (e->in_use && !e->acked) {
            double age = elapsed_ms(&e->sent_at);
            if (age >= (double)timeout_ms) {
                result = e;
                break;
            }
        }
    }

    pthread_mutex_unlock(&w->lock);
    return result;
}

bool send_window_full(const rudp_send_window_t *w) {
    return _send_window_in_flight(w) >= w->window_size;
}

bool send_window_empty(const rudp_send_window_t *w) {
    return _send_window_in_flight(w) == 0;
}

/* ──────────────────────────────────────────────────────────────── */
/*  RECV WINDOW                                                      */
/* ──────────────────────────────────────────────────────────────── */

int recv_window_init(rudp_recv_window_t *w, uint32_t window_size) {
    memset(w, 0, sizeof(*w));
    w->window_size = (window_size <= RUDP_WINDOW_SIZE) ? window_size : RUDP_WINDOW_SIZE;
    w->expected_seq = 0;
    if (pthread_mutex_init(&w->lock, NULL) != 0) return -1;
    if (pthread_cond_init(&w->data_avail, NULL) != 0) {
        pthread_mutex_destroy(&w->lock);
        return -1;
    }
    return 0;
}

void recv_window_destroy(rudp_recv_window_t *w) {
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->data_avail);
}

int recv_window_insert(rudp_recv_window_t *w, const rudp_packet_t *pkt,
                       size_t payload_len, const struct sockaddr_in *from) {
    pthread_mutex_lock(&w->lock);

    uint32_t seq = ntohl(pkt->hdr.seq);

    /* Bootstrap: on very first data packet, anchor the window to sender's seq */
    if (w->expected_seq == 0) {
        w->expected_seq = seq;
    }

    /* Discard duplicates and packets outside window */
    if (seq < w->expected_seq) {
        pthread_mutex_unlock(&w->lock);
        return -1; /* duplicate */
    }

    uint32_t offset = seq - w->expected_seq;
    if (offset >= w->window_size) {
        pthread_mutex_unlock(&w->lock);
        return -1; /* outside window */
    }

    uint32_t slot = offset % w->window_size;
    if (!w->entries[slot].in_use) {
        memcpy(&w->entries[slot].pkt, pkt, sizeof(rudp_packet_t));
        w->entries[slot].payload_len = payload_len;
        w->entries[slot].in_use      = true;
        if (from) w->entries[slot].from = *from;
    }

    /* Signal if the expected slot is now filled */
    if (offset == 0)
        pthread_cond_signal(&w->data_avail);

    pthread_mutex_unlock(&w->lock);
    return 0;
}

int recv_window_get(rudp_recv_window_t *w, rudp_packet_t *out,
                    size_t *payload_len, struct sockaddr_in *from) {
    pthread_mutex_lock(&w->lock);

    uint32_t slot = 0; /* expected_seq always maps to slot 0 relative */
    /* Wait for in-order packet */
    while (!w->entries[slot].in_use)
        pthread_cond_wait(&w->data_avail, &w->lock);

    rudp_recv_entry_t *e = &w->entries[slot];
    if (out)         memcpy(out, &e->pkt, sizeof(rudp_packet_t));
    if (payload_len) *payload_len = e->payload_len;
    if (from)        *from = e->from;

    e->in_use = false;
    w->expected_seq++;

    /* Shift window: bring slot 1 → 0, etc. */
    for (uint32_t i = 0; i + 1 < w->window_size; i++)
        w->entries[i] = w->entries[i + 1];
    memset(&w->entries[w->window_size - 1], 0, sizeof(rudp_recv_entry_t));

    /* Wake up again if more consecutive packets are buffered */
    if (w->entries[0].in_use)
        pthread_cond_signal(&w->data_avail);

    pthread_mutex_unlock(&w->lock);
    return 0;
}

bool recv_window_has_data(const rudp_recv_window_t *w) {
    return w->entries[0].in_use;
}

uint32_t recv_window_get_cum_ack(rudp_recv_window_t *w) {
    pthread_mutex_lock(&w->lock);
    if (w->expected_seq == 0) {
        pthread_mutex_unlock(&w->lock);
        return 0;
    }
    uint32_t highest = w->expected_seq - 1;
    for (uint32_t i = 0; i < w->window_size; i++) {
        if (w->entries[i].in_use) highest++;
        else break;
    }
    pthread_mutex_unlock(&w->lock);
    return highest;
}
