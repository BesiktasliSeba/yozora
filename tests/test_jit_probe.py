import re
import subprocess
import sys


def main():
    result = subprocess.run([sys.argv[1]], capture_output=True, text=True, timeout=30)
    lines = result.stdout.splitlines()
    verdict = lines[-1] if lines else ""
    # every strategy prints its name and one outcome line, the last line is the verdict
    names = [line for line in lines[:-1] if not line.startswith(" ")]
    assert names[:4] == ["rwx-anon", "map-jit", "reserve-commit", "write-then-exec"], result
    outcomes = [line.strip() for line in lines[:-1] if line.startswith("    ") and re.search(r"runs code|refused|killed by signal|wrong value", line)]
    assert len(outcomes) == len(names), result
    ready = verdict == "yozora-jit-probe: javascript jit can use executable memory"
    assert verdict.startswith("yozora-jit-probe: "), result
    # the exit status follows the verdict, a kernel that refuses rwx pages is a valid answer
    assert (result.returncode == 0) == ready, result
    if ready:
        assert any(line.strip() == "runs code" for line in lines), result
    print("jit probe: strategies, verdict and exit status agree")


if __name__ == "__main__":
    main()
