from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    fixture = Path(sys.argv[2]) / "test/subset/data/fonts"
    with tempfile.TemporaryDirectory(prefix="yozora-page-") as directory:
        for name in ("Roboto-Regular.ttf", "Amiri-Regular.ttf", "RobotoMono.ttf"):
            shutil.copyfile(fixture / name, Path(directory) / name)
        output = Path(directory) / "page.png"
        result = subprocess.run([binary, directory, str(output)], capture_output=True, timeout=60)
        expected_errors = (
            b"yozora kill ring: text exceeds 65536 UTF-16 units\n" * 2
            + b"yozora clipboard: rejected data exceeding item, type or byte bounds\n"
            + b"yozora clipboard: rejected custom data exceeding item, type or byte bounds\n" * 2
            + b"yozora clipboard: rejected data exceeding item, type or byte bounds\n"
        )
        assert result.returncode == 0 and result.stderr == expected_errors, result
        assert result.stdout == b"page: local HTML, CSS layout, JavaScript DOM mutation and CPU pixels passed\n", result
        assert 24 <= output.stat().st_size <= 1024 * 1024
        with output.open("rb") as image:
            header = image.read(24)
        assert header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR", header
        assert struct.unpack(">II", header[16:24]) == (320, 240), header
    print("page: local HTML, CSS layout, JavaScript DOM mutation and CPU pixels passed")


if __name__ == "__main__":
    main()
