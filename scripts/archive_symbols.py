#!/usr/bin/env python3
"""keeps the linker map of a slice under the uuid of the binary it came from.

usage: archive_symbols.py SLICE

the system crash report of an app names a build by the uuid of its slice, and the map of the link is the only thing that names the local functions
`strip -x` takes out of the binary. build/<slice>/Yozora.map is overwritten by the next link, so a report of an older build had nothing to be read
against. the map is copied to build/symbols/UUID.map (UUID in capitals) and a line is added to build/symbols/index.txt; tools/yozora_ips.py reads them
"""
import shutil
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LC_UUID = 0x1B
MAGICS = {0xFEEDFACE: ("<", 28), 0xFEEDFACF: ("<", 32)}


def binary_uuid(path):
    """the LC_UUID of a thin little endian mach-o file, as the capitals with dashes a crash report prints"""
    with open(path, "rb") as stream:
        head = stream.read(32)
        if len(head) < 28:
            raise SystemExit("archive_symbols: %s is too short for a mach-o header" % path)
        magic = struct.unpack("<I", head[:4])[0]
        if magic not in MAGICS:
            raise SystemExit("archive_symbols: %s is not a thin little endian mach-o file (a universal binary has no single uuid)" % path)
        count = struct.unpack("<I", head[16:20])[0]
        stream.seek(MAGICS[magic][1])
        for _ in range(count):
            command = stream.read(8)
            if len(command) < 8:
                break
            kind, size = struct.unpack("<II", command)
            if size < 8:
                break
            body = stream.read(size - 8)
            if kind == LC_UUID and len(body) >= 16:
                raw = body[:16].hex().upper()
                return "-".join((raw[:8], raw[8:12], raw[12:16], raw[16:20], raw[20:]))
    raise SystemExit("archive_symbols: %s has no LC_UUID" % path)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    slice_name = sys.argv[1]
    binary = ROOT / "build" / slice_name / "Yozora"
    link_map = ROOT / "build" / slice_name / "Yozora.map"
    for path in (binary, link_map):
        if not path.is_file():
            raise SystemExit("archive_symbols: %s is missing, link the %s slice first" % (path, slice_name))
    uuid = binary_uuid(binary)
    folder = ROOT / "build" / "symbols"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / (uuid + ".map")
    shutil.copyfile(link_map, target)
    with open(folder / "index.txt", "a") as index:
        index.write("%s %s %s\n" % (uuid, slice_name, time.strftime("%Y-%m-%d %H:%M:%S")))
    # the file the build graph watches, so the step runs again after the next link
    (folder / (slice_name + "-latest.txt")).write_text(uuid + "\n")
    print("yozora: map of %s kept as build/symbols/%s.map" % (slice_name, uuid))


if __name__ == "__main__":
    main()
