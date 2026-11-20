from fetch_darwin_sources import fetch_sources
from fetch_core import ROOT


if __name__ == "__main__":
    try:
        fetch_sources(ROOT / "core/network-sources.json", None)
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(f"yozora: {error}") from error
