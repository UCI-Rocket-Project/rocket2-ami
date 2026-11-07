#!/usr/bin/env python3
"""Export the AMI v2 flash log over USB CDC to CSV.

Usage: python3 tools/dump_flash.py /dev/ttyACM0 capture.csv
Requires pyserial. The board must be mounted and idle before starting.
"""

import argparse
import csv
import struct
import sys

PAGE_BYTES = 2043
SAMPLES_PER_PAGE = 339
SAMPLE_BYTES = SAMPLES_PER_PAGE * 6


def read_exact(port, count):
    data = bytearray()
    while len(data) < count:
        part = port.read(count - len(data))
        if not part:
            raise TimeoutError(f"USB serial timed out after {len(data)}/{count} bytes")
        data.extend(part)
    return bytes(data)


def export_frames(port, output):
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
        port.write(b"dump bin\n")
        response = port.readline().strip()
        if response != b"OK dump bin":
            raise RuntimeError(f"board rejected dump: {response.decode(errors='replace')}")
        with open(args.output, "w", newline="") as output:
            pages = export_frames(port, output)
    print(f"Exported {pages} pages and {pages * SAMPLES_PER_PAGE} XYZ samples to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"dump failed: {exc}", file=sys.stderr)
        sys.exit(1)
