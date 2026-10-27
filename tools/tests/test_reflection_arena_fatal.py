# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Run reflection arena allocation failure cases in disposable child processes."""

import argparse
from pathlib import Path
import signal
import subprocess
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    #  Retain evidence, but never accept logs from a previous invocation.
    root = args.output_directory.resolve() / uuid.uuid4().hex
    root.mkdir(parents=True)
    print(f"Reflection arena fatal evidence: {root}")
    for mode, consumed, requested in (
        ("growth-directory", "4096", "1 x 1"),
        ("growth-block", "4096", "1 x 1"),
        ("alignment", "4095", "1 x 1"),
        ("oversized", "0", "1 x 4097"),
        ("overflow", "0", None),
    ):
        output = root / mode
        output.mkdir(exist_ok=True)
        child = subprocess.run(
            [str(binary), "-t0", f"--reflection-arena-fatal={mode}",
             f"--output-directory={output}", f"--log-tag=arena-fatal-{mode}"],
            capture_output=True, text=True, timeout=30,
        )
        (output / "stdout.txt").write_text(child.stdout, encoding="utf-8")
        (output / "stderr.txt").write_text(child.stderr, encoding="utf-8")
        # Require the intended fatal mechanism, not just any nonzero return.
        #  Windows uses RaiseFailFastException, then a TerminateProcess fallback.
        expected = (0xC0000602, 0xC0000409) if sys.platform == "win32" else (-signal.SIGABRT,)
        if child.returncode not in expected:
            raise AssertionError(f"{mode}: exit {child.returncode}, expected {expected}\n{child.stdout}\n{child.stderr}")
        records = [path.read_text(errors="replace") for path in output.rglob("*direct*") if path.is_file()]
        evidence = next((record for record in records if "SPIRV-Reflect arena allocation failed:" in record), "")
        for field in ("identity=arena-fatal-fixture", "input=20", "capacity=4096", f"consumed={consumed}",
                      "requested=", "blocks=1", "block_size=4096", "allocated=4096"):
            if field not in evidence:
                raise AssertionError(f"{mode}: missing panic evidence {field!r}")
        if requested is not None and f"requested={requested} bytes" not in evidence:
            raise AssertionError(f"{mode}: wrong requested allocation")
        print(f"Reflection arena {mode}: isolated fatal exit and diagnostic verified")


if __name__ == "__main__":
    main()
