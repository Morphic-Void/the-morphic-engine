
# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# File:    run_tests.py
# Authors: Ritchie Brannan / OpenAI Codex
# Date:    10 Oct 26
#
# Run the real Host/Executive/Rendering acceptance flow in a fresh filesystem.

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    args.output_directory.mkdir(parents=True, exist_ok=True)
    runtime = Path(tempfile.mkdtemp(prefix="texture-", dir=args.output_directory)).resolve()
    development = runtime / "development"
    development.mkdir()
    manifest = repository / "development/root-manifest.json"
    shutil.copyfile(manifest, development / "root-manifest.json")
    for root in json.loads(manifest.read_text(encoding="utf-8"))["roots"].values():
        (development / root["source"]).mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repository / "development/logical-roots/dev-source/test_input.tga",
                    development / "logical-roots/dev-source/test_input.tga")
    config = runtime / "bootstrap.cfg"
    config.write_text("executive=package:/bin/MorphicExecutive.dll\n"
                      "log-tag=texture-acceptance\n"
                      "log-directory=development/logical-roots/test-logs\n",
                      encoding="utf-8")
    print(f"Acceptance diagnostics: {runtime}", flush=True)
    with (runtime / "console.log").open("w", encoding="utf-8") as console:
        result = subprocess.run([str(args.engine.resolve()), str(config)], cwd=runtime,
                                stdout=console, stderr=subprocess.STDOUT, timeout=120)
    logs = list((development / "logical-roots/test-logs").glob("morphic_debug.*.log"))
    if result.returncode != 0 or len(logs) != 1:
        raise RuntimeError(f"Host failed ({result.returncode}); inspect {runtime}")
    direct_log = logs[0].with_name(logs[0].name.replace("morphic_debug.", "morphic_debug_direct.", 1))
    events = logs[0].read_text(encoding="utf-8") + direct_log.read_text(encoding="utf-8")
    for expected in ("Asset acceptance: 71 sequential and 32 concurrent operations passed",
                     "Filesystem acceptance: 12 queued refresh, concurrent access and cache operations passed",
                     "Rendering Basis: shut down", "Host: Rendering thread joined"):
        if expected not in events:
            raise RuntimeError(f"Missing {expected!r}; inspect {logs[0]}")
    if re.search(r"\[(assert|error|critical|fatal):", events):
        raise RuntimeError(f"Unexpected diagnostics; inspect {logs[0]}")
    sizing = re.search(r"Host: Batch runners (\d+)\b", events)
    if sizing is None:
        raise RuntimeError(f"Missing batch runner configuration; inspect {logs[0]}")
    queued = int(sizing[1]) != 0
    route = "queued" if queued else "completed inline"
    thread = r"batch_runner_\d+" if queued else "rendering"
    if f"Rendering: Texture batch {route}" not in events:
        raise RuntimeError(f"Missing texture batch {route} route; inspect {logs[0]}")
    for operation in ("encode", "transcode"):
        if not re.search(rf"\[render_vulkan:{thread}\].*Rendering Basis: {operation} begin", events):
            raise RuntimeError(f"Incorrect {operation} thread or module context; inspect {direct_log}")
    print("Host texture acceptance passed (71 sequential, 32 concurrent, 12 filesystem operations).")


if __name__ == "__main__":
    main()
