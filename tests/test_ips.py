"""the crash report reader names frames from the map of the build, and the archive step files a map under the uuid of its binary"""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import archive_symbols  # noqa: E402

UUID = "B06E8A54-E2A5-3980-8C94-C54F9CB62762"
MAP = """# Path: build/armv7/Yozora
# Arch: armv7
# Symbols:
# Address\tSize    \tFile  Name
0x00016A40\t0x000002F0\t[  1] -[YZAppDelegate application:didFinishLaunchingWithOptions:]
0x00020000\t0x00000100\t[  2] _first_function
0x00020100\t0x00000200\t[  2] _second_function
0x00030000\t0x00000010\t[  3] _far_away
# Dead Stripped Symbols:
#\tSize    \tFile  Name
<<dead>>\t0x00000100\t[  2] _dead
"""


def report(frames, uuid=UUID):
    header = {"bug_type": "109", "os_version": "iPhone OS 10.3.3 (14G60)", "app_name": "Yozora", "app_version": "1.3.0-stable", "slice_uuid": uuid.lower()}
    lines = [json.dumps(header), "Incident Identifier: x", "Hardware Model:      iPad3,4", "Exception Type:  EXC_BAD_ACCESS (SIGSEGV)",
             "Exception Subtype: KERN_INVALID_ADDRESS at 0x00000064", "", "Thread 0 name:  Dispatch queue: com.apple.main-thread", "Thread 0:",
             "0   libsystem_kernel.dylib        \t0x1b7a8900 0x1b7a8000 + 2304", "", "Thread 1 Crashed:"]
    lines += frames
    lines += ["", "Thread 1 crashed with ARM Thread State (32-bit):", "    r0: 0x0000000c", ""]
    return "\n".join(lines)


def run_tool(directory, text, symbols):
    path = Path(directory) / "report.ips"
    path.write_text(text)
    return subprocess.run([sys.executable, "-B", str(ROOT / "tools/yozora_ips.py"), str(path), str(symbols)], capture_output=True, text=True)


def mach_o(uuid_bytes):
    uuid_command = struct.pack("<II", archive_symbols.LC_UUID, 24) + uuid_bytes
    segment = struct.pack("<II", 0x1, 8)
    header = struct.pack("<7I", 0xFEEDFACE, 12, 9, 2, 2, len(segment) + len(uuid_command), 0)
    return header + segment + uuid_command


def main():
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    with tempfile.TemporaryDirectory() as directory:
        symbols = Path(directory) / "symbols"
        symbols.mkdir()
        (symbols / (UUID + ".map")).write_text(MAP)
        # offset 0x1C000 from the load address is 0x20000 in the map, a return address is looked up two bytes earlier
        frames = ["0   ???                           \t0x7aad1362 0 + 2058163042",
                  "1   Yozora                        \t0x02aaa544 0x9c000 + %d" % (0x20010 - 0x4000 + 2),
                  "2   Yozora                        \t0x02aaa544 0x9c000 + %d" % (0x20120 - 0x4000),
                  "3   Yozora                        \t0x02aaa544 0x9c000 + %d" % (0x30500 - 0x4000),
                  "4   libsystem_pthread.dylib       \t0x1b87493a 0x1b871000 + 14650"]
        result = run_tool(directory, report(frames), symbols)
        check(result.returncode == 0, "the reader failed: %s" % result.stderr)
        out = result.stdout
        check("generated code or unmapped memory" in out, "a frame in no image is not said to be generated code")
        check("_first_function + 16" in out or "first_function + 16" in out, "a return address was not looked up in its caller: %s" % out)
        check("second_function + 30" in out, "a frame deep inside a function is wrong: %s" % out)
        check("not in the map" in out, "a frame far past the last symbol is named as if it belonged to it: %s" % out)
        check("libsystem_pthread.dylib" in out, "a system frame is dropped")
        check("EXC_BAD_ACCESS" in out and "iPad3,4" in out, "the exception and the model are not printed")
        check("thread 1 crashed" in out and out.index("thread 1 crashed") < out.index("thread 0"), "the crashed thread does not come first")
        check("_dead" not in out, "a dead stripped symbol was read as live")

        missing = run_tool(directory, report(frames, "00000000-0000-0000-0000-000000000000"), symbols)
        check(missing.returncode == 2 and UUID in missing.stdout, "a report of an unknown build must say which maps exist: %s" % missing.stdout)
        check(f"maps kept: {UUID}" in missing.stdout, "the maps that exist are not listed")

        bad = run_tool(directory, "not a report\nrest", symbols)
        check(bad.returncode != 0, "text that is no report was accepted")

        raw = bytes.fromhex("B06E8A54E2A539808C94C54F9CB62762")
        binary = Path(directory) / "Yozora"
        binary.write_bytes(mach_o(raw))
        check(archive_symbols.binary_uuid(binary) == UUID, "the uuid of a mach-o file was read wrong")
        binary.write_bytes(b"\xca\xfe\xba\xbe" + b"\0" * 60)
        try:
            archive_symbols.binary_uuid(binary)
            failures.append("a universal binary was given a single uuid")
        except SystemExit:
            pass
        binary.write_bytes(b"short")
        try:
            archive_symbols.binary_uuid(binary)
            failures.append("a truncated file was accepted")
        except SystemExit:
            pass

    for failure in failures:
        print("FAIL:", failure)
    if failures:
        sys.exit(1)
    print("ips: ok")


if __name__ == "__main__":
    main()
