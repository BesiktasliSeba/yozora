from pathlib import Path
import subprocess
import sys


def main():
    result = subprocess.run([str(Path(sys.argv[1]).resolve())],
                            capture_output=True, timeout=30)
    expected = (b"webcore raster: native images, blend modes, sampling, recording, "
                b"layers, clip, clear, cross-thread replay and image ownership passed\n")
    assert result.returncode == 0 and not result.stderr, result
    assert result.stdout == expected, result
    print(expected.decode(), end="")


if __name__ == "__main__":
    main()
