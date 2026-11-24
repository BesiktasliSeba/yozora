"""yozora_record.py names the app frames of a crash record from the map of the build it carries

the record is written by the app (app/YZCrash.m), the map by the linker. the test builds both by hand: a block of a known build gets its
frames named, a block of a build without a map is passed through with a note, frames of other images are left alone
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UUID = "0123456789ABCDEF0123456789ABCDEF"
DASHED = "01234567-89AB-CDEF-0123-456789ABCDEF"
OTHER = "FEDCBA9876543210FEDCBA9876543210"

# ld map: the address is the unslid vm address, the text of an armv7 slice starts at 0x4000
MAP = """# Path: build/armv7/Yozora
# Arch: armv7
# Symbols:
# Address\tSize    \tFile  Name
0x00004000\t0x00000100\t[  1] _first_function
0x00004100\t0x00000200\t[  1] _second_function
# Dead Stripped Symbols:
"""

RECORD = """
---- launch 2026-11-24 10:00:00 UTC: Yozora 1.4.0-stable (1) armv7, pid 7

==== CRASH 2026-11-24 10:01:03 UTC ====
what      SIGSEGV (segmentation fault)
binary    uuid {uuid}, slide 0x1000, offsets in this block are from the load address

thread #3 "yozora.core" CRASHED: running, cpu 98%
    #0 0x0  null address
    #1 0x464f51c6  in no image: generated code (jit) or a jump into nothing
    #2 0x2f0  Yozora +16
    #3 0x4a0  Yozora +272
    #4 0x31eab474  libsystem_kernel.dylib +5236  mach_msg_trap

==== HANG 2026-11-24 10:02:00 UTC ====
binary    uuid {other}, slide 0x0, offsets in this block are from the load address
    #0 0x10  Yozora +16
"""


def run(record, folder):
    path = folder / "record.log"
    path.write_text(record)
    result = subprocess.run([sys.executable, "-I", "-B", str(ROOT / "tools" / "yozora_record.py"), str(path), str(folder)], text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    return result.stdout


def main():
    with tempfile.TemporaryDirectory(prefix="yozora-record-") as directory:
        folder = Path(directory)
        (folder / (DASHED + ".map")).write_text(MAP)
        output = run(RECORD.format(uuid=UUID, other=OTHER), folder)
        # an offset is counted from the load address, so +16 is 0x4010 and +272 is 0x4110. a return address is looked up one instruction
        # (two bytes of thumb) earlier, the frame after the pc is a call
        assert "#2 0x2f0  _first_function + 14" in output, output
        assert "#3 0x4a0  _second_function + 14" in output, output
        assert "libsystem_kernel.dylib +5236  mach_msg_trap" in output, output
        assert "null address" in output and "generated code (jit)" in output, output
        assert "no map for build " + OTHER in output and "#0 0x10  Yozora +16" in output, output
        assert "---- launch 2026-11-24 10:00:00 UTC" in output, output
    print("crash record: frames named from the map, unknown builds passed through")


if __name__ == "__main__":
    main()
