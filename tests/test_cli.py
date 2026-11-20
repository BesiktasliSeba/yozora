import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="yozora-cli-") as directory:
    os.chmod(directory, 0o700)

    def run(*arguments, success=True):
        result = subprocess.run([binary, directory, *arguments], text=True, capture_output=True)
        assert (result.returncode == 0) == success, (arguments, result.stdout, result.stderr)
        return result

    url = "https://example.test/a?quote=';DROP%20TABLE%20history;"
    title = '明かり "Акари"\nnext\tline'
    run("visit", url, title)
    run("visit", url, title)
    history = [json.loads(line) for line in run("history").stdout.splitlines()]
    assert len(history) == 1 and history[0]["url"] == url and history[0]["title"] == title
    assert history[0]["visits"] == 2 and history[0]["time_ms"] > 0
    run("bookmark", url, title)
    run("clear-history")
    assert run("history").stdout == ""
    assert len(run("bookmarks").stdout.splitlines()) == 1
    assert run("bookmarks", "1").stdout == ""
    run("remove-bookmark", url)
    assert run("bookmarks").stdout == ""
    run("history", "-1", success=False)
    run("history", "9999999999999999999999", success=False)
    run("history", "4097", success=False)
    run("unknown", success=False)
    run("remove-history", url, success=False)
    run("visit", "https://example.test/", "x" * 512, success=False)
    assert (Path(directory) / "browser.sqlite3").stat().st_mode & 0o777 == 0o600
print("cli: real database commands, JSON escaping, persistence and input errors passed")
