from pathlib import Path
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    font = Path(sys.argv[2]).resolve()
    arabic_font = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix="yozora-text-") as directory:
        root = Path(directory)

        def run(text, name, source=font):
            return subprocess.run([binary, str(source), text, str(root / name)],
                                  capture_output=True, timeout=15)

        def success(text, name, source=font):
            result = run(text, name, source)
            assert result.returncode == 0, (text, result.stderr)
            assert not result.stderr, result.stderr
            data = json.loads(result.stdout)
            assert data["glyphs"] == len(data["run"]) > 0
            image = root / name
            pixels = image.read_bytes()
            header = b"P5\n1024 128\n255\n"
            assert pixels.startswith(header) and len(pixels) == len(header) + 1024 * 128
            assert any(value != 255 for value in pixels[len(header):])
            assert data["ink"] > 0
            assert stat.S_IMODE(image.stat().st_mode) == 0o600
            return data, pixels

        success("Yozora 123", "latin.pgm")
        composed, image = success("é", "composed.pgm")
        decomposed, other = success("e\u0301", "decomposed.pgm")
        assert composed["run"] == decomposed["run"] and image == other
        ligature, _ = success("ffi", "ligature.pgm")
        assert ligature["glyphs"] < 3, ligature
        arabic, _ = success("مرحبا بالعالم", "arabic.pgm", arabic_font)
        assert arabic["run"][0][1] > arabic["run"][-1][1], arabic
        for index, text in enumerate(["", "a\nb", "a\tb", "\x7f", "a" * 4097, b"\xff"]):
            result = run(text, f"invalid-{index}.pgm")
            assert result.returncode == 2 and result.stderr and not result.stdout, result
            assert not (root / f"invalid-{index}.pgm").exists()
        for index, text in enumerate(["W" * 200, "a" * 4096, "灯り"]):
            result = run(text, f"unsupported-{index}.pgm")
            assert result.returncode == 1 and result.stderr and not result.stdout, result
            assert not (root / f"unsupported-{index}.pgm").exists()
        empty = root / "empty.ttf"
        empty.touch()
        malformed = root / "malformed.ttf"
        malformed.write_bytes(b"invalid font")
        enlarged = root / "enlarged.ttf"
        data = bytearray(font.read_bytes())
        tables = struct.unpack_from(">H", data, 4)[0]
        for index in range(tables):
            tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * index)
            if tag == b"head":
                struct.pack_into(">H", data, offset + 18, 16)
                break
        else:
            raise AssertionError("fixture font has no head table")
        enlarged.write_bytes(data)
        result = run("A", "enlarged.pgm", enlarged)
        assert result.returncode == 1 and b"outline exceeds" in result.stderr, result
        assert not (root / "enlarged.pgm").exists()
        oversized = root / "oversized.ttf"
        with oversized.open("wb") as stream:
            stream.truncate(32 * 1024 * 1024 + 1)
        fifo = root / "pipe"
        os.mkfifo(fifo)
        for index, (source, code) in enumerate([(empty, 2), (malformed, 1), (oversized, 2),
                                              (fifo, 2), (root, 2), (root / "missing", 2)]):
            result = run("Yozora", f"font-{index}.pgm", source)
            assert result.returncode == code and result.stderr and not result.stdout, result
            assert not (root / f"font-{index}.pgm").exists()
        preserved = root / "preserved.pgm"
        preserved.write_bytes(b"keep this file")
        linked = root / "linked.pgm"
        linked.symlink_to(preserved)
        for name in [preserved.name, linked.name, "missing/output.pgm"]:
            result = run("Yozora", name)
            assert result.returncode == 1 and result.stderr and not result.stdout, result
            assert preserved.read_bytes() == b"keep this file"
        result = subprocess.run([binary], capture_output=True, timeout=15)
        assert result.returncode == 2 and b"usage:" in result.stderr
    print("text: Latin and Arabic shaping, ligatures, ICU composition, pixels, bounds and output safety passed")


if __name__ == "__main__":
    main()
