from pathlib import Path
import os
import subprocess
import sys
import tempfile


def run(binary, path):
    return subprocess.run([binary, str(path)], capture_output=True, timeout=15)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    features = run(binary, Path(__file__).parent / "js/features.js")
    assert features.returncode == 0, features.stderr.decode(errors="replace")
    assert features.stdout == b"features passed\n", features.stdout
    with tempfile.TemporaryDirectory(prefix="yozora-js-") as directory:
        source = Path(directory) / "input.js"
        cases = [
            (b"1 + 2", 0, b"3\n", b""),
            ("'灯り'".encode(), 0, "灯り\n".encode(), b""),
            (b"''", 0, b"\n", b""),
            (b"", 0, b"undefined\n", b""),
            (b"throw new Error('failure')", 1, b"", b"uncaught exception: Error: failure"),
            (b"let = ;", 1, b"", b"SyntaxError"),
            (b"Promise.reject(new Error('rejection'))", 1, b"", b"unhandled promise rejection: Error: rejection"),
            (b"Promise.resolve().then(() => { throw new Error('microtask'); }); 1",
             1, b"", b"unhandled promise rejection: Error: microtask"),
            (b"(async () => { await 1; throw new Error('await'); })(); 1",
             1, b"", b"unhandled promise rejection: Error: await"),
            (b"Promise.reject('handled').catch(() => 7); 'handled'", 0, b"handled\n", b""),
            (b"({toString() { throw new Error('conversion'); }})", 1, b"", b"result conversion threw"),
            (b"'a'.repeat(65536)", 1, b"", b"result exceeds"),
            (b"while (true) {}", 124, b"", b"execution exceeded"),
            (b"function again() { Promise.resolve().then(again); } again(); 1",
             124, b"", b"execution exceeded"),
            (b"({toString() { while (true) {} }})", 124, b"", b"CPU time limit"),
            (b"({toString() { Promise.reject('conversion'); return 'value'; }})",
             1, b"value\n", b"unhandled promise rejection during result conversion: conversion"),
            (b"\xff", 2, b"", b"not valid UTF-8"),
            (b"1\0+2", 2, b"", b"NUL byte"),
            (b" " * (1024 * 1024) + b"1", 2, b"", b"exceeds 1 MiB"),
            (b" " * (1024 * 1024 - 1) + b"1", 0, b"1\n", b""),
        ]
        for text, status, output, error in cases:
            source.write_bytes(text)
            result = run(binary, source)
            assert result.returncode == status, (text[:100], result.returncode, result.stderr)
            assert result.stdout == output, (text[:100], result.stdout)
            assert error in result.stderr, (text[:100], result.stderr)
        missing = run(binary, Path(directory) / "missing.js")
        assert missing.returncode == 2 and b"open:" in missing.stderr
        non_file = run(binary, Path(directory))
        assert non_file.returncode == 2 and b"regular file" in non_file.stderr
        fifo = Path(directory) / "pipe"
        os.mkfifo(fifo)
        blocked = run(binary, fifo)
        assert blocked.returncode == 2 and b"regular file" in blocked.stderr
    print("javascriptcore: modern syntax, ICU, microtasks, exceptions, CPU timeout and input limits passed")


if __name__ == "__main__":
    main()
