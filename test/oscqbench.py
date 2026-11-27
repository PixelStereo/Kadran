"""Run Kadran against a deterministic OSCQuery device using Python's standard library."""
import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import struct
import subprocess
import threading
from urllib.parse import urlsplit

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def osc_string(value):
    data = value.encode("ascii") + b"\0"
    return data + b"\0" * (-len(data) % 4)


def osc_packet(path, tag, data=b""):
    return osc_string(path) + osc_string("," + tag) + data


def leaf(path, tag, access, value=None):
    node = {"FULL_PATH": path, "TYPE": tag, "ACCESS": access}
    if value is not None:
        node["VALUE"] = [value]
    return node


class Bench(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, udp_port, actions):
        super().__init__(("127.0.0.1", 0), Handler)
        self.udp_port = udp_port
        self.lock = threading.Lock()
        self.subscriptions = set()
        self.nodes = {node["FULL_PATH"][1:]: node for node in actions}
        self.nodes.update({
            "signal": leaf("/signal", "I", 1),
            "meter": leaf("/meter", "f", 1, 0.25),
            "color": leaf("/color", "r", 3, "#FF8040C0"),
            "old": leaf("/old", "f", 1, 0.125),
        })


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.headers.get("Upgrade", "").lower() == "websocket":
            self.websocket()
            return
        url = urlsplit(self.path)
        if url.query == "HOST_INFO":
            result = {"NAME": "OSCQbench fixture", "OSC_PORT": self.server.udp_port,
                      "OSC_TRANSPORT": "UDP", "WS_PORT": self.server.server_port,
                      "EXTENSIONS": {"LISTEN": True, "PATH_ADDED": True, "PATH_REMOVED": True}}
        else:
            with self.server.lock:
                nodes = dict(self.server.nodes)
            node = {"FULL_PATH": "/", "ACCESS": 0, "CONTENTS": nodes} if url.path == "/" else nodes.get(url.path[1:])
            if node is None:
                self.send_error(404)
                return
            # The meter's initial value is in the tree. Its updates come only from LISTEN.
            if url.query == "VALUE" and url.path == "/meter" or (url.query and url.query not in node):
                self.send_response(204)
                self.end_headers()
                return
            result = {url.query: node[url.query]} if url.query else node
        body = json.dumps(result).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(400)
            return
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest())
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept.decode("ascii"))
        self.end_headers()
        self.close_connection = True
        self.connection.settimeout(12)
        try:
            while True:
                header = self.rfile.read(2)
                if len(header) != 2 or header[0] == 0x88:
                    return
                # The test client sends only short, masked, single-frame JSON commands.
                size = header[1] & 0x7f
                if header[0] != 0x81 or not header[1] & 0x80 or size > 125:
                    return
                mask = self.rfile.read(4)
                data = self.rfile.read(size)
                if len(mask) != 4 or len(data) != size:
                    return
                message = json.loads(bytes(value ^ mask[i % 4] for i, value in enumerate(data)))
                if message.get("COMMAND") == "LISTEN":
                    self.listen(message.get("DATA"))
        except (ConnectionError, TimeoutError):
            return

    def frame(self, opcode, payload):
        self.wfile.write(bytes((0x80 | opcode, len(payload))) + payload)

    def listen(self, path):
        with self.server.lock:
            if path in self.server.subscriptions:
                return
            self.server.subscriptions.add(path)
            if path == "/color":
                self.server.nodes["added"] = leaf("/added", "f", 1, 0.5)
            elif path == "/added":
                self.server.nodes.pop("old", None)
        if path == "/meter":
            # Only the binary WebSocket event carries 0.75.
            self.frame(2, osc_packet("/meter", "f", struct.pack(">f", 0.75)))
        elif path == "/color":
            self.frame(1, b'{"COMMAND":"PATH_ADDED","DATA":"/added"}')
        elif path == "/added":
            self.frame(1, b'{"COMMAND":"PATH_REMOVED","DATA":"/old"}')

    def log_message(self, *_):
        pass


def case_data(path):
    rows = [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")]
    expected = {row[1]: bytes.fromhex(row[4]) for row in rows
                if row[0] == "osc" and row[1:3] in (["/N", "N"], ["/I", "I"], ["/r", "r"])}
    actions = [json.loads(row[2]) for row in rows if row[:2] == ["oscquery-action", "1"]]
    return [node for node in actions if node["FULL_PATH"] in ("/N", "/I")], {
        expected["/N"], expected["/I"], osc_string("/color") + expected["/r"][4:],
    }


def run(client, vectors):
    actions, expected = case_data(vectors)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(0.1)
        with Bench(udp.getsockname()[1], actions) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                subprocess.run([client, str(server.server_port)], check=True, timeout=30)
                packets = []
                while True:
                    try:
                        packets.append(udp.recv(1024))
                    except socket.timeout:
                        break
                if set(packets) != expected or len(packets) != len(expected):
                    raise SystemExit("Unexpected OSC packets: " + ", ".join(packet.hex() for packet in packets))
                if not {"/meter", "/color", "/added"} <= server.subscriptions:
                    raise SystemExit("Expected LISTEN subscriptions were not received")
            finally:
                server.shutdown()
                worker.join()
    print("OSCQbench: discovery, actions, RGBA, LISTEN and dynamic paths passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client", type=Path)
    parser.add_argument("vectors", type=Path)
    args = parser.parse_args()
    run(args.client.resolve(), args.vectors)
