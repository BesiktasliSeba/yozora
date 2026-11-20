import re
import subprocess
import sys


def inspect(otool, path, system_only=False):
    for arch, target in (("armv7", "5.0"), ("arm64", "7.0")):
        commands = subprocess.check_output([otool, "-arch", arch, "-l", path], text=True)
        versions = re.findall(r"cmd LC_VERSION_MIN_IPHONEOS\s+cmdsize \d+\s+version ([\d.]+)", commands)
        if versions != [target]:
            raise RuntimeError(f"{arch}: unexpected deployment targets {versions}")
        libraries = subprocess.check_output([otool, "-arch", arch, "-L", path], text=True)
        paths = re.findall(r"^\s+(/\S+)\s+\(", libraries, re.MULTILINE)
        allowed = {"/usr/lib/libSystem.B.dylib", "/usr/lib/libsqlite3.dylib", "/usr/lib/libsqlite3.0.dylib"}
        if system_only:
            if paths != ["/usr/lib/libSystem.B.dylib"]:
                raise RuntimeError(f"{arch}: unexpected dependencies {paths}")
        elif len(paths) != 2 or set(paths) - allowed or not any("libsqlite3" in p for p in paths):
            raise RuntimeError(f"{arch}: unexpected dependencies {paths}")
        print(f"{arch}: ios {target}, dependencies {', '.join(paths)}")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] != "--system-only"):
        raise SystemExit("usage: verify_ios.py OTOOL EXECUTABLE [--system-only]")
    inspect(sys.argv[1], sys.argv[2], len(sys.argv) == 4)
