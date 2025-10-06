# handler_conventional.py
import socket
import csv
import time
from datetime import datetime

UDP_IP = "0.0.0.0"
UDP_PORT = 9000
OUT_CSV = "recv_log.csv"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
print(f"Listening on {UDP_IP}:{UDP_PORT}")

with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["recv_time_iso", "recv_time_perf_us", "addr", "packet_len"])
    try:
        while True:
            data, addr = sock.recvfrom(65535)
            now_iso = datetime.utcnow().isoformat() + "Z"
            now_perf_us = int(time.perf_counter() * 1_000_000)
            writer.writerow([now_iso, now_perf_us, f"{addr[0]}:{addr[1]}", len(data)])
            f.flush()
            # optional: print small samples
            print(f"recv {len(data)} bytes from {addr} at {now_iso}")
    except KeyboardInterrupt:
        print("Stopped.")
