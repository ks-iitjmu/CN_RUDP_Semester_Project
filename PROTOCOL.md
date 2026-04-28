# PROTOCOL.md — RUDP Protocol Specification

## 1. Overview

RUDP (Reliable UDP) is a **custom Layer 4 transport protocol** that runs over UDP datagrams. It provides TCP-like reliability without TCP's connection overhead and head-of-line blocking.

---

## 2. Packet (Segment) Format

### Wire Layout

```
  Byte  0          1          2          3
        ┌──────────┬──────────┬──────────┬──────────┐
  0–3   │ Ver│Flg_H│ Flg_L    │       Payload Len    │
        ├──────────┴──────────┴──────────┴──────────┤
  4–7   │                Sequence Number             │
        ├────────────────────────────────────────────┤
  8–11  │              Acknowledgment Number         │
        ├──────────┬──────────┬──────────┬──────────┤
 12–15  │       Checksum      │        Reserved       │
        ├──────────┴──────────┴──────────┴──────────┤
 16+    │                Payload (0–1400 bytes)      │
        └────────────────────────────────────────────┘
```

**Total header = 16 bytes** (vs 8 bytes for UDP, 20 bytes for TCP).

### Field Descriptions

| Field | Bits | Description |
|---|---|---|
| Version | 4 | Protocol version. Currently = 1 |
| Flags | 8 | See flag table below |
| Payload Len | 16 | Length of payload in bytes (0–1400) |
| Sequence # | 32 | Monotonically increasing sender-side counter |
| ACK # | 32 | Cumulative ACK: all seq ≤ ACK# received |
| Checksum | 16 | Internet checksum (RFC 1071) over entire packet |
| Reserved | 16 | Set to 0; future use |

### Flags (8-bit field)

| Bit | Flag | Meaning |
|---|---|---|
| 7 | `DATA` | Packet carries user payload |
| 6 | `NAT` | NAT hole-punch probe |
| 5 | `PONG` | Keep-alive reply |
| 4 | `PING` | Keep-alive request |
| 3 | `RST` | Abort connection |
| 2 | `FIN` | Graceful teardown |
| 1 | `ACK` | Acknowledgment field is valid |
| 0 | `SYN` | Connection initiation |

---

## 3. Changes vs Standard UDP

| Aspect | UDP | RUDP |
|---|---|---|
| Header | 8 bytes | **16 bytes** (+8 bytes overhead) |
| Reliability | None | ACK + retransmit |
| Ordering | None | Sliding-window reorder buffer |
| Flow control | None | Window size limit (10,000 pkts) |
| RTT | No measurement | **Jacobson/Karels SRTT + RTTVAR** |
| Loss detection | None | Per-packet expiry timer |
| Connection | Stateless | **SYN → SYN/ACK → Established** |
| Teardown | None | **FIN → FIN/ACK** |
| NAT | Not supported | **UDP Hole Punching** |
| Broadcast | Works | Unicast only (reliable semantics) |

---

## 4. Connection State Machine

```
  CLIENT                             SERVER
    │                                  │
    │──── SYN (seq=x) ────────────────►│
    │                                  │  (passive open: rudp_accept)
    │◄─── SYN|ACK (seq=y, ack=x) ─────│
    │                                  │
    │  [ESTABLISHED]          [ESTABLISHED]
    │                                  │
    │──── DATA|ACK (seq=x+1) ─────────►│
    │◄─── ACK (ack=x+1) ──────────────│
    │                                  │
    │  ... more data exchange ...      │
    │                                  │
    │──── FIN ────────────────────────►│
    │◄─── FIN|ACK ────────────────────│
    │                                  │
    │  [CLOSED]               [CLOSED] │
```

---

## 5. Reliable Delivery Mechanism

### Sender Side

1. Assign `seq` number from an atomic counter.
2. Store packet in **send window** (circular buffer, max 16 slots).
3. Transmit via raw UDP `sendto()`.
4. Start retransmit timer (initial = RTO from RTT estimator).
5. **If ACK arrives** before timeout → remove from window, slide base.
6. **If timeout** → retransmit; increment retry counter.
7. After `max_retries` (default 10) → error out.

### Receiver Side

1. Validate checksum; drop on mismatch.
2. Insert packet into **receive window** (reorder buffer).
3. In-order packets delivered to application immediately.
4. Out-of-order packets buffered until gap fills.
5. Send **cumulative ACK** for highest in-order seq received.

---

## 6. RTT Estimation (Jacobson/Karels)

```
RTTVAR = (1 - β)·RTTVAR + β·|SRTT - R|
SRTT   = (1 - α)·SRTT   + α·R
RTO    = SRTT + 4·RTTVAR

where:  α = 0.125,  β = 0.25,  R = measured RTT
        RTO clamped: [50ms, 6000ms]
```

This adapts the retransmission timeout to actual network conditions, reducing unnecessary retransmits on stable networks and recovering faster under congestion.

---

## 7. NAT Traversal (UDP Hole Punching)

```
  Client A (NAT)        Rendezvous Server        Client B (NAT)
       │                       │                      │
       │── REGISTER A room1 ──►│                      │
       │◄── WAIT ──────────────│                      │
       │                       │◄── REGISTER B room1 ─│
       │◄── PEER B_pub:port ───│                      │
       │                       │── PEER A_pub:port ──►│
       │                       │                      │
       │──── NAT probe ─────────────────────────────►│
       │◄─── NAT probe ──────────────────────────────│
       │                       │                      │
       │  [Bidirectional path open — normal RUDP flows]
```

On a **LAN / hotspot**, the rendezvous step is skipped — peers connect directly via local IP.

---

## 8. Performance Comparison

*(Expected results under 20% simulated packet loss — your actual benchmark numbers will fill this table)*

| Metric | UDP | TCP | RUDP |
|---|---|---|---|
| Control overhead | 8 B/pkt | 20 B/pkt | **16 B/pkt** |
| Latency (avg) | ~0.2ms | ~2–5ms | **~0.5ms** |
| Packet loss at app | 20% | 0% (but stalls) | **0%** |
| Retransmits | 0 | OS-managed | App-managed |
| Head-of-line blocking | No | **Yes** | **No** |
| Throughput under loss | Drops 20% | Slow (congestion ctrl) | Near-100% |

> Run `bench_report.py` after your benchmarks to fill in real numbers.

---

## 9. Checksum Algorithm

RUDP uses the **Internet Checksum (RFC 1071)** over the entire packet (header + payload):

```c
sum = 0
for each 16-bit word w in packet:
    sum += w
sum = (sum >> 16) + (sum & 0xFFFF)
checksum = ~sum
```

On receipt, re-compute over the full packet (with checksum field set to 0); result must be 0xFFFF to be valid.
