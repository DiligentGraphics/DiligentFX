"""Shared loopback HTTP fixtures for Radient network resolver tests."""

import gzip
import json
import struct
import threading
import time
from urllib.parse import urlsplit

PREFIX = "/radient-network"
_LOCK = threading.Lock()
_ACTIVE = 0
POSITIONS = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
IMAGE = bytearray(21)
IMAGE[2], IMAGE[12], IMAGE[14], IMAGE[16] = 2, 1, 1, 24
IMAGE[18:] = b"\xff\xff\xff"
DOCUMENT = json.dumps({
    "asset": {"version": "2.0"}, "scene": 0,
    "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
    "buffers": [{"uri": "mesh.bin", "byteLength": len(POSITIONS)}],
    "bufferViews": [{"buffer": 0, "byteLength": len(POSITIONS)}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3,
                   "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
    "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
    "textures": [{"source": 0}], "images": [{"uri": "image.tga"}],
}).encode()
JSON_CHUNK = DOCUMENT + b" " * (-len(DOCUMENT) % 4)
GLB = struct.pack("<5I", 0x46546C67, 2, 20 + len(JSON_CHUNK), len(JSON_CHUNK), 0x4E4F534A) + JSON_CHUNK


def respond(handler, status=200, body=b"", headers=None, body_delay=0, declared_size=None):
    handler.send_response(status)
    handler.send_header("Content-Length", str(len(body) if declared_size is None else declared_size))
    for name, value in (headers or {}).items():
        handler.send_header(name, value)
    handler.end_headers()
    if handler.command != "HEAD":
        if body_delay:
            handler.wfile.flush()
            time.sleep(body_delay)
        handler.wfile.write(body)


def handle_request(handler):
    """Handle only fixture URLs; leave module serving and reports to the runner."""
    global _ACTIVE
    path = urlsplit(handler.path).path
    if not path.startswith(PREFIX + "/"):
        return False
    path = path[len(PREFIX):]
    try:
        if path == "/bytes" or path == "/cors-bytes":
            headers = {"Access-Control-Allow-Origin": "*"} if path == "/cors-bytes" else {}
            respond(handler, body=b"A\0B\xff", headers=headers)
        elif path == "/empty":
            respond(handler)
        elif path.startswith("/status/"):
            respond(handler, int(path.rsplit("/", 1)[1]), b"error body")
        elif path in ("/entry.gltf", "/entry.glb", "/loop", "/cors-ok", "/cors-denied"):
            targets = {"/entry.gltf": PREFIX + "/model/scene.gltf",
                       "/entry.glb": PREFIX + "/model/123", "/loop": PREFIX + "/loop",
                       "/cors-ok": f"http://localhost:{handler.server.server_port}{PREFIX}/cors-bytes",
                       "/cors-denied": f"http://localhost:{handler.server.server_port}{PREFIX}/bytes"}
            respond(handler, 302, headers={"Location": targets[path]})
        elif path == "/model/scene.gltf":
            respond(handler, body=DOCUMENT)
        elif path == "/model/123":
            respond(handler, body=GLB)
        elif path == "/model/mesh.bin":
            respond(handler, body=POSITIONS)
        elif path == "/model/image.tga":
            respond(handler, body=IMAGE)
        elif path == "/gzip":
            respond(handler, body=gzip.compress(DOCUMENT), headers={"Content-Encoding": "gzip"})
        elif path == "/truncated":
            respond(handler, body=b"part", declared_size=64)
            handler.close_connection = True
        elif path == "/slow-body":
            respond(handler, body=b"body", body_delay=1)
        elif path in ("/slow-headers", "/pending.gltf"):
            if path == "/pending.gltf":
                with _LOCK:
                    _ACTIVE += 1
            try:
                time.sleep(1)
                respond(handler, body=DOCUMENT)
            finally:
                if path == "/pending.gltf":
                    with _LOCK:
                        _ACTIVE -= 1
        elif path == "/active":
            with _LOCK:
                status = 200 if _ACTIVE else 404
            respond(handler, status)
        else:
            respond(handler, 404)
    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
        # Expected when AbortController terminates a timed-out transfer.
        pass
    return True
