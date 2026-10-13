#!/usr/bin/env python3
"""SacredFire online, step 3: a private "discovery" server that only listens and writes down
what the game sends (docs/sacredfire/15-online.md).

The port answers every host name it resolves with BB_ONLINE_SERVER's address, sends HTTP and
HTTPS as plain HTTP to BB_ONLINE_HTTP_PORT, and refuses every other destination. Run this on the
same PC (BB_ONLINE_SERVER=127.0.0.1) to see which services, ports and messages the game uses:

    python tools/online/discovery_server.py
    python tools/online/discovery_server.py --tcp 3074-3080,443 --udp 3074 --http 8080

Every connection, datagram and HTTP request goes to the console and to online-discovery.log
(hex and text). HTTP requests are answered with --http-status (404 by default, empty body);
TCP and UDP traffic is only read, never answered. Standard library only.
"""

import argparse
import asyncio
import datetime
import os
import sys

LOG = None


def log(line):
    stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    text = f"{stamp} {line}"
    print(text, flush=True)
    if LOG:
        LOG.write(text + "\n")
        LOG.flush()


def dump(data, limit=4096):
    """Hex and printable text, 16 bytes a line."""
    lines = []
    shown = data[:limit]
    for offset in range(0, len(shown), 16):
        chunk = shown[offset:offset + 16]
        hexpart = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"    {offset:06x}  {hexpart:<47}  {text}")
    if len(data) > limit:
        lines.append(f"    ... {len(data) - limit} more bytes")
    return "\n".join(lines)


def ports(text):
    """'3074-3080,443' -> [3074, ..., 3080, 443]."""
    result = []
    for part in filter(None, (p.strip() for p in text.split(","))):
        if "-" in part:
            first, last = part.split("-", 1)
            result.extend(range(int(first), int(last) + 1))
        else:
            result.append(int(part))
    return result


class Tcp:
    def __init__(self, port):
        self.port = port

    async def handle(self, reader, writer):
        peer = writer.get_extra_info("peername")
        log(f"TCP {self.port}: connection from {peer[0]}:{peer[1]}")
        total = 0
        try:
            while True:
                data = await reader.read(65536)
                if not data:
                    break
                total += len(data)
                log(f"TCP {self.port}: {len(data)} bytes\n{dump(data)}")
        except (ConnectionError, OSError) as error:
            log(f"TCP {self.port}: {error}")
        log(f"TCP {self.port}: closed after {total} bytes")
        writer.close()


class Udp(asyncio.DatagramProtocol):
    def __init__(self, port):
        self.port = port

    def datagram_received(self, data, addr):
        log(f"UDP {self.port}: {len(data)} bytes from {addr[0]}:{addr[1]}\n{dump(data)}")

    def error_received(self, exc):
        log(f"UDP {self.port}: {exc}")


class Http:
    def __init__(self, port, status):
        self.port = port
        self.status = status

    async def handle(self, reader, writer):
        try:
            head = await reader.readuntil(b"\r\n\r\n")
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError):
            writer.close()
            return
        lines = head.decode("latin-1").split("\r\n")
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                name, value = line.split(":", 1)
                headers[name.strip().lower()] = value.strip()
        length = int(headers.get("content-length", "0") or 0)
        body = await reader.readexactly(length) if length else b""
        scheme = headers.get("x-sacredfire-scheme", "http")
        host = headers.get("host", "?")
        log(f"HTTP {lines[0].split(' ')[0]} {scheme}://{host}{lines[0].split(' ')[1]}")
        for line in lines[1:]:
            if line:
                log(f"    {line}")
        if body:
            log(f"    body ({len(body)} bytes)\n{dump(body)}")
        reason = {200: "OK", 404: "Not Found", 503: "Service Unavailable"}.get(self.status, "Status")
        writer.write(f"HTTP/1.1 {self.status} {reason}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                     .encode())
        await writer.drain()
        writer.close()


async def main(args):
    loop = asyncio.get_running_loop()
    servers = []
    http = Http(args.http, args.http_status)
    servers.append(await asyncio.start_server(http.handle, args.bind, args.http))
    log(f"HTTP on {args.bind}:{args.http} (answers {args.http_status})")
    for port in ports(args.tcp):
        if port == args.http:
            continue
        try:
            servers.append(await asyncio.start_server(Tcp(port).handle, args.bind, port))
        except OSError as error:
            log(f"TCP {port}: cannot listen ({error})")
    log(f"TCP on {args.tcp}")
    for port in ports(args.udp):
        try:
            await loop.create_datagram_endpoint(lambda p=port: Udp(p), local_addr=(args.bind, port))
        except OSError as error:
            log(f"UDP {port}: cannot listen ({error})")
    log(f"UDP on {args.udp}")
    log("Waiting for the game (Ctrl+C to stop)")
    await asyncio.gather(*(s.serve_forever() for s in servers))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bind", default="127.0.0.1", help="address to listen on (0.0.0.0 for the LAN)")
    parser.add_argument("--http", type=int, default=8080, help="HTTP port (BB_ONLINE_HTTP_PORT)")
    parser.add_argument("--http-status", type=int, default=404, help="status of every HTTP reply")
    parser.add_argument("--tcp", default="3074-3080,443", help="TCP ports to listen on")
    parser.add_argument("--udp", default="3074-3080", help="UDP ports to listen on")
    parser.add_argument("--log", default="online-discovery.log", help="log file")
    args = parser.parse_args()
    LOG = open(args.log, "a", encoding="utf-8")
    log(f"--- discovery server, Python {sys.version.split()[0]}, pid {os.getpid()}")
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        log("stopped")
