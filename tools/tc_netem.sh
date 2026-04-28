#!/usr/bin/env bash
# tools/tc_netem.sh — Simulate packet loss using Linux tc netem
# ==============================================================
# Adds/removes packet loss on the loopback interface (lo).
# This is the correct way to stress-test RUDP without needing
# two physical machines.
#
# Usage:
#   sudo bash tools/tc_netem.sh setup <loss_pct> [delay_ms] [interface]
#   sudo bash tools/tc_netem.sh teardown [interface]
#   sudo bash tools/tc_netem.sh status [interface]
#
# Examples:
#   sudo bash tools/tc_netem.sh setup 10          # 10% loss on lo
#   sudo bash tools/tc_netem.sh setup 20 50       # 20% loss + 50ms delay on lo
#   sudo bash tools/tc_netem.sh setup 5 10 eth0   # 5% loss on eth0
#   sudo bash tools/tc_netem.sh teardown
#   sudo bash tools/tc_netem.sh status

set -euo pipefail

CMD="${1:-help}"
LOSS="${2:-10}"
DELAY="${3:-0}"
IFACE="${4:-lo}"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}[Error] This script must be run as root (sudo).${NC}" >&2
        exit 1
    fi
}

cmd_setup() {
    check_root
    echo -e "${YELLOW}[tc netem] Setting up on interface: ${IFACE}${NC}"

    # Remove existing qdisc (ignore error if none)
    tc qdisc del dev "$IFACE" root 2>/dev/null || true

    if [[ "$DELAY" -gt 0 ]]; then
        tc qdisc add dev "$IFACE" root netem loss "${LOSS}%" delay "${DELAY}ms"
        echo -e "${GREEN}[tc netem] Applied: ${LOSS}% packet loss + ${DELAY}ms delay on ${IFACE}${NC}"
    else
        tc qdisc add dev "$IFACE" root netem loss "${LOSS}%"
        echo -e "${GREEN}[tc netem] Applied: ${LOSS}% packet loss on ${IFACE}${NC}"
    fi

    echo ""
    echo "  Run your benchmark now. To remove, run:"
    echo "    sudo bash tools/tc_netem.sh teardown ${IFACE}"
    echo ""
}

cmd_teardown() {
    check_root
    if tc qdisc del dev "$IFACE" root 2>/dev/null; then
        echo -e "${GREEN}[tc netem] Removed netem rules from ${IFACE}${NC}"
    else
        echo -e "${YELLOW}[tc netem] No netem rules were active on ${IFACE}${NC}"
    fi
}

cmd_status() {
    echo -e "${YELLOW}[tc netem] Current qdisc on ${IFACE}:${NC}"
    tc qdisc show dev "$IFACE" || echo "  (none)"
}

cmd_help() {
    grep '^#' "$0" | head -20 | sed 's/^# \?//'
}

# ─── Full Benchmark Run Helper ────────────────────────────────────
cmd_run_benchmark() {
    # Convenience: runs sender+receiver+report for all three protocols
    # at given loss level. Requires benchmark binaries to be compiled.
    LOCAL_LOSS="${2:-10}"
    PORT=9001
    COUNT=1000
    SIZE=512

    for PROTO in udp rudp tcp; do
        echo -e "${YELLOW}=== Benchmarking ${PROTO^^} at ${LOCAL_LOSS}% loss ===${NC}"
        sudo bash "$0" setup "$LOCAL_LOSS" 0 lo

        ./benchmark/bench_receiver --proto "$PROTO" --port "$PORT" \
            --count "$COUNT" &
        RECV_PID=$!
        sleep 0.5

        ./benchmark/bench_sender --proto "$PROTO" --host 127.0.0.1 \
            --port "$PORT" --count "$COUNT" --size "$SIZE"

        wait $RECV_PID 2>/dev/null || true
        sudo bash "$0" teardown lo

        mkdir -p results
        mv sender.log   "results/${PROTO}_loss${LOCAL_LOSS}_sender.log"
        mv receiver.log "results/${PROTO}_loss${LOCAL_LOSS}_receiver.log"

        python3 benchmark/bench_report.py \
            --sender   "results/${PROTO}_loss${LOCAL_LOSS}_sender.log" \
            --receiver "results/${PROTO}_loss${LOCAL_LOSS}_receiver.log" \
            --proto    "${PROTO^^}" \
            --size     "$SIZE" \
            --output   "results/${PROTO}_loss${LOCAL_LOSS}_report.csv"
    done

    echo -e "${GREEN}=== All benchmark results in ./results/ ===${NC}"
}

case "$CMD" in
    setup)          cmd_setup      ;;
    teardown)       cmd_teardown   ;;
    status)         cmd_status     ;;
    run_benchmark)  cmd_run_benchmark "$@" ;;
    help|--help|-h) cmd_help       ;;
    *)
        echo "Unknown command: $CMD"
        cmd_help
        exit 1
        ;;
esac
