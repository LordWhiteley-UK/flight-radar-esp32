#!/usr/bin/env python3
"""
preview/serve.py — local HTTP server for the icon preview page.

Serves the contents of this preview/ directory on
    http://127.0.0.1:8080/icons.html

So you can sanity-check the aircraft glyph sizes in any browser,
without flashing the ESP32.

Usage:
    python3 preview/serve.py            # default 127.0.0.1:8080
    python3 preview/serve.py --port 9000
    python3 preview/serve.py --host 0.0.0.0   # bind all interfaces
                                              # (so a phone on the same
                                              # WiFi can view too)

Stop with Ctrl-C.
"""
import argparse
import http.server
import socketserver
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Disable caching while iterating on the preview page.
        self.send_header("Cache-Control", "no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def log_message(self, fmt, *args):
        # Only log non-200 responses so the terminal stays quiet.
        msg = fmt % args
        if " 200 " not in msg:
            sys.stderr.write(msg + "\n")


def main():
    ap = argparse.ArgumentParser(description="Serve the Flight Tracker icon preview page.")
    ap.add_argument("--host", default="127.0.0.1",
                    help="Interface to bind (default 127.0.0.1). Use 0.0.0.0 to expose on the LAN.")
    ap.add_argument("--port", type=int, default=8080,
                    help="TCP port (default 8080).")
    args = ap.parse_args()

    # SimpleHTTPRequestHandler serves the current working directory by default,
    # so chdir into the preview dir first.
    import os
    os.chdir(HERE)

    with socketserver.TCPServer((args.host, args.port), QuietHandler) as httpd:
        url = f"http://{args.host}:{args.port}/icons.html"
        print(f"Flight Tracker icon preview")
        print(f"  Serving from: {HERE}")
        print(f"  Open in browser: {url}")
        print(f"  Press Ctrl-C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()