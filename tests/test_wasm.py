"""the numeric instructions of the interpreter of core/wasm against V8: one function for each instruction of the specification (i32, i64, f32, f64, conversions,
sign extension, saturating truncation), called with edge values, and the results must be the same bits. a NaN is a NaN whatever its payload, a trap is a trap whatever
its message

run: python3 -B tests/test_wasm.py   (builds tools/yozora_wasm_run.c itself, needs node)
"""
import itertools
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wasm_builder import *  # noqa: F401,F403

ROOT = Path(__file__).resolve().parent.parent
LETTERS = {I32: "i32", I64: "i64", F32: "f32", F64: "f64"}

# (first opcode, last opcode, parameters, result)
GROUPS = [
    (0x45, 0x45, [I32], I32), (0x46, 0x4f, [I32, I32], I32),
    (0x50, 0x50, [I64], I32), (0x51, 0x5a, [I64, I64], I32),
    (0x5b, 0x60, [F32, F32], I32), (0x61, 0x66, [F64, F64], I32),
    (0x67, 0x69, [I32], I32), (0x6a, 0x78, [I32, I32], I32),
    (0x79, 0x7b, [I64], I64), (0x7c, 0x8a, [I64, I64], I64),
    (0x8b, 0x91, [F32], F32), (0x92, 0x98, [F32, F32], F32),
    (0x99, 0x9f, [F64], F64), (0xa0, 0xa6, [F64, F64], F64),
    (0xa7, 0xa7, [I64], I32),
    (0xa8, 0xa9, [F32], I32), (0xaa, 0xab, [F64], I32),
    (0xac, 0xad, [I32], I64),
    (0xae, 0xaf, [F32], I64), (0xb0, 0xb1, [F64], I64),
    (0xb2, 0xb3, [I32], F32), (0xb4, 0xb5, [I64], F32), (0xb6, 0xb6, [F64], F32),
    (0xb7, 0xb8, [I32], F64), (0xb9, 0xba, [I64], F64), (0xbb, 0xbb, [F32], F64),
    (0xbc, 0xbc, [F32], I32), (0xbd, 0xbd, [F64], I64), (0xbe, 0xbe, [I32], F32), (0xbf, 0xbf, [I64], F64),
    (0xc0, 0xc1, [I32], I32), (0xc2, 0xc4, [I64], I64),
]
SATURATING = [(0, [F32], I32), (1, [F32], I32), (2, [F64], I32), (3, [F64], I32), (4, [F32], I64), (5, [F32], I64), (6, [F64], I64), (7, [F64], I64)]

I32_VALUES = [0, 1, 2, 3, 7, 31, 32, 33, 255, 256, 65535, 0x12345678, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff, 0xdeadbeef]
I64_VALUES = [0, 1, 2, 63, 64, 65, 0xff, 0xffff, 0xffffffff, 0x100000000, 0x123456789abcdef0, 0x7fffffffffffffff, 0x8000000000000000, 0x8000000000000001,
              0xfffffffffffffffe, 0xffffffffffffffff, 0xdeadbeefcafef00d]
F32_BITS = [0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x3fc00000, 0x40200000, 0xc0200000, 0x00000001, 0x007fffff, 0x00800000, 0x7f7fffff,
            0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0x4effffff, 0x4f000000, 0x4f800000, 0xcf000000, 0xcf000001, 0x5f000000, 0x5f800000, 0xdf000000,
            0x3dcccccd, 0x4b000000, 0x4b7fffff, 0x4b800001, 0x2f800000]
F64_BITS = [0x0000000000000000, 0x8000000000000000, 0x3ff0000000000000, 0xbff0000000000000, 0x3fe0000000000000, 0x3ff8000000000000, 0x4004000000000000,
            0xc004000000000000, 0x0000000000000001, 0x000fffffffffffff, 0x0010000000000000, 0x7fefffffffffffff, 0xffefffffffffffff, 0x7ff0000000000000,
            0xfff0000000000000, 0x7ff8000000000000, 0x41dfffffffc00000, 0x41e0000000000000, 0x41efffffffe00000, 0x41f0000000000000, 0xc1e0000000000000,
            0xc1e0000000200000, 0x43dfffffffffffff, 0x43e0000000000000, 0x43f0000000000000, 0xc3e0000000000000, 0xc3e0000000000001, 0x3fb999999999999a,
            0x4330000000000000, 0x47efffffe0000000, 0x47f0000000000000, 0x36a0000000000000]


