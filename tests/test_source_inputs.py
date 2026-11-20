import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from fetch_darwin_sources import fetch_sources


def main():
    commit = json.loads((ROOT / "core/source.json").read_text())["upstream_commit"]
    with tempfile.TemporaryDirectory(prefix="yozora-inputs-") as directory:
        root = Path(directory)
        source = root / "source"
        source.mkdir()
        path = source / "Source/input.cpp"
        path.parent.mkdir()
        path.write_bytes(b"known source\n")
        manifest = root / "inputs.json"
        item = {"path": "Source/input.cpp", "size": 13,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        data = {"upstream_commit": commit, "files": [item]}
        manifest.write_text(json.dumps(data))
        with patch.dict(os.environ, {"YOZORA_WEBKIT_SRC": str(source), "YOZORA_FETCH_JOBS": "1"}):
            fetch_sources(manifest)
            for changed in [dict(data, upstream_commit="0" * 40),
                            dict(data, files=[dict(item, path="Source/../../escape.cpp")]),
                            dict(data, files=[dict(item, sha256="0" * 64)])]:
                manifest.write_text(json.dumps(changed))
                try:
                    fetch_sources(manifest)
                except RuntimeError:
                    assert path.read_bytes() == b"known source\n"
                    assert not (root / "escape.cpp").exists()
                else:
                    raise AssertionError("invalid supplemental input accepted")
    print("supplemental inputs: revision, path bounds and modified source rejection passed")


if __name__ == "__main__":
    main()
