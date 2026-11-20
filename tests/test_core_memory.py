from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
result = subprocess.run([sys.argv[1], str(ROOT / 'app/fonts'), str(ROOT / 'app/ca-bundle.crt')],
                        capture_output=True, text=True, timeout=30)
assert result.returncode == 0, result
# the first warning gives back what the page does not need, the second close behind it (the memory did not
# come back) releases everything
releases = [line for line in result.stderr.splitlines() if 'memory pressure' in line]
assert len(releases) == 2, result
assert 'unused tiles and caches released' in releases[0] and 'everything released' in releases[1], result
print(result.stdout.strip())
