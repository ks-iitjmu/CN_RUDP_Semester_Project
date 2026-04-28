# Custom Reliable UDP (RUDP) Protocol for P2P Multiplayer Gaming
### Group 3: Dhruv · Krishiv · Piyush · Vivek · Kunal
### Network Computing Semester Project

---

## 📌 Overview

This project aims to build a decentralized, serverless multiplayer game engine by developing a custom **Reliable UDP (RUDP) protocol**. Since TCP is too slow for real-time games and standard UDP drops critical state updates, our custom transport layer guarantees the delivery of important game events without needing a central host.

### Key Features:
- **P2P Game Client**: Developed using Raylib and C++, featuring a peer-to-peer ring topology.
- **Custom Transport Protocol (RUDP)**: Developed from scratch over raw UDP, featuring a 16-byte header, sliding windows with Go-Back-N/cumulative ACKs, Jacobson/Karels RTT estimators, and NAT traversal.
- **Benchmarking Suite**: Tools to benchmark and compare RUDP's latency and reliability against standard TCP and UDP under simulated network packet loss.
- **Wireshark Integration**: Custom Lua dissector to demonstrate packet formats live in Wireshark.

---

## 🎮 Game Engine (Raylib P2P)

![Video Demo](https://github.com/user-attachments/assets/48955388-5d4a-48fe-a90a-6ec8a568e057)

### Compilation Command:
```sh
g++ game.cpp -o game.out -lraylib -pthread -std=c++17
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
./game.out
```

### Dependencies:
- `raylib`
- `pthread`
- `linux-kernel >= 6.1x.xx` (For Windows, replace `arpa/inet` with `winsock`)

### P2P Ring Topology Architecture:
1. **Iterative DNS-inspired Discovery**:
   - Host is requested.
   - If the host's `next` pointer is empty, you become its `next` pointer.
   - If occupied, the request is redirected further down the chain.
   - The chain connects back to the head to form a complete Peer-to-Peer Ring (🍐2🍐).
2. **Game Data Exchange**:
   - All peers communicate with their `.next` peer.
   - Scores are broadcasted down the network chain until resolving back to the origin.

User can switch protocols via the Menu:
- `TCP` -> Standard reliable but high overhead.
- `UDP` -> Unreliable default.
- `Custom (RUDP)` -> Our custom protocol utilizing Go-Back-N (Window Size: 10,000).

---

## 🛠 Complete Transport Layer Stack (C)

While the game client integrates custom `rudp.hpp`, we have also provided a fully decoupled robust implementation in the `/rudp` and `/transport` directories.

### RUDP Packet Format

```
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Ver  |        Flags          |         Payload Length        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Sequence Number                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Acknowledgment Number                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            Checksum           |           Reserved            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Payload ...                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
**Header = 16 bytes.** Flags: `SYN ACK FIN RST PING PONG NAT DATA`

### Detailed Technical Specifications
For full details on the State Machine, RTT Estimation, Flow Control, and Checksum calculations, please refer to [PROTOCOL.md](./PROTOCOL.md).

---

## 📈 Benchmarking and Performance Evaluation

We provide a complete suite of standard C tools to evaluate throughput, latency, and reliability in simulated lossy environments.

```bash
# 1. Start packet loss simulation (Using Linux tc netem)
sudo bash tools/tc_netem.sh setup 20   # 20% Packet Loss

# 2. Run Receiver and Sender (Using the Makefile to build fully decoupled tools)
make benchmark
./bench_receiver --proto rudp --port 9001 --count 1000 &
./bench_sender --proto rudp --host 127.0.0.1 --port 9001 --count 1000 --size 512

# 3. Generate Evaluation Report
python3 benchmark/bench_report.py --proto RUDP --size 512

# 4. Remove rules
sudo bash tools/tc_netem.sh teardown
```

### Protocol Comparison Highlights
| Feature | Standard UDP | TCP | RUDP |
|---------|-------------|-----|------|
| Header size | 8 bytes | 20 bytes | **16 bytes** |
| Reliability | None | ACK + retransmit | **ACK + retransmit** |
| Ordering | None | Strict In-Order | **Sliding window** |
| RTT estimation | None | Standard | **Jacobson/Karels** |
| Congestion Control| None | Window-based Slow Start| **Window-based Flow Control** |
| Head-of-line blocking | No | **Yes** | **No** |

---

## 🔍 Wireshark Live Demo

To easily demonstrate to the evaluator that our Protocol is actively on the wire, check out [WIRESHARK.md](./WIRESHARK.md) for custom display filters and the Lua Dissector code.

Quick Wireshark Display Filter:
```wireshark
udp.port == 9000 || udp.port == 9001
```

> **Note for maintainers**: The Game UI is tightly coupled to the P2P networking state machine. Do not modify component screen transitions without understanding the full peer discovery handshake flow.
