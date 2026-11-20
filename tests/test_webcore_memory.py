from collections import Counter
import os
from pathlib import Path
import subprocess
import sys


binary = str(Path(sys.argv[1]).resolve())
result = subprocess.run([binary], capture_output=True, text=True, timeout=45,
                        env=dict(os.environ, LC_ALL="C"))
assert result.returncode == 0, result
expected = Counter()
for operation, message, count in [
    ("import mapping", "Permission denied", 1),
    ("write access denied", "Permission denied", 1),
    ("unaligned borrowed mapping", "Invalid argument", 2),
    ("allocation size", "Invalid argument", 3),
    ("invalid size", "Invalid argument", 3),
    ("backing size mismatch", "Invalid argument", 2),
    ("unsealed backing", "Invalid argument", 1),
    ("inspect descriptor", "Bad file descriptor", 1),
]:
    expected[f"yozora shared memory: {operation}: {message}"] = count
assert Counter(result.stderr.splitlines()) == expected, result
assert result.stdout.startswith("shared memory: ") and result.stdout.endswith("cleanup passed\n"), result
print(result.stdout, end="")
print("shared memory: complete boundary error messages passed")
