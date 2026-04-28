#!/usr/bin/env python3
"""
bench_report.py — RUDP Benchmark Report Generator
==================================================
Reads sender.log and receiver.log, computes:
  - Packet loss %
  - One-way latency: avg, median, P95, P99, min, max
  - Throughput (bytes/sec)

Usage:
  python3 bench_report.py [--sender sender.log] [--receiver receiver.log]
                          [--size <bytes>] [--output report.csv]

Run this after bench_sender + bench_receiver on the same machine.
"""

import sys
import csv
import math
import argparse
import os

def parse_log(path):
    """Returns dict: seq -> timestamp_us"""
    data = {}
    with open(path) as f:
        reader = csv.DictReader(f, delimiter='\t')
        for row in reader:
            data[int(row['seq'])] = int(row[row.keys().__iter__().__next__()
                                          if 'send_us' not in row else 'send_us']
                                       if 'send_us' in row
                                       else list(row.values())[1])
    # Re-read properly
    data = {}
    with open(path) as f:
        next(f)  # skip header
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split('\t')
            if len(parts) >= 2:
                data[int(parts[0])] = int(parts[1])
    return data


def percentile(sorted_vals, p):
    if not sorted_vals:
        return float('nan')
    idx = int(math.ceil(p / 100.0 * len(sorted_vals))) - 1
    idx = max(0, min(idx, len(sorted_vals) - 1))
    return sorted_vals[idx]


def print_table(rows, headers):
    widths = [max(len(h), max((len(str(r[i])) for r in rows), default=0))
              for i, h in enumerate(headers)]
    sep = '+' + '+'.join('-' * (w + 2) for w in widths) + '+'
    fmt = '|' + '|'.join(' {:<' + str(w) + '} ' for w in widths) + '|'
    print(sep)
    print(fmt.format(*headers))
    print(sep)
    for row in rows:
        print(fmt.format(*[str(x) for x in row]))
    print(sep)


def main():
    parser = argparse.ArgumentParser(description='RUDP Benchmark Report')
    parser.add_argument('--sender',   default='sender.log')
    parser.add_argument('--receiver', default='receiver.log')
    parser.add_argument('--size',     type=int, default=512,
                        help='Payload size in bytes per packet')
    parser.add_argument('--proto',    default='RUDP',
                        help='Protocol label for report')
    parser.add_argument('--output',   default='report.csv')
    args = parser.parse_args()

    if not os.path.exists(args.sender):
        print(f"[Error] {args.sender} not found.")
        sys.exit(1)
    if not os.path.exists(args.receiver):
        print(f"[Error] {args.receiver} not found.")
        sys.exit(1)

    sent = parse_log(args.sender)
    recv = parse_log(args.receiver)

    total_sent = len(sent)
    total_recv = len(recv)
    lost       = total_sent - total_recv
    loss_pct   = 100.0 * lost / total_sent if total_sent else 0.0

    # One-way latency for matched packets
    latencies = []
    for seq, send_us in sent.items():
        if seq in recv:
            lat = recv[seq] - send_us  # microseconds
            if lat >= 0:  # ignore clock skew artifacts
                latencies.append(lat)

    latencies.sort()
    avg_us  = sum(latencies) / len(latencies) if latencies else float('nan')
    med_us  = percentile(latencies, 50)
    p95_us  = percentile(latencies, 95)
    p99_us  = percentile(latencies, 99)
    min_us  = latencies[0]  if latencies else float('nan')
    max_us  = latencies[-1] if latencies else float('nan')

    # Throughput: based on recv timestamps
    if len(recv) >= 2:
        recv_times = sorted(recv.values())
        duration_s = (recv_times[-1] - recv_times[0]) / 1e6
        throughput_bps = (total_recv * args.size * 8) / duration_s if duration_s > 0 else 0
        throughput_kbps = throughput_bps / 1024
    else:
        throughput_kbps = float('nan')

    # ─── Print Report ───────────────────────────────────────────
    print()
    print('=' * 54)
    print(f'  RUDP Benchmark Report  [{args.proto}]')
    print('=' * 54)
    print(f'  Packets Sent:       {total_sent}')
    print(f'  Packets Received:   {total_recv}')
    print(f'  Lost:               {lost}  ({loss_pct:.2f}%)')
    print()
    print('  One-Way Latency (µs):')
    print(f'    Min:     {min_us}')
    print(f'    Average: {avg_us:.1f}')
    print(f'    Median:  {med_us}')
    print(f'    P95:     {p95_us}')
    print(f'    P99:     {p99_us}')
    print(f'    Max:     {max_us}')
    print()
    print(f'  Throughput:  {throughput_kbps:.2f} KB/s  ({throughput_kbps/8:.2f} KBps raw)')
    print('=' * 54)

    # ─── Write CSV ───────────────────────────────────────────────
    with open(args.output, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['protocol', 'sent', 'received', 'lost', 'loss_pct',
                         'lat_avg_us', 'lat_med_us', 'lat_p95_us', 'lat_p99_us',
                         'lat_min_us', 'lat_max_us', 'throughput_kbps'])
        writer.writerow([args.proto, total_sent, total_recv, lost,
                         f'{loss_pct:.2f}',
                         f'{avg_us:.1f}', med_us, p95_us, p99_us,
                         min_us, max_us, f'{throughput_kbps:.2f}'])

    print(f'\n  Results saved to: {args.output}')
    print()


if __name__ == '__main__':
    main()
