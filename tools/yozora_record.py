#!/usr/bin/env python3
"""names the frames of Yozora in the crash record the app writes itself (Library/Caches/yozora-crash.log, the crashes tab of the log screen).

usage: yozora_record.py RECORD [SYMBOLS_DIR]

every block of the record (CRASH, HANG) names its build by the uuid on its binary line and prints frames as `#N 0xADDRESS Image +OFFSET`, the offset
counted from the load address like the offsets of a system report. SYMBOLS_DIR holds UUID.map files (build/symbols by default, see yozora_ips.py).
frames of other images keep the name the app printed. a block of a build whose map was not kept is printed as it is, with a note
"""
import importlib.util
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("yozora_ips", HERE / "yozora_ips.py")
ips = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ips)

BLOCK = re.compile(r"^==== ", re.M)
UUID = re.compile(r"^binary\s+uuid ([0-9A-F]{32})", re.M)
FRAME = re.compile(r"^(\s+#(\d+) 0x[0-9a-f]+\s+)Yozora \+(\d+)(.*)$")
loaded = {}


def symbols_for(folder, uuid):
    dashed = "%s-%s-%s-%s-%s" % (uuid[:8], uuid[8:12], uuid[12:16], uuid[16:20], uuid[20:])
    path = folder / (dashed + ".map")
    if not path.is_file():
        return None
    if dashed not in loaded:
        table = ips.read_map(path)
        loaded[dashed] = (table[0], table[1], ips.demangle(table[2]))
    return loaded[dashed]


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    record = Path(sys.argv[1]).read_text(errors="replace")
    folder = Path(sys.argv[2]) if len(sys.argv) == 3 else HERE.parent / "build" / "symbols"
    parts = BLOCK.split(record)
    sys.stdout.write(parts[0])
    for part in parts[1:]:
        block = "==== " + part
        found = UUID.search(block)
        symbols = symbols_for(folder, found.group(1)) if found else None
        if found and symbols is None:
            sys.stdout.write("\n(no map for build %s in %s, frames of the app stay as offsets)\n%s" % (found.group(1), folder, block))
            continue
        out = []
        for line in block.splitlines():
            frame = FRAME.match(line) if symbols else None
            if frame:
                lead, index, offset, rest = frame.groups()
                line = "%s%s" % (lead, ips.describe(symbols, int(offset), int(index) > 0))
            out.append(line)
        sys.stdout.write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
