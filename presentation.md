# Custom Reliable UDP (RUDP) Protocol for P2P Multiplayer Gaming
### Network Computing — Semester Project Presentation
**Group 3:** Dhruv · Krishiv · Piyush · Vivek · Kunal

---

## Slide 1: Title

# Custom Reliable UDP (RUDP) Protocol
## for Peer-to-Peer Multiplayer Gaming

> "Building a custom transport protocol from scratch to achieve reliable, low-latency game state synchronization over a serverless P2P ring topology."

**Network Computing — Semester Project**
Group 3: Dhruv · Krishiv · Piyush · Vivek · Kunal

---

## Slide 2: Problem Statement

# Why Not Just Use TCP or UDP?

**Standard UDP:**
- Fast, but zero reliability — packets drop silently.
- 1.0% packet loss at 1M packets = 9,694 missed game states.
- Player positions glitch, rubber-band, and freeze.

**Standard TCP:**
- Byte-stream (not message-oriented) — Nagle's algorithm merges packets.
- Head-of-line blocking: one dropped packet stalls entire stream.
- Adds 5–10x latency overhead for game state tick updates.

**The Real-World Gap:**
> No standard transport protocol is optimized for discrete, time-sensitive game datagrams that need guaranteed delivery *without* stream-level blocking.

**Our Solution:** Build a custom Layer-4 protocol (RUDP) that takes the best of both worlds.

---

## Slide 3: Project Goals

# What We Set Out to Build

1. **Custom RUDP Protocol (C):** A fully custom Layer-4 protocol built over raw UDP with reliable delivery, sliding window, retransmission, and RTT estimation.

2. **Decentralized P2P Game Engine (C++):** A real-time multiplayer coin-collection game using a Ring Topology — no central server required.

3. **Protocol Flexibility:** Players can choose TCP, UDP, or Custom RUDP from the in-game menu for academic comparison.

4. **NAT Traversal:** UDP Hole Punching via a Rendezvous server to allow peers across different networks to connect.

5. **Benchmarking Suite:** CLI tools to measure throughput, packet loss, and latency across all three protocols under simulated network conditions.

6. **Wireshark Integration:** Custom Lua Dissector to display decoded RUDP fields live in Wireshark during demo.

---

## Slide 4: RUDP Packet Format

# Our Custom Protocol Header (16 Bytes)

```
 Byte  0          1          2          3
        +----------+----------+----------+----------+
  0-3   | Ver|Flg_H| Flags_L  |     Payload Length  |
        +----------+----------+----------+----------+
  4-7   |            Sequence Number                 |
        +--------------------------------------------+
  8-11  |          Acknowledgment Number             |
        +----------+----------+----------+----------+
 12-15  |       Checksum      |        Reserved      |
        +----------+----------+----------+----------+
 16+    |              Payload (0-1400 bytes)        |
        +--------------------------------------------+
```

**Header = 16 bytes** (vs 8B for UDP, 20B for TCP)

**Flags (8-bit):** `SYN` `ACK` `FIN` `RST` `PING` `PONG` `NAT` `DATA`

**Checksum:** Internet Checksum (RFC 1071) over full packet.

---

## Slide 5: RUDP — Reliability Mechanisms

# How We Guarantee Delivery

### Sliding Window (Go-Back-N)
- Window Size: **10,000 packets** — allows massive pipelining.
- Sender fills window and fires all packets asynchronously.
- Receiver buffers out-of-order packets, only delivers in-order to app.

### Cumulative ACK
- Receiver replies with highest contiguous sequence received.
- If packet 501 drops but 502-600 arrive, receiver keeps ACK'ing `500`.
- Sender retransmits 501, then both sides continue from 501 onwards.

### RTT Estimation (Jacobson/Karels)
```
RTTVAR = (1 - 0.25) * RTTVAR + 0.25 * |SRTT - R|
SRTT   = (1 - 0.125) * SRTT  + 0.125 * R
RTO    = SRTT + 4 * RTTVAR   [clamped: 50ms - 6000ms]
```

