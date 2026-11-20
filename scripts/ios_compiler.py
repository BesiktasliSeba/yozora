#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys


def main():
    configuration = json.loads(Path(__file__).with_name("compiler.json").read_text())
    is_cxx = Path(sys.argv[0]).name == "cxx"
    command = [configuration["cxx" if is_cxx else "cc"], *configuration["flags"]]
    if is_cxx:
        command.extend(["-nostdinc++", "-isystem", configuration["headers"], "-nostdlib++", "-faligned-allocation"])
    command.extend(sys.argv[1:])
    if is_cxx and not any(arg in sys.argv[1:] for arg in ("-c", "-S", "-E", "-fsyntax-only", "--version", "-v")):
        command.extend(configuration["libraries"])
    os.execv(command[0], command)


if __name__ == "__main__":
    main()
