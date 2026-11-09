"""Exercise native PNG comparison through the generated test bindings in Node."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--module', type=Path, required=True)
parser.add_argument('--node', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'actual.png').write_bytes(png(2, 1, bytes([10, 20, 30, 40, 50, 60])))
    (path / 'near.png').write_bytes(png(2, 1, bytes([11, 20, 30, 40, 50, 60])))
    (path / 'different.png').write_bytes(png(2, 1, bytes([110, 20, 30, 40, 50, 60])))
    (path / 'size.png').write_bytes(png(1, 1, bytes([10, 20, 30])))
    result = subprocess.run([args.node, str(Path(__file__).with_name('image_tests.mjs')), str(args.module.resolve()), directory])
    raise SystemExit(result.returncode)
