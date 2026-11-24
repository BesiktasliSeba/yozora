#!/usr/bin/env python3
"""names the frames of Yozora in a system crash report (.ips) from the linker map of the build that crashed.

usage: yozora_ips.py REPORT.ips [SYMBOLS_DIR]

SYMBOLS_DIR holds UUID.map files (build/symbols by default, written by scripts/archive_symbols.py at every link). the report names its build by
slice_uuid. for the frames in the app binary the tool prints the function and the offset into it; the crashed thread is printed first. a frame
in no image (generated code, null) is said to be so. reads the text report of ios 5 to 14; the json report of newer systems is not read
"""
import bisect
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# __TEXT of a 32 bit slice starts here, the report prints the offset from the load address
TEXT_BASE = 0x4000
FRAME = re.compile(r"^(\d+)\s+(\S+)\s+0x([0-9a-f]+) 0x([0-9a-f]+) \+ (\d+)\s*$")
THREAD = re.compile(r"^Thread (\d+)( name:\s*(.*?))?( Crashed)?:\s*$")


def read_map(path):
    """the symbols of a ld map as two parallel lists sorted by address, and the sizes"""
    addresses, sizes, names = [], [], []
    inside = False
    for line in Path(path).read_text(errors="replace").splitlines():
        if line.startswith("# Symbols:"):
            inside = True
            continue
        if line.startswith("# Dead Stripped Symbols:"):
            break
        if not inside or line.startswith("#"):
            continue
        fields = line.split("\t", 2)
        if len(fields) != 3 or not fields[0].startswith("0x"):
            continue
        name = fields[2].split("] ", 1)[-1]
        addresses.append(int(fields[0], 16))
        sizes.append(int(fields[1], 16))
        names.append(name)
    order = sorted(range(len(addresses)), key=addresses.__getitem__)
    return [addresses[i] for i in order], [sizes[i] for i in order], [names[i] for i in order]


def demangle(names):
    tool = shutil.which("llvm-cxxfilt") or shutil.which("c++filt")
    if not tool or not names:
        return names
    result = subprocess.run([tool], input="\n".join(names) + "\n", capture_output=True, text=True)
    lines = result.stdout.splitlines()
    return lines if len(lines) == len(names) else names


def describe(symbols, offset, caller):
    """the function of an offset into the image. a return address points after the call, so it is looked up one instruction earlier"""
    addresses, sizes, names = symbols
    address = (TEXT_BASE + offset) & ~1
    if caller:
        address -= 2
    position = bisect.bisect_right(addresses, address) - 1
    if position < 0:
        return "outside the map"
    distance = address - addresses[position]
    if distance > max(sizes[position], 1) + 0x40:
        return "after %s, past its end by %d (not in the map)" % (names[position], distance - sizes[position])
    return "%s + %d" % (names[position], distance)


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    report = Path(sys.argv[1])
    folder = Path(sys.argv[2]) if len(sys.argv) == 3 else Path(__file__).resolve().parent.parent / "build" / "symbols"
    text = report.read_text(errors="replace")
    first, _, body = text.partition("\n")
    try:
        header = json.loads(first)
    except ValueError:
        raise SystemExit("yozora_ips: %s does not start with the json header of an ios 6 to 14 report" % report)
    uuid = str(header.get("slice_uuid", "")).upper()
    app = header.get("app_name", "Yozora")
    print("report of %s %s on %s, build %s" % (app, header.get("app_version", "?"), header.get("os_version", "?"), uuid or "unknown"))
    for key in ("Hardware Model", "Exception Type", "Exception Subtype", "Termination Reason"):
        match = re.search(r"^%s:\s*(.*)$" % key, body, re.M)
        if match:
            print("  %s: %s" % (key, match.group(1)))
    map_path = folder / (uuid + ".map")
    if not uuid or not map_path.is_file():
        have = sorted(path.stem for path in folder.glob("*.map")) if folder.is_dir() else []
        print("no map for this build in %s. it is the map of the link that made %s: keep build/<slice>/Yozora.map with every ipa you hand out" % (folder, uuid))
        print("maps kept: %s" % (", ".join(have) if have else "none"))
        return 2
    symbols = read_map(map_path)
    names = demangle(symbols[2])
    symbols = (symbols[0], symbols[1], names)
    threads = []
    current = None
    for line in body.splitlines():
        thread = THREAD.match(line)
        if thread:
            current = {"number": int(thread.group(1)), "name": thread.group(3) or "", "crashed": bool(thread.group(4)), "frames": []}
            threads.append(current)
            continue
        if not line.strip():
            current = None
            continue
        frame = FRAME.match(line)
        if current is not None and frame:
            current["frames"].append(frame.groups())
        elif current is not None and re.match(r"^\d+\s+\?\?\?\s", line):
            current["frames"].append((line.split()[0], "???", "0", "0", "0"))
    threads.sort(key=lambda t: (not t["crashed"], t["number"]))
    for thread in threads:
        if not thread["crashed"] and thread["number"] != 0:
            continue
        print("\nthread %d%s%s" % (thread["number"], " " + thread["name"] if thread["name"] else "", " crashed" if thread["crashed"] else ""))
        for index, image, address, base, offset in thread["frames"]:
            if image == "???":
                print("  %3s  generated code or unmapped memory" % index)
            elif image == app:
                print("  %3s  %s" % (index, describe(symbols, int(offset), int(index) > 0)))
            else:
                print("  %3s  %s" % (index, image))
    return 0


if __name__ == "__main__":
    sys.exit(main())
