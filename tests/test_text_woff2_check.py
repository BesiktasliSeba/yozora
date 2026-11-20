from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
check = root / "scripts/check_text_woff2.sh"
with tempfile.TemporaryDirectory(prefix="yozora-woff-check-") as directory:
    prefix = Path(directory)
    archive = prefix / "lib/libfreetype.a"
    archive.parent.mkdir()
    archive.write_bytes(b"fixture")
    nm = prefix / "nm"
    env = dict(os.environ, YOZORA_TEXT_NM=str(nm))
    for output, expected in [("U _BrotliDecoderDecompress", 0), ("U _FT_Init_FreeType", 1)]:
        nm.write_text("#!/usr/bin/env bash\nprintf '%s\\n' '" + output + "'\n")
        nm.chmod(0o755)
        result = subprocess.run(["bash", str(check), str(prefix)], env=env, capture_output=True)
        assert result.returncode == expected, result
        if expected:
            assert b"lacks WOFF2 support" in result.stderr
    archive.unlink()
    result = subprocess.run(["bash", str(check), str(prefix)], env=env, capture_output=True)
    assert result.returncode and b"missing" in result.stderr
print("text dependencies: missing and stale FreeType rejected, Brotli-enabled archive accepted")
