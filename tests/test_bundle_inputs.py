"""every flat resource of the app folders is listed in the ipa tables of meow.toml

meow puts a tree under a name and never at the root of the bundle, so the icons, the interface glyphs, the textures and the launch
images are files of the ipa. a file added to those folders and left out of meow.toml would ship in no package
"""

import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIPPED = {"app/icons/yozora-source.png"}


def main():
    config = tomllib.loads((ROOT / "meow.toml").read_text())
    failures = []
    for name, ipa in config["ipa"].items():
        listed = {entry["from"] for entry in ipa.get("file", [])}
        for folder in ("app/icons", "app/icons/ui", "app/launch", "app/textures"):
            for path in sorted((ROOT / folder).iterdir()):
                relative = path.relative_to(ROOT).as_posix()
                if path.is_file() and relative not in SKIPPED and relative not in listed:
                    failures.append(f"{name}: {relative} is not listed")
        for entry in ipa.get("file", []):
            if not (ROOT / entry["from"]).exists() and not entry["from"].startswith("build/"):
                failures.append(f"{name}: {entry['from']} does not exist")
    if failures:
        sys.exit("\n".join(failures))
    print("bundle inputs: every flat resource is listed in every ipa")


if __name__ == "__main__":
    main()
