# replay_simulator.py
import argparse
import json
import socket
import time
import itertools
from typing import List, Any

def load_data(path: str) -> List[Any]:
    with open(path, "r", encoding="utf-8") as f:
        obj = json.load(f)
    if isinstance(obj, list):
        return obj
    return [obj]

def make_payload(item):
    # You can customize payload structure here.
    # For simplicity: send the raw snapshot/update as JSON string.
    return json.dumps(item, separators=(",", ":"), ensure_ascii=False).encode("utf-8")

def run_loop(data, target_ip, target_port, rate=None, interval_ms=None, burst=1, loops=None, verbose=False):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    idx_iter = itertools.cycle(range(len(data)))
    sent = 0
    start_time = time.perf_counter()
    if rate and rate > 0:
        interval = 1.0 / rate
    elif interval_ms is not None:
        interval = interval_ms / 1000.0
    else:
        interval = 0.1  # default 100 ms

    try:
        while True:
            if loops is not None and sent >= loops:
                if verbose: print("Finished requested loops:", loops)
                break
            i = next(idx_iter)
            payload = make_payload(data[i])
            for b in range(burst):
                sock.sendto(payload, (target_ip, target_port))
                sent += 1
            if verbose and sent % 100 == 0:
                print(f"Sent {sent} packets so far...")
            # sleep precisely: use sleep for ms and small busy wait for better precision
            if interval >= 0.005:
                time.sleep(interval)
            else:
                # for very small intervals (<5ms) try busy-wait loop to increase precision
                deadline = time.perf_counter() + interval
                while time.perf_counter() < deadline:
                    pass
    except KeyboardInterrupt:
        print("\nStopped by user. Sent:", sent)
    finally:
        sock.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", default="sample_data.json")
    parser.add_argument("--target-ip", default="127.0.0.1")
    parser.add_argument("--target-port", type=int, default=9000)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--rate", type=float, help="messages per second (msgs/sec)")
    group.add_argument("--interval-ms", type=float, help="interval between messages in ms")
    parser.add_argument("--burst", type=int, default=1, help="packets to send per tick")
    parser.add_argument("--loops", type=int, default=None, help="total number of packets to send (None = infinite loop)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    data = load_data(args.file)
    print(f"Loaded {len(data)} snapshot(s) from {args.file}")
    run_loop(data, args.target_ip, args.target_port, rate=args.rate, interval_ms=args.interval_ms, burst=args.burst, loops=args.loops, verbose=args.verbose)