### Retransmission
- Background pthread scans send window every 1ms for timed-out packets.
- Max 10 retries per packet before dropping (network declared dead).

---

## Slide 6: Connection State Machine

# RUDP Handshake & Teardown

```
  CLIENT                             SERVER
    |                                  |
    |---- SYN (seq=x) --------------->|
    |                                  |  rudp_accept() waits
    |<--- SYN|ACK (seq=y, ack=x) -----|
    |                                  |
    |  [ESTABLISHED]        [ESTABLISHED]
    |                                  |
    |---- DATA|ACK ------------------>|
    |<--- ACK -----------------------|
    |                                  |
    |  ... game data exchange ...      |
    |                                  |
    |---- FIN ------------------------>|
    |<--- FIN|ACK -------------------|
    |                                  |
    |  [CLOSED]             [CLOSED]  |
```

- Two background pthreads per socket: `_recv_thread` + `_retransmit_thread`
- Thread-safe access to send/recv windows via mutex + condition variables.
- `SO_REUSEADDR` ensures instant port rebinding on restart.

---

## Slide 7: NAT Traversal — UDP Hole Punching

# Connecting Peers Across Networks

**The Problem:** Players behind NAT (home routers, mobile hotspots) cannot accept incoming connections — their local IP/port is invisible to the internet.

### Rendezvous Server Design

```
  Peer A (NAT)         Rendezvous Server         Peer B (NAT)
       |                       |                      |
       |--- REGISTER room1 --->|                      |
       |<-- WAIT ---------------|                      |
       |                       |<-- REGISTER room1 ---|
       |<-- PEER B public:port -|                      |
       |                       |- PEER A public:port ->|
       |                       |                      |
       |------ NAT probe --------------------------->|
       |<----- NAT probe ----------------------------|
       |                       |                      |
       |  [Bidirectional path open - RUDP flows]      |
```

- Both peers first "punch" their router by sending outbound UDP packets.
- NAT now has an entry: **"incoming from B is allowed"**.
- After exchange, the Rendezvous Server is no longer needed.
- **On LAN / Hotspot:** Rendezvous step is skipped — direct local IP connection.

---

## Slide 8: P2P Ring Topology Game Engine

# How the Game Network Works

### Ring Topology Design
- Players form a logical ring: `P1 -> P2 -> P3 -> P1`
- Each peer maintains only ONE outgoing and ONE incoming connection.
- Game state (position + score) travels around the ring every **50ms**.

### Peer Discovery (DNS-Inspired Handshake)
1. New peer sends `A<port>` (join request) to Host via UDP.
2. Host checks if its `next` slot is free:
   - Free: **"You are my next"** (`S` message) — peer joins.
   - Occupied: **"Ask the next node"** (`R` message) — peer redirected.
3. Ring closes when the last peer's next connects back to Host.
4. Host broadcasts `F` (final/start) message around the ring with random seed.

### Synchronized Game Start
- All peers receive the **same random seed** → same coin positions on every screen.
- Game starts simultaneously on `Enter` press by Host.

---

## Slide 9: Game Features & Technical Stack

# What We Built

### Game Engine
- **Language:** C++17 with Raylib graphics library
- **Input:** WASD + Arrow key support
- **Rendering:** Neon grid background, particle burst on coin collection
- **UI:** Live leaderboard sidebar, smooth position interpolation across peers

### Payload Format (All Protocols)
```
U{id}|{score}|{x}|{y}   →  Game state update
A{seqNum}               →  ACK (RUDP only)
F{seed}                 →  Game start signal
X{id};                  →  ID assignment (ring setup)
```

### Thread Architecture
- **RX Thread:** Receives packets, updates `peerPositions` map (mutex-protected)
- **TX Thread:** Broadcasts own position every 50ms around the ring
- **SIGPIPE ignored:** Prevents crash on peer disconnect

