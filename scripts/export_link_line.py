#!/usr/bin/env python3
"""writes the libraries the cmake tree links into the Yozora target as a response file for meow

cmake stays the owner of the library graph (WebCore, JavaScriptCore, the dependency archives, the frameworks and
their order). meow compiles the app sources and links them, so it needs that line without running cmake's own
link of the app. usage: export_link_line.py BUILD_DIR OUTPUT
"""

import re
import shlex
import sys
from pathlib import Path


def main():
    build, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
    text = (build / "build.ninja").read_text()
    start = text.index("build bin/Yozora:")
    block = text[start:].split("\n\n", 1)[0]
    match = re.search(r"^  LINK_LIBRARIES = (.*)$", block, re.M)
    if not match:
        sys.exit("export_link_line: no LINK_LIBRARIES for the Yozora target in " + str(build / "build.ninja"))
    # the library directories cmake gives separately (-lbrotlidec and the other -l names are found through them)
    paths = re.search(r"^  LINK_PATH = (.*)$", block, re.M)
    words = shlex.split(paths.group(1)) if paths else []
    for word in shlex.split(match.group(1)):
        # an rpath names a directory of the build machine, the app links its libraries statically
        if word.startswith("-Wl,-rpath,"):
            continue
        # a library of the build tree is written relative to it
        if word.endswith(".a") and not word.startswith("/"):
            word = str(build / word)
        words.append(word)
    for word in words:
        if word.endswith(".a") and not Path(word).is_file():
            sys.exit("export_link_line: missing " + word)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(shlex.quote(word) for word in words) + "\n")


if __name__ == "__main__":
    main()
