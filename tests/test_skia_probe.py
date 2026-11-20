from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="yozora-skia-") as directory:
        root = Path(directory)
        fonts = root / "fonts"
        fonts.mkdir()
        for index, source in enumerate(sys.argv[2:]):
            shutil.copyfile(source, fonts / f"fixture-{index}.ttf")
        result = subprocess.run([binary, str(fonts)], capture_output=True, timeout=15)
        assert result.returncode == 0 and not result.stderr, result
        data = json.loads(result.stdout)
        assert data["raster"] and data["clip"] and data["transform"], data
        assert data["glyphs"] == 5 and data["text_pixels"] > 0, data
        empty = root / "empty"
        empty.mkdir()
        for arguments in [[], [str(empty)], [str(root / "missing")]]:
            result = subprocess.run([binary, *arguments], capture_output=True, timeout=15)
            assert result.returncode == 2 and result.stderr and not result.stdout, result
    print("skia: CPU raster pixels, clipping, transforms, FreeType fonts and missing fixtures passed")


if __name__ == "__main__":
    main()
