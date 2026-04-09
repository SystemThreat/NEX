#!/usr/bin/env python3
"""
NEX Wallet Server — serves the PWA + proxies RPC to the local node.
Solves CORS: browser talks to THIS server, which forwards to nexd.

Usage:
    python3 serve.py                         # defaults: wallet on 8090, node on 127.0.0.1:9332
    python3 serve.py --port 8090 --rpc-url http://127.0.0.1:9332
"""

import http.server
import json
import urllib.request
import urllib.error
import argparse
import os
import sys

WALLET_PORT = 8090
RPC_URL = 'http://127.0.0.1:9332'
REMOTE_URL = 'http://44.213.68.147:9332'

def detect_rpc():
    """Try remote first, fall back to local."""
    for url in [REMOTE_URL, RPC_URL]:
        try:
            req = urllib.request.Request(url,
                data=b'{"jsonrpc":"1.0","id":"ping","method":"getblockchaininfo","params":[]}',
                headers={'Content-Type': 'application/json', 'Authorization': 'Basic bmV4OjZjNDFlOTkxNDE5YmVmMjkzZDk5MTAyNDNhMTg4ZmMy'})
            urllib.request.urlopen(req, timeout=3)
            return url
        except:
            continue
    return RPC_URL

class WalletHandler(http.server.SimpleHTTPRequestHandler):
    """Serves static files + proxies POST /rpc → nexd."""

    def do_POST(self):
        if self.path.startswith('/rpc'):
            self.proxy_rpc()
        else:
            self.send_error(404)

    def do_OPTIONS(self):
        # CORS preflight
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, Authorization')
        self.send_header('Content-Length', '0')
        self.end_headers()

    def proxy_rpc(self):
        try:
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length)

            # Forward auth header from client
            auth = self.headers.get('Authorization', '')

            # Determine target URL (might include /wallet/name)
            target = RPC_URL
            if '?' in self.path:
                # /rpc?wallet=name → http://node:9332/wallet/name
                params = dict(p.split('=', 1) for p in self.path.split('?', 1)[1].split('&') if '=' in p)
                if 'wallet' in params and params['wallet']:
                    target += '/wallet/' + params['wallet']

            req = urllib.request.Request(
                target,
                data=body,
                headers={
                    'Content-Type': 'application/json',
                    'Authorization': auth,
                },
                method='POST'
            )

            with urllib.request.urlopen(req, timeout=30) as resp:
                result = resp.read()
                self.send_response(resp.status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.send_header('Content-Length', str(len(result)))
                self.end_headers()
                self.wfile.write(result)

        except urllib.error.HTTPError as e:
            body = e.read()
            self.send_response(e.code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        except Exception as e:
            error = json.dumps({'error': str(e)}).encode()
            self.send_response(502)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Content-Length', str(len(error)))
            self.end_headers()
            self.wfile.write(error)

    def log_message(self, fmt, *args):
        # Compact logging
        if '/rpc' in (args[0] if args else ''):
            sys.stderr.write(f"  RPC → {args[0]}\n")
        elif '200' not in str(args):
            sys.stderr.write(f"  {args[0]}\n")

    def end_headers(self):
        # Add security headers to all responses
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')
        super().end_headers()


def main():
    global RPC_URL, WALLET_PORT
    parser = argparse.ArgumentParser(description='NEX Wallet Server')
    parser.add_argument('--port', type=int, default=8090, help='Wallet HTTP port (default: 8090)')
    parser.add_argument('--rpc-url', default='http://127.0.0.1:9332', help='NEX node RPC URL (default: http://127.0.0.1:9332)')
    args = parser.parse_args()

    WALLET_PORT = args.port
    if args.rpc_url != 'http://127.0.0.1:9332':
        RPC_URL = args.rpc_url
    else:
        RPC_URL = detect_rpc()

    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    server = http.server.HTTPServer(('127.0.0.1', WALLET_PORT), WalletHandler)
    print(f"""
  ╔═══════════════════════════════════════╗
  ║         NEX WALLET SERVER             ║
  ╠═══════════════════════════════════════╣
  ║  Wallet:  http://127.0.0.1:{WALLET_PORT:<5}     ║
  ║  Node:    {RPC_URL:<28}║
  ║  RPC via: /rpc (same-origin proxy)    ║
  ╚═══════════════════════════════════════╝
""")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nShutdown.')
        server.server_close()


if __name__ == '__main__':
    main()
