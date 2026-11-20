from pathlib import Path
import os
import re
import select
import shutil
import struct
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    fixture = Path(sys.argv[2])

    def run(arguments):
        result = subprocess.run([binary, *map(str, arguments)], capture_output=True, timeout=30)
        assert result.returncode == 0 and not result.stderr, result
        return result.stdout.decode()

    with tempfile.TemporaryDirectory(prefix="yozora-fonts-") as directory:
        root = Path(directory)
        fonts = root / "good"
        fonts.mkdir()
        for index, name in enumerate(["Roboto-Regular.ttf", "Amiri-Regular.ttf", "RobotoMono.ttf"]):
            shutil.copyfile(fixture / "test/subset/data/fonts" / name, fonts / f"{index}.ttf")
        expected = "fonts: families, aliases, clusters, glyph pixels, shared catalog and ownership passed\n"
        assert run([fonts]) == expected

        snapshot = root / "snapshot"
        shutil.copytree(fonts, snapshot)
        process = subprocess.Popen([binary, "--snapshot", str(snapshot)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            assert select.select([process.stdout], [], [], 30)[0], "font snapshot did not load"
            assert process.stdout.readline() == b"ready\n"
            for path in snapshot.iterdir():
                path.write_bytes(b"")
            output, errors = process.communicate(b"x", timeout=30)
            assert process.returncode == 0 and not errors and output == b"font snapshot passed\n", (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

        def rejected(name, message, create):
            target = root / name
            target.mkdir()
            create(target)
            assert run(["--reject", target]) == f"rejected: {message}\n"

        rejected("empty", "font directory is empty", lambda p: None)
        assert run(["--reject", root / "missing"]) == "rejected: cannot open font directory\n"
        rejected("malformed", "font face count or format is invalid", lambda p: (p / "bad.ttf").write_bytes(b"broken font"))
        rejected("symlink", "cannot open font file", lambda p: (p / "link.ttf").symlink_to(fonts / "0.ttf"))
        rejected("fifo", "font file type or size is invalid", lambda p: os.mkfifo(p / "fifo.ttf"))
        rejected("subdirectory", "font file type or size is invalid", lambda p: (p / "dir.ttf").mkdir())
        rejected("empty-file", "font file type or size is invalid", lambda p: (p / "empty.ttf").touch())

        def excessive_size(path):
            with (path / "large.ttf").open("wb") as stream:
                stream.truncate(32 * 1024 * 1024 + 1)
        rejected("large", "font file type or size is invalid", excessive_size)
        rejected("many", "font file limit exceeded", lambda p: [os.link(fonts / "0.ttf", p / f"{i}.ttf") for i in range(65)])
        rejected("entries", "font file limit exceeded", lambda p: [(p / f"{i}.txt").touch() for i in range(4097)])

        def excessive_total(path):
            target = path / "0.ttf"
            shutil.copyfile(fonts / "0.ttf", target)
            with target.open("r+b") as stream:
                stream.truncate(32 * 1024 * 1024)
            for index in range(1, 3):
                os.link(target, path / f"{index}.ttf")
        rejected("total", "font file type or size is invalid", excessive_total)

        collection = root / "collection"
        collection.mkdir()
        ttc = fixture / "test/shape/data/in-house/fonts/TTC.ttc"
        shutil.copyfile(ttc, collection / "collection.ttc")
        counts = re.fullmatch(r"families=(\d+) faces=(\d+)\n", run(["--catalog", collection]))
        assert counts and int(counts[1]) > 0
        assert int(counts[2]) == struct.unpack(">I", ttc.read_bytes()[8:12])[0]

        variable = root / "variable"
        variable.mkdir()
        shutil.copyfile(fixture / "test/subset/data/fonts/Roboto-Variable.ttf", variable / "variable.ttf")
        counts = re.fullmatch(r"families=(\d+) faces=(\d+)\n", run(["--catalog", variable]))
        assert counts and int(counts[1]) > 0 and int(counts[2]) > 1
    run(["--math", Path(__file__).resolve().parents[1] / "app/fonts"])
    print("fonts: catalog, aliases, clusters, math glyphs, pixels, snapshots, TTC, variations, malformed input, bounds and cleanup passed")


if __name__ == "__main__":
    main()
