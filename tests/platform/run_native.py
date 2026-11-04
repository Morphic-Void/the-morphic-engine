# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:    run_native.py
# Authors: Ritchie Brannan / OpenAI Codex
# Date:    04 Nov 26
#
# Runs the explicitly requested Windows native-window acceptance executable,
# capturing its output and enforcing an isolated-process timeout.

"""Explicit Windows acceptance: visible windows, native menus and pointer movement.

Never invoked by ordinary/headless CI. The harness owns its test windows and
process; a timeout terminates that isolated process without claiming safe unwind.
"""

import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Native acceptance requires a Windows interactive desktop.")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    log = args.output_directory / "native-console.log"
    with log.open("w", encoding="utf-8") as console:
        result = subprocess.run([str(args.binary.resolve()), "--platform-native",
                                 "--log-tag=platform-native", f"--output-directory={args.output_directory.resolve()}"],
                                stdout=console, stderr=subprocess.STDOUT, timeout=45)
    print(log.read_text(encoding="utf-8"), end="")
    if result.returncode:
        raise RuntimeError(f"Native acceptance failed with exit {result.returncode}; inspect {log}")


if __name__ == "__main__":
    main()
