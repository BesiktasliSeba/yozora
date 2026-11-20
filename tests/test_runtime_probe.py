from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    assert subprocess.run([executable], capture_output=True, timeout=10).returncode == 2
    with tempfile.TemporaryDirectory(prefix="yozora-runtime-") as directory:
        result = subprocess.run([executable, directory], capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        assert "ICU 78.3 passed" in result.stdout, result.stdout
        assert not list(Path(directory).iterdir()), "scratch files survived successful probe"
    result = subprocess.run([executable, "/missing-yozora-scratch/directory"], capture_output=True, timeout=10)
    assert result.returncode == 1 and result.stderr
    print("runtime probe: real runtime, ICU data, input errors and scratch cleanup passed")


if __name__ == "__main__":
    main()