### Compilation
```bash
g++ game.cpp -o game.out -lraylib -pthread -std=c++17
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
./game.out
```

---

## Slide 10: Benchmarking Results

# Protocol Performance — 1,000,000 Packets at 512 Bytes

| Metric | Custom RUDP | Standard UDP | Standard TCP |
| :--- | :---: | :---: | :---: |
| **Packets Sent** | 1,000,000 | 1,000,000 | 1,000,000 |
| **Packets Received** | **1,000,000** | 990,306 | 37,671* |
| **Packet Loss** | **0.0%** | 1.0% | N/A |
| **Duplicates** | **0** | 0 | 325,509* |
| **Throughput** | ~45 MB/s | ~260 MB/s | ~900 MB/s |
| **Gaming Suitability** | **Excellent** | Poor | Poor |

*\* TCP result is due to stream merging (Nagle's Algorithm) — not actual data loss.*

### Key Takeaway
- **UDP:** Fast, but silent 1% loss makes it unsuitable for game sync.
- **TCP:** Reliable bytes, but stream-merging breaks discrete packet framing. Adds head-of-line blocking.
- **RUDP:** 0% loss with guaranteed, in-order delivery. Purpose-built for game datagrams.

---

## Slide 11: Live Demo Walkthrough

# How to Run the Full Demo

### Game Demo (2 Terminals — Same Laptop or Same Network)
```bash
# Terminal 1 (Host)
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
./game.out
# → Select Protocol → Start New Peer Group → Note the Port

# Terminal 2 (Client)
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
./game.out
# → Same Protocol → Join a Peer Group → Enter 127.0.0.1:PORT
```

### Benchmark Demo
```bash
# Terminal 1 (Receiver)
./bench_receiver --proto rudp --port 8002 --count 1000000

# Terminal 2 (Sender)
./bench_sender --proto rudp --host 127.0.0.1 --port 8002 --count 1000000 --size 512
```

### Wireshark Live Demo
```bash
sudo wireshark &
# Interface: lo (loopback)
# Filter: udp.port == <YOUR_GAME_PORT>
# Load plugin: ~/.config/wireshark/plugins/rudp.lua
# Reload: Analyze → Reload Lua Plugins
```

### Simulate Packet Loss (tc netem)
```bash
sudo bash tools/tc_netem.sh setup 20   # 20% loss
# Run benchmark → Show RUDP retransmissions in Wireshark
sudo bash tools/tc_netem.sh teardown
```

---

## Slide 12: Conclusion & Learnings

# Summary & Key Takeaways

### What We Achieved
- Built a **fully custom Layer-4 transport protocol (RUDP)** from scratch in C, implementing sliding windows, cumulative ACKs, Jacobson/Karels RTT estimation, and UDP hole punching.
- Developed a **decentralized P2P multiplayer game** in C++ with real-time position sync and dynamic peer discovery — no central game server.
- Demonstrated **0% packet loss** at 1 million packets vs 1% for raw UDP.
- Built complete **benchmarking tools**, **Wireshark Lua dissector**, and **NAT traversal utilities** for academic evaluation.

### Key Learnings
1. **TCP is not always best** — for message-oriented game sync, datagram protocols with custom reliability are superior.
2. **Concurrency is hard** — mutex-protected shared state (`peerPositions`, `scores`) is critical for race-condition-free gameplay.
3. **NAT is a real barrier** — UDP Hole Punching with a Rendezvous server is the industry-standard solution.
4. **SIGPIPE kills processes** — P2P apps must always handle peer disconnection gracefully.

### Future Scope
- QUIC-based implementation for browser-compatible P2P gaming
- Extend ring topology to support dynamic peer join/leave mid-game
- Congestion window scaling (slow-start + AIMD) for internet-scale deployments

---

*End of Presentation — Thank You*
*Group 3: Dhruv · Krishiv · Piyush · Vivek · Kunal*
