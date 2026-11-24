"""brings a source tree that a cmake tree was configured for up to date with a newer, already patched tree of the same project

usage: sync_core_source.py OLD_SOURCE NEW_SOURCE [--apply]
a cmake tree is tied to the path of its sources, so a new WebKit version in a new directory means a full build. this copies only the files whose
content differs into the old directory (the rest keep their time stamps, so ninja rebuilds what changed and nothing else), adds new files and
removes the ones the new tree does not have. without --apply it only reports. build with YOZORA_WEBKIT_SRC set to OLD_SOURCE afterwards
"""
import filecmp, os, shutil, sys
from pathlib import Path
if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] != '--apply'):
    raise SystemExit('usage: sync_core_source.py OLD_SOURCE NEW_SOURCE [--apply]')
old, new = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
apply = len(sys.argv) == 4
changed, added, removed, same = [], [], [], 0
new_files = set()
for root, dirs, files in os.walk(new):
    for name in files + [d for d in dirs if (Path(root) / d).is_symlink()]:
        src = Path(root) / name
        rel = src.relative_to(new)
        new_files.add(rel)
        dst = old / rel
        if src.is_symlink():
            if dst.is_symlink() and os.readlink(dst) == os.readlink(src):
                same += 1; continue
            (changed if dst.exists() or dst.is_symlink() else added).append(rel)
            if apply:
                dst.parent.mkdir(parents=True, exist_ok=True)
                if dst.exists() or dst.is_symlink(): dst.unlink()
                os.symlink(os.readlink(src), dst)
            continue
        if dst.is_file() and not dst.is_symlink() and filecmp.cmp(src, dst, shallow=False):
            same += 1; continue
        (changed if dst.exists() else added).append(rel)
        if apply:
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.is_symlink(): dst.unlink()
            shutil.copyfile(src, dst)
            shutil.copymode(src, dst)
for root, dirs, files in os.walk(old):
    for name in files:
        rel = (Path(root) / name).relative_to(old)
        if rel not in new_files:
            removed.append(rel)
            if apply: (old / rel).unlink()
print('same', same, 'changed', len(changed), 'added', len(added), 'removed', len(removed))
for label, items in (('added', added), ('removed', removed)):
    for r in items[:15]: print(' ', label, r)
print('changed in Source/WebCore:', sum(1 for r in changed if str(r).startswith('Source/WebCore')), ' JavaScriptCore:', sum(1 for r in changed if str(r).startswith('Source/JavaScriptCore')), ' WTF:', sum(1 for r in changed if str(r).startswith('Source/WTF')))
