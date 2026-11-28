#!/usr/bin/env python3
"""Tiny local Range-capable HTTP server for 3FP I/O smoke tests."""
import argparse
import os
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class RangeHandler(BaseHTTPRequestHandler):
    root = "."
    delay = 0.0
    latency_file = ""
    stats_file = ""
    request_count = 0
    stats_lock = threading.Lock()
    disconnected_paths = set()
    disconnect_lock = threading.Lock()
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def record_request(self):
        with RangeHandler.stats_lock:
            RangeHandler.request_count += 1
            if RangeHandler.stats_file:
                try:
                    with open(RangeHandler.stats_file, "w", encoding="utf-8") as fh:
                        fh.write(str(RangeHandler.request_count))
                except OSError:
                    pass

    def current_latency(self):
        if self.latency_file:
            try:
                with open(self.latency_file, "r", encoding="utf-8") as fh:
                    return max(0.0, float(fh.read().strip() or "0"))
            except (OSError, ValueError):
                pass
        return self.delay

    def do_GET(self):
        self.record_request()
        rel = self.path.split("?", 1)[0].lstrip("/")
        fault = ""
        truncate_bytes = 0
        stall_seconds = 0.0
        if rel.startswith("__no_range/"):
            fault = "no_range"
            rel = rel[len("__no_range/"):]
        elif rel.startswith("__wrong_range/"):
            fault = "wrong_range"
            rel = rel[len("__wrong_range/"):]
        elif rel.startswith("__truncate/"):
            parts = rel.split("/", 2)
            if len(parts) != 3:
                self.send_error(400)
                return
            try:
                truncate_bytes = int(parts[1])
            except ValueError:
                self.send_error(400)
                return
            fault = "truncate"
            rel = parts[2]
        elif rel.startswith("__stall/"):
            parts = rel.split("/", 2)
            if len(parts) != 3:
                self.send_error(400)
                return
            try:
                stall_seconds = float(parts[1])
            except ValueError:
                self.send_error(400)
                return
            fault = "stall"
            rel = parts[2]
        elif rel.startswith("__disconnect/"):
            parts = rel.split("/", 2)
            if len(parts) != 3:
                self.send_error(400)
                return
            try:
                truncate_bytes = int(parts[1])
            except ValueError:
                self.send_error(400)
                return
            rel = parts[2]
            with RangeHandler.disconnect_lock:
                if rel not in RangeHandler.disconnected_paths:
                    RangeHandler.disconnected_paths.add(rel)
                    fault = "short_range"
        path = os.path.abspath(os.path.join(self.root, rel))
        if not os.path.commonpath([self.root, path]) == self.root or not os.path.isfile(path):
            self.send_error(404)
            return
        latency = self.current_latency()
        if latency > 0:
            time.sleep(latency)
        stat = os.stat(path)
        size = stat.st_size
        start, end = 0, size - 1
        match = re.match(r"bytes=(\d+)-(\d*)", self.headers.get("Range", ""))
        ranged = bool(match) and fault != "no_range"
        if ranged:
            start = int(match.group(1))
            if match.group(2):
                end = min(int(match.group(2)), size - 1)
            if start >= size or start > end:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
        length = end - start + 1
        if fault == "short_range":
            # A valid but short 206: the body matches its Content-Range, so the
            # client cannot call it corrupt; the next request must resume at
            # the byte after this range.
            end = min(start + max(0, truncate_bytes) - 1, size - 1)
            length = end - start + 1
        self.send_response(206 if ranged else 200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("ETag", f"\"{size}-{int(stat.st_mtime)}\"")
        self.send_header("Content-Length", str(length))
        if ranged:
            content_start = start + 1 if fault == "wrong_range" else start
            self.send_header("Content-Range", f"bytes {content_start}-{end}/{size}")
        if fault in ("truncate", "short_range"):
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        body_bytes = length if fault != "truncate" else min(length, truncate_bytes)
        stall_body = fault == "stall" and start > 0
        if stall_body:
            body_bytes = min(length, 4096)
        with open(path, "rb") as f:
            f.seek(start)
            remaining = body_bytes
            while remaining:
                chunk = f.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                if self.delay > 0:
                    time.sleep(self.delay)
                self.wfile.write(chunk)
                remaining -= len(chunk)
        if stall_body:
            # A range request (seek/reconnect) gets headers and a few bytes,
            # then the origin stops talking. Close/Abort must not wait out the
            # caller's full receive budget.
            time.sleep(stall_seconds)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=18865)
    ap.add_argument("--delay", type=float, default=0.0)
    ap.add_argument("--latency-file", default="")
    ap.add_argument("--stats-file", default="")
    args = ap.parse_args()
    RangeHandler.root = os.path.abspath(args.root)
    RangeHandler.delay = max(0.0, args.delay)
    RangeHandler.latency_file = os.path.abspath(args.latency_file) if args.latency_file else ""
    RangeHandler.stats_file = os.path.abspath(args.stats_file) if args.stats_file else ""
    if RangeHandler.stats_file:
        with open(RangeHandler.stats_file, "w", encoding="utf-8") as fh:
            fh.write("0")
    ThreadingHTTPServer(("127.0.0.1", args.port), RangeHandler).serve_forever()


if __name__ == "__main__":
    sys.exit(main())
