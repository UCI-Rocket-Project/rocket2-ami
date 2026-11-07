#!/usr/bin/env python3
"""Export the AMI v2 flash log over USB CDC to CSV.

Usage: python3 tools/dump_flash.py /dev/ttyACM0 capture.csv
Requires pyserial. The board must be mounted and idle or full before starting.
"""

import argparse
import csv
import re
import struct
import sys
import time

PAGE_BYTES = 2043
SAMPLES_PER_PAGE = 339
SAMPLE_BYTES = SAMPLES_PER_PAGE * 6
DATA_PAGES_PER_BLOCK = 63
MAX_BLOCKS = 1024


def estimated_pages_from_status(port):
    """Return an upper estimate from the physical write cursor."""
    port.write(b"status\n")
    for _ in range(10):
        response = port.readline().strip()
        if not response:
            raise TimeoutError("timed out waiting for board status")
        if not response.startswith(b"STATUS "):
            continue  # Earlier notices may still be queued by the board.
        mode = re.search(rb"\bmode=([a-z-]+)", response)
        mounted = re.search(rb"\bmounted=([01])", response)
        cursor = re.search(rb"\bnext=(\d+):(\d+)", response)
        if not mode or not mounted or not cursor:
            raise ValueError(f"invalid board status: {response.decode(errors='replace')}")
        if mode.group(1) not in (b"idle", b"full") or mounted.group(1) != b"1":
            raise RuntimeError(f"board is not ready to dump: {response.decode(errors='replace')}")
        block, page = map(int, cursor.groups())
        if block > MAX_BLOCKS or not 1 <= page <= 64:
            raise ValueError(f"invalid flash cursor: {block}:{page}")
        return block * DATA_PAGES_PER_BLOCK + (page - 1 if page < 64 else 0)
    raise RuntimeError("board did not respond to status")


class DumpProgress:
    def __init__(self, estimated_pages):
        self.estimated_pages = estimated_pages
        self.started = time.monotonic()
        self.last_update = 0.0
        self.last_width = 0
        self.terminal = sys.stderr.isatty()
        self.pages = 0

    def update(self, pages, done=False):
        self.pages = pages
        now = time.monotonic()
        interval = 0.2 if self.terminal else 5.0
        if not done and now - self.last_update < interval:
            return
        self.last_update = now
        elapsed = max(now - self.started, 0.001)
        width = 24
        if done:
            bar = "#" * width
            amount = "100% done"
        elif self.estimated_pages:
            fraction = min(pages / self.estimated_pages, 1.0)
            filled = int(width * fraction)
            bar = "#" * filled + "-" * (width - filled)
            amount = f"{fraction * 100:5.1f}% est"
        else:
            position = int(elapsed * 4) % width
            bar = "-" * position + "#" + "-" * (width - position - 1)
            amount = "dumping"
        page_count = (f"{pages:,}/{self.estimated_pages:,} est pages"
                      if self.estimated_pages and not done else f"{pages:,} pages")
        text = (f"[{bar}] {amount} | {page_count} | "
                f"{pages * SAMPLES_PER_PAGE:,} XYZ | "
                f"{pages * PAGE_BYTES / 1048576:.1f} MiB | {pages / elapsed:.1f} pages/s")
        if self.terminal:
            sys.stderr.write("\r" + text.ljust(self.last_width))
            self.last_width = max(self.last_width, len(text))
        else:
            sys.stderr.write(text + "\n")
        sys.stderr.flush()

    def finish_line(self):
        if self.terminal and self.last_width:
            sys.stderr.write("\n")
            sys.stderr.flush()


def read_exact(port, count):
    data = bytearray()
    while len(data) < count:
        part = port.read(count - len(data))
        if not part:
            raise TimeoutError(f"USB serial timed out after {len(data)}/{count} bytes")
        data.extend(part)
    return bytes(data)


def export_frames(port, output, progress=None):
    writer = csv.writer(output)
    writer.writerow(("page", "sample", "page_time10us", "x", "y", "z"))
    expected_page = 0
    while True:
        marker = read_exact(port, 4)
        if marker in (b"UEND", b"UABT"):
            total = struct.unpack("<I", read_exact(port, 4))[0]
            if total != expected_page:
                raise ValueError(f"end count {total} != received pages {expected_page}")
            if marker == b"UABT":
                raise RuntimeError(f"dump aborted after {total} pages")
            return total
        if marker != b"UCP2":
            raise ValueError(f"bad frame marker {marker!r}")
        page, length, samples = struct.unpack("<IHH", read_exact(port, 8))
        if page != expected_page or length != PAGE_BYTES or samples != SAMPLES_PER_PAGE:
            raise ValueError(f"unexpected page frame: {page=}, {length=}, {samples=}")
        payload = read_exact(port, length)
        timestamp = struct.unpack_from("<I", payload)[0]
        for index, (x, y, z) in enumerate(
            struct.iter_unpack("<hhh", payload[4 : 4 + SAMPLE_BYTES])
        ):
            writer.writerow((page, index, timestamp, x, y, z))
        expected_page += 1
        if progress:
            progress.update(expected_page)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="CDC port, such as /dev/ttyACM0")
    parser.add_argument("output", help="CSV output filename")
    args = parser.parse_args()
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Install pyserial: python3 -m pip install pyserial") from exc

    with serial.Serial(args.port, 115200, timeout=10, write_timeout=3) as port:
        estimated_pages = estimated_pages_from_status(port)
        port.write(b"dump bin\n")
        response = port.readline().strip()
        if response != b"OK dump bin":
            raise RuntimeError(f"board rejected dump: {response.decode(errors='replace')}")
        progress = DumpProgress(estimated_pages)
        progress.update(0)
        with open(args.output, "w", newline="") as output:
            try:
                pages = export_frames(port, output, progress)
                progress.update(pages, done=True)
            finally:
                progress.finish_line()
    print(f"Exported {pages} pages and {pages * SAMPLES_PER_PAGE} XYZ samples to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"dump failed: {exc}", file=sys.stderr)
        sys.exit(1)
