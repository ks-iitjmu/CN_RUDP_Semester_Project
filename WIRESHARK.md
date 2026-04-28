# WIRESHARK.md — Capturing and Analyzing RUDP Packets

## 1. Quick Setup

### Start Wireshark

```bash
sudo wireshark &
```

Select interface: **`lo`** (loopback) for local tests, or **`wlan0` / `eth0`** for LAN/hotspot.

---

## 2. Display Filters

> **Note:** The P2P game uses **dynamic (ephemeral) ports**. Run `./game.out` and note
> the `My Port: XXXXX` printed in the terminal — use that port number in the filter below.
> The benchmarking tools use fixed port **9001**.

### Show all RUDP game traffic (replace PORT with actual port)

```
udp.port == <YOUR_GAME_PORT>
```

### Show both game + benchmark traffic

```
udp.port == <YOUR_GAME_PORT> || udp.port == 9001
```

### Show RUDP by payload length (≥ 17 bytes = 16-byte header + 1 byte payload)

```
udp.port == <YOUR_GAME_PORT> && udp.length >= 25
```

### Capture only during benchmark (fixed port)

```
udp.port == 9001 && udp.length > 16
```

---

## 3. Identifying RUDP Header Fields in Wireshark

After selecting a UDP packet, expand the **Data** section in the packet details pane. Our 16-byte header is in the UDP payload:

```
Offset  Field                   Example value
------  ------                  -------------
 0      Version (4b) + Flags_H  0x18 → version=1, flags_hi=0x8 → DATA
 1      Flags_L                 0x02 → ACK
 2–3    Payload Length          0x0200 → 512 bytes
 4–7    Sequence Number         0x00000001 → seq=1
 8–11   ACK Number              0x00000000 → ack=0
12–13   Checksum                0xA4B1
14–15   Reserved                0x0000
16+     Payload
```

### Example: DATA|ACK packet (normal data transfer)

```
0000: 18 02 02 00   ← version=1, flags=DATA|ACK, len=512
0004: 00 00 00 05   ← seq=5
0008: 00 00 00 04   ← ack=4
000C: A4 B1 00 00   ← checksum, reserved
0010: ...           ← payload starts here
```

### Example: SYN packet (handshake initiation)

```
0000: 10 01 00 00   ← version=1, flags=SYN, len=0
0004: 00 00 00 01   ← seq=1
0008: 00 00 00 00   ← ack=0
000C: xx xx 00 00   ← checksum, reserved
```

### Example: ACK-only packet

```
0000: 10 02 00 00   ← version=1, flags=ACK, len=0
0004: 00 00 00 00   ← seq=0 (ACK-only)
0008: 00 00 00 05   ← ack=5 (cumulative)
```

---

## 4. Lua Dissector for Wireshark

Save this as `~/.config/wireshark/plugins/rudp.lua` (or wherever Wireshark loads plugins):

