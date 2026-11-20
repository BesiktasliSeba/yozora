import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply(source, name):
    groups = json.loads((ROOT / "core/patches/sources.json").read_text())
    apply_verified(source, groups[name], ROOT / "core/patches" / (name + ".patch"), groups)


def successors(groups, path, state):
    # the states a file reaches when later groups patch it on top of this one
    found = set()
    frontier = [state]
    while frontier:
        current = frontier.pop()
        for items in groups.values():
            for item in items:
                if item["path"] == path and item["before"] == current and item["after"] not in found:
                    found.add(item["after"])
                    frontier.append(item["after"])
    return found


def apply_verified(source, metadata, patch, groups=None):
    pending = []
    for item in metadata:
        path = source / item["path"]
        if item["before"] is None and not path.exists() and not path.is_symlink():
            actual = None
        else:
            actual = digest(path)
        # a tree that already carries later groups on the same file has this group applied too
        if actual == item["after"] or (groups and actual in successors(groups, item["path"], item["after"])):
            continue
        if actual != item["before"]:
            raise RuntimeError(f"refusing to patch modified source: {path}")
        pending.append(item)
    if not pending:
        return
    if len(pending) != len(metadata):
        raise RuntimeError(f"partially applied {patch.name} patch, inspect the source tree")
    command = ["patch", "--batch", "--fuzz=0", "--no-backup-if-mismatch", "-p1", "-d", str(source), "-i", str(patch)]
    subprocess.run([*command, "--dry-run"], check=True)
    subprocess.run(command, check=True)
    for item in metadata:
        if digest(source / item["path"]) != item["after"]:
            raise RuntimeError(f"patched checksum mismatch: {item['path']}")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise RuntimeError("usage: apply_core_patches.py SOURCE PATCH_NAME")
        apply(Path(sys.argv[1]).resolve(), sys.argv[2])
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"yozora: {error}") from error
