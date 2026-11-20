import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile


spec = importlib.util.spec_from_file_location(
    "apply_core_patches", Path(__file__).resolve().parent.parent / "scripts/apply_core_patches.py")
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)


def reject(source, metadata, patch):
    before = {path.name: path.read_bytes() for path in source.iterdir()}
    try:
        patches.apply_verified(source, metadata, patch)
    except (RuntimeError, subprocess.CalledProcessError):
        assert before == {path.name: path.read_bytes() for path in source.iterdir()}
        return
    raise AssertionError("invalid patch inputs were accepted")


def main():
    with tempfile.TemporaryDirectory(prefix="yozora-patch-") as directory:
        root = Path(directory)
        source = root / "source"
        source.mkdir()
        metadata = []
        changes = []
        for name in ("one.c", "two.c"):
            (source / name).write_text("old\n")
            metadata.append({"path": name,
                             "before": hashlib.sha256(b"old\n").hexdigest(),
                             "after": hashlib.sha256(b"new\n").hexdigest()})
            changes.append(f"--- a/{name}\n+++ b/{name}\n@@ -1 +1 @@\n-old\n+new\n")
        patch = root / "fixture.patch"
        patch.write_text("".join(changes))
        patches.apply_verified(source, metadata, patch)
        assert all((source / name).read_text() == "new\n" for name in ("one.c", "two.c"))
        patches.apply_verified(source, metadata, patch)
        (source / "one.c").write_text("old\n")
        reject(source, metadata, patch)
        (source / "two.c").write_text("developer change\n")
        reject(source, metadata, patch)
        (source / "two.c").write_text("old\n")
        patch.write_text("".join(changes).replace("-old", "-unexpected"))
        reject(source, metadata, patch)
    print("core patches: verified application, repeat, partial state and developer changes passed")

    with tempfile.TemporaryDirectory(prefix="yozora-port-patch-") as directory:
        root = Path(directory)
        patch = root / "new.patch"
        source = root / "source"
        source.mkdir()
        patch.write_text("--- /dev/null\n+++ b/port.c\n@@ -0,0 +1 @@\n+port\n")
        metadata = [{"path": "port.c", "before": None,
                     "after": hashlib.sha256(b"port\n").hexdigest()}]
        patches.apply_verified(source, metadata, patch)
        assert (source / "port.c").read_bytes() == b"port\n"
        patches.apply_verified(source, metadata, patch)
        (source / "port.c").write_text("developer change\n")
        reject(source, metadata, patch)
    print("core patches: new port files and existing developer changes passed")

    with tempfile.TemporaryDirectory(prefix="yozora-chain-patch-") as directory:
        root = Path(directory)
        source = root / "source"
        source.mkdir()
        states = [hashlib.sha256(text).hexdigest() for text in (b"old\n", b"mid\n", b"new\n")]
        first = [{"path": "file.c", "before": states[0], "after": states[1]}]
        second = [{"path": "file.c", "before": states[1], "after": states[2]}]
        groups = {"first": first, "second": second}
        first_patch, second_patch = root / "first.patch", root / "second.patch"
        first_patch.write_text("--- a/file.c\n+++ b/file.c\n@@ -1 +1 @@\n-old\n+mid\n")
        second_patch.write_text("--- a/file.c\n+++ b/file.c\n@@ -1 +1 @@\n-mid\n+new\n")
        (source / "file.c").write_text("old\n")
        patches.apply_verified(source, first, first_patch, groups)
        patches.apply_verified(source, second, second_patch, groups)
        # both groups were applied before, repeating the first one must not fail on the second one's result
        patches.apply_verified(source, first, first_patch, groups)
        patches.apply_verified(source, second, second_patch, groups)
        assert (source / "file.c").read_text() == "new\n"
        (source / "file.c").write_text("developer change\n")
        reject(source, first, first_patch)
        try:
            patches.apply_verified(source, first, first_patch, groups)
        except RuntimeError:
            pass
        else:
            raise AssertionError("a developer change was taken for a later group")
    print("core patches: files patched by later groups count as applied")


if __name__ == "__main__":
    main()
