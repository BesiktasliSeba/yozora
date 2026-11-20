#!/usr/bin/env python3
"""symbolizes the crash lines yozora-render prints on a fatal signal.

usage: yozora_crash.py LOG UNSTRIPPED_BINARY
"""
import bisect
import re
import subprocess
import sys

if len(sys.argv) != 3:
    sys.exit(__doc__)
log, binary = sys.argv[1:]
text = open(log, errors="replace").read()
header = re.search(r"crash signal=(\d+) code=(-?\d+) address=([0-9a-f]+) slide=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) sp=([0-9a-f]+)\nyozora-render: crash frames([0-9a-f ]*)", text)
if not header:
    sys.exit("no crash line in the log")
signal_number, code, address, slide, pc, lr, sp = header.groups()[:7]
frames = [int(value, 16) for value in header.group(8).split()]
slide = int(slide, 16)
nm = subprocess.run(["llvm-nm", "-n", "--defined-only", binary], capture_output=True, text=True, check=True)
addresses, names = [], []
for line in nm.stdout.splitlines():
    fields = line.split(None, 2)
    if len(fields) == 3 and fields[1] in "tT":
        addresses.append(int(fields[0], 16))
        names.append(fields[2][1:] if fields[2].startswith("_") else fields[2])

def describe(value, call):
    address = (value & ~1) - slide - (2 if call else 0)
    position = bisect.bisect_right(addresses, address) - 1
    if position < 0 or not 0x4000 <= address < addresses[-1] or address - addresses[position] > 0x40000:
        return "0x%x (outside the image)" % value
    name = subprocess.run(["llvm-cxxfilt"], input=names[position] + "\n", capture_output=True, text=True).stdout.strip()
    return "0x%x %s + %d" % (value, name, address - addresses[position])

registers = re.search(r"crash registers([0-9a-f ]*)", text)
print("signal %s code %s address 0x%s" % (signal_number, code, address))
if registers:
    print("registers", " ".join("r%d=%s" % (index, value) for index, value in enumerate(registers.group(1).split())))
print("pc  ", describe(int(pc, 16), False))
print("lr  ", describe(int(lr, 16), True))
for frame in frames:
    print("    ", describe(frame, True))