```lua
-- rudp.lua — Wireshark Lua dissector for RUDP
-- Load via: Edit > Preferences > Protocols > DissectorTable or auto-load

local rudp_proto = Proto("RUDP", "Reliable UDP (RUDP)")

-- Field definitions
local f_version  = ProtoField.uint8  ("rudp.version",  "Version",          base.DEC)
local f_flags    = ProtoField.uint8  ("rudp.flags",    "Flags",            base.HEX)
local f_syn      = ProtoField.bool   ("rudp.flags.syn","SYN",              8, nil, 0x01)
local f_ack      = ProtoField.bool   ("rudp.flags.ack","ACK",              8, nil, 0x02)
local f_fin      = ProtoField.bool   ("rudp.flags.fin","FIN",              8, nil, 0x04)
local f_rst      = ProtoField.bool   ("rudp.flags.rst","RST",              8, nil, 0x08)
local f_ping     = ProtoField.bool   ("rudp.flags.ping","PING",            8, nil, 0x10)
local f_pong     = ProtoField.bool   ("rudp.flags.pong","PONG",            8, nil, 0x20)
local f_nat      = ProtoField.bool   ("rudp.flags.nat","NAT",              8, nil, 0x40)
local f_data     = ProtoField.bool   ("rudp.flags.data","DATA",            8, nil, 0x80)
local f_len      = ProtoField.uint16 ("rudp.len",      "Payload Length",   base.DEC)
local f_seq      = ProtoField.uint32 ("rudp.seq",      "Sequence Number",  base.DEC)
local f_ack_num  = ProtoField.uint32 ("rudp.ack",      "ACK Number",       base.DEC)
local f_checksum = ProtoField.uint16 ("rudp.checksum", "Checksum",         base.HEX)
local f_payload  = ProtoField.bytes  ("rudp.payload",  "Payload")

rudp_proto.fields = {
    f_version, f_flags, f_syn, f_ack, f_fin, f_rst,
    f_ping, f_pong, f_nat, f_data,
    f_len, f_seq, f_ack_num, f_checksum, f_payload
}

function rudp_proto.dissector(buffer, pinfo, tree)
    if buffer:len() < 16 then return end

    pinfo.cols.protocol = "RUDP"

    local subtree = tree:add(rudp_proto, buffer(), "RUDP Protocol")

    -- Byte 0: version (high nibble) + flags_hi (low nibble)
    local b0    = buffer(0, 1):uint()
    local ver   = bit.rshift(bit.band(b0, 0xF0), 4)
    local fhi   = bit.band(b0, 0x0F)
    local b1    = buffer(1, 1):uint()
    local flags = bit.bor(bit.lshift(fhi, 4), bit.band(b1, 0x0F))

    subtree:add(f_version,  buffer(0, 1), ver)

    local flag_tree = subtree:add(f_flags, buffer(0, 2), flags)
    flag_tree:add(f_syn,  buffer(0, 2), flags)
    flag_tree:add(f_ack,  buffer(0, 2), flags)
    flag_tree:add(f_fin,  buffer(0, 2), flags)
    flag_tree:add(f_rst,  buffer(0, 2), flags)
    flag_tree:add(f_ping, buffer(0, 2), flags)
    flag_tree:add(f_pong, buffer(0, 2), flags)
    flag_tree:add(f_nat,  buffer(0, 2), flags)
    flag_tree:add(f_data, buffer(0, 2), flags)

    subtree:add(f_len,      buffer(2,  2))
    subtree:add(f_seq,      buffer(4,  4))
    subtree:add(f_ack_num,  buffer(8,  4))
    subtree:add(f_checksum, buffer(12, 2))

    local pay_len = buffer(2, 2):uint()
    if pay_len > 0 and buffer:len() >= 16 + pay_len then
        subtree:add(f_payload, buffer(16, pay_len))
    end

    -- Column info
    local flag_str = ""
    if bit.band(flags, 0x01) ~= 0 then flag_str = flag_str .. "SYN " end
    if bit.band(flags, 0x02) ~= 0 then flag_str = flag_str .. "ACK " end
    if bit.band(flags, 0x04) ~= 0 then flag_str = flag_str .. "FIN " end
    if bit.band(flags, 0x80) ~= 0 then flag_str = flag_str .. "DATA " end
    if bit.band(flags, 0x40) ~= 0 then flag_str = flag_str .. "NAT " end

    pinfo.cols.info = string.format(
        "RUDP [%s] seq=%u ack=%u len=%u",
        flag_str,
        buffer(4, 4):uint(),
        buffer(8, 4):uint(),
        pay_len)
end

-- Register on common game/test ports
local udp_table = DissectorTable.get("udp.port")
udp_table:add(9000, rudp_proto)
udp_table:add(9001, rudp_proto)
udp_table:add(9500, rudp_proto)
udp_table:add(9999, rudp_proto)
```

### Loading the Plugin

1. Copy the file: `~/.config/wireshark/plugins/rudp.lua`
2. In Wireshark: **Analyze → Reload Lua Plugins** (or restart Wireshark)
3. Packets on port 9000/9001/9500/9999 will now show as **RUDP** with decoded fields

---

## 5. Capture Commands (Terminal)

### Capture to file for offline analysis

```bash
# Capture game traffic to pcap
sudo tcpdump -i lo -w rudp_capture.pcap udp port 9000

# Open in Wireshark
wireshark rudp_capture.pcap
```

### Quick field dump (no GUI)

```bash
# Show raw hex of UDP payloads (our RUDP packets)
sudo tcpdump -i lo -X -n udp port 9000
```

---

## 6. What to Show in the Demo

1. **Open Wireshark** on loopback interface (`lo`), filter `udp.port == <YOUR_GAME_PORT>`
2. **Start first game** (`./game.out`) → note the port from `My Port: XXXXX` in terminal
   → update Wireshark filter to that port number
3. In Wireshark, observe:
   - SYN / SYN-ACK three-way-ish handshake packets
   - DATA|ACK packets carrying game positions
   - Retransmissions visible (same seq number sent twice) when you add packet loss
4. **Apply the Lua dissector** → fields decode cleanly in the packet tree
5. Start a benchmark under `tc netem` 20% loss → show retransmissions in Wireshark

---

## 7. Example Filter Cheat Sheet

| Goal | Filter |
|---|---|
| All RUDP game traffic | `udp.port == 9000` |
| Only SYN packets | `udp.port == 9000 && udp.payload[1] == 0x01` |
| Only DATA packets | `udp.port == 9000 && udp.payload[1] == 0x02` |
| Benchmark traffic | `udp.port == 9001` |
| Rendezvous server | `udp.port == 9999` |
| Large payloads (>512B) | `udp.length > 528` |
