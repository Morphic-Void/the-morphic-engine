# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Exercise Host fatal clock/deadline termination in isolated test processes."""

import argparse
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    args = parser.parse_args()
    for mode in ("start-clock", "read-clock", "deadline"):
        started = time.monotonic()
        result = subprocess.run(
            [str(args.tests.resolve()), f"--host-fatal-{mode}", f"--log-tag=endpoint-fatal-{mode}"],
            capture_output=True, text=True, timeout=15,
        )
        elapsed = time.monotonic() - started
        expected = 0xC0000602 if sys.platform == "win32" else -6
        if result.returncode != expected:
            raise RuntimeError(f"{mode}: exit {result.returncode}, expected {expected}: {result.stderr}")
        if ("FATAL PROBE ARMED" not in result.stdout or "FATAL PROBE LIVE EXECUTION RETAINED" not in result.stdout
                or "FATAL PROBE UNWOUND" in result.stdout):
            raise RuntimeError(f"{mode}: missing execution or unsafe unwinding: {result.stdout}")
        if mode == "deadline" and not 9.5 <= elapsed <= 14.0:
            raise RuntimeError(f"{mode}: deadline was shortened or extended: {elapsed:.3f}s")
        if mode != "deadline" and elapsed > 4.0:
            raise RuntimeError(f"{mode}: clock failure did not terminate promptly: {elapsed:.3f}s")
        print(f"PASS fatal {mode}: {elapsed:.3f}s", flush=True)


if __name__ == "__main__":
    main()