def operations():
    for first, last, params, result in GROUPS:
        for opcode in range(first, last + 1):
            yield f"op_{opcode:x}", params, result, bytes([opcode])
    for index, params, result in SATURATING:
        yield f"sat_{index}", params, result, b"\xfc" + u(index)


def build_module():
    module = Module()
    names = {}
    for name, params, result, code in operations():
        body_code = b"".join(LOCAL_GET(i) for i in range(len(params))) + code
        module.func(params, [result], body_code, export=name)
        names[name] = {"params": [LETTERS[p] for p in params], "results": [LETTERS[result]]}
    return module.build(), names


def values_for(kind):
    if kind == I32:
        return [f"i32:{v}" for v in I32_VALUES]
    if kind == I64:
        return [f"i64:{v}" for v in I64_VALUES]
    if kind == F32:
        return [f"f32:{v}" for v in F32_BITS]
    return [f"f64:{v}" for v in F64_BITS]


def normalize(line):
    line = line.strip()
    if line.startswith("trap") or line.startswith("error"):
        return line.split(":")[0].split()[0]
    parts = []
    for item in line.split():
        kind, _, text = item.partition(":")
        value = int(text, 16)
        if kind == "f32" and (value & 0x7f800000) == 0x7f800000 and value & 0x7fffff:
            parts.append("f32:nan")
        elif kind == "f64" and (value & 0x7ff0000000000000) == 0x7ff0000000000000 and value & 0xfffffffffffff:
            parts.append("f64:nan")
        else:
            parts.append(f"{kind}:{value:x}")
    return " ".join(parts)


def main():
    node = shutil.which("node")
    compiler = os.environ.get("HOST_CC", "cc")
    if not node:
        raise SystemExit("node is needed as the reference engine")
    with tempfile.TemporaryDirectory(prefix="yozora-wasm-") as directory:
        directory = Path(directory)
        binary, names = build_module()
        (directory / "ops.wasm").write_bytes(binary)
        runner = directory / "wasm-run"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2", f"-I{ROOT}/core/wasm", str(ROOT / "tools/yozora_wasm_run.c"),
                        str(ROOT / "core/wasm/yozora_wasm_module.c"), str(ROOT / "core/wasm/yozora_wasm_run.c"), "-lm", "-o", str(runner)], check=True)
        lines = []
        for (name, params, result, code) in operations():
            lists = [values_for(p) for p in params]
            for combination in itertools.product(*lists):
                lines.append(f"{name} {' '.join(combination)}")
        feed = "\n".join(lines) + "\n"
        engine = subprocess.run([str(runner), str(directory / "ops.wasm")], input=feed, capture_output=True, text=True, timeout=300)
        assert engine.returncode == 0, engine
        environment = dict(os.environ, WASM_SIGNATURES=json.dumps(names))
        reference = subprocess.run([node, str(ROOT / "tests/wasm_reference.cjs"), str(directory / "ops.wasm")], input=feed, capture_output=True, text=True,
                                   timeout=300, env=environment)
        assert reference.returncode == 0, reference
    got = engine.stdout.splitlines()
    want = reference.stdout.splitlines()
    assert len(got) == len(want) == len(lines), (len(got), len(want), len(lines))
    failures = 0
    for line, mine, theirs in zip(lines, got, want):
        if normalize(mine) != normalize(theirs):
            failures += 1
            if failures <= 25:
                print(f"DIFF {line}\n  v8:     {theirs}\n  engine: {mine}")
    if failures:
        raise SystemExit(f"{failures} of {len(lines)} calls differ from V8")
    print(f"wasm numeric: {len(lines)} calls of {len(names)} instructions give the bits V8 gives")


if __name__ == "__main__":
    main()
