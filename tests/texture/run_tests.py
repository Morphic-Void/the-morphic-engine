
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


def check_diagnostics(events, repository, runtime):
    expected = json.loads((repository / "tests/support/host_expected_diagnostics.json").read_text(encoding="utf-8"))
    for entry in expected:
        entry["message"] = entry["message"].replace("{runtime}", runtime.as_posix())
    passes = sorted((int(incident), scenario) for incident, scenario in re.findall(
        r"(?m)^\[(\d+)\].*\[(?:executive_thread|texture_flow_tests)\.cpp:\d+\] (?:Executive|Rendering): (\w+) passed\r?$", events))
    seen = set()
    for line in events.splitlines():
        if not re.search(r"\[(assert|error|critical|fatal):", line):
            continue
        record = re.fullmatch(r"\[(\d+)\] \[[^\]]+\] \[error:event\] \[([^:\]]+):\d+\] (.*)", line)
        if record is None:
            raise RuntimeError(f"Unexpected diagnostic: {line}")
        incident, source, message = record.groups()
        scenario = next((name for identifier, name in passes if identifier > int(incident)), None)
        matches = [entry for entry in expected if entry == dict(scenario=scenario, source=source, message=message)]
        if len(matches) != 1 or scenario in seen:
            raise RuntimeError(f"Unexpected or duplicate diagnostic: {line}")
        seen.add(scenario)
    missing = {entry["scenario"] for entry in expected} - seen
    if missing:
        raise RuntimeError(f"Missing expected diagnostics: {sorted(missing)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--host-workers", type=int, choices=(1, 2), default=2)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    args.output_directory.mkdir(parents=True, exist_ok=True)
    runtime = Path(tempfile.mkdtemp(prefix=f"host-{args.host_workers}-", dir=args.output_directory)).resolve()
    development = runtime / "development"
    development.mkdir()
    manifest = repository / "development/root-manifest.json"
    shutil.copyfile(manifest, development / "root-manifest.json")
    for root in json.loads(manifest.read_text(encoding="utf-8"))["roots"].values():
        (development / root["source"]).mkdir(parents=True, exist_ok=True)
    (development / "logical-roots/dev-workspace/acceptance").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repository / "development/logical-roots/dev-source/test_input.tga",
                    development / "logical-roots/dev-source/test_input.tga")
    config = runtime / "bootstrap.cfg"
    config.write_text("executive=package:/bin/MorphicExecutive.${module_extension}\n"
                      "log-tag=host-acceptance\n"
                      f"host-workers={args.host_workers}\n"
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
    direct_events = direct_log.read_text(encoding="utf-8")
    events = logs[0].read_text(encoding="utf-8") + direct_events
    for expected in ("Asset acceptance: 50 sequential and 32 concurrent operations passed",
                     "Rendering acceptance: 23 texture flow tests passed",
                     "Executive: Module tests completed: 23 passed, 0 failed, 0 skipped",
                     "Filesystem acceptance: 12 queued refresh, concurrent access and cache operations passed",
                     "Bundle acceptance: bake, reference, save, close and reload passed",
                     "Acquisition acceptance: shared borrows, deferred disposal, stale release and client exit passed",
                     "Rendering Basis: shut down", "Host: Module rendering service thread joined"):
        if expected not in events:
            raise RuntimeError(f"Missing {expected!r}; inspect {logs[0]}")
    expected_cases = json.loads((repository / "tests/texture/flow_cases.json").read_text(encoding="utf-8"))
    actual_cases = re.findall(
        r"\[rendering:rendering\].*\[texture_flow_tests\.cpp:\d+\] Rendering: (\w+) passed", events)
    if sorted(actual_cases) != sorted(expected_cases):
        raise RuntimeError(f"Missing, duplicate or unexpected rendering cases: {actual_cases}")
    check_diagnostics(events, repository, runtime)
    workers = re.search(r"Host: Background workers (\d+), dedicated conditioning workers (\d+), "
                        r"reported hardware threads (\d+)", events)
    if workers is None:
        raise RuntimeError(f"Missing Host worker configuration; inspect {logs[0]}")
    expected_workers = min(args.host_workers, 2 if int(workers[3]) >= 8 else 1)
    if int(workers[1]) != expected_workers or int(workers[2]) != expected_workers - 1:
        raise RuntimeError(f"Incorrect Host worker configuration; inspect {logs[0]}")
    sizing = re.search(r"Host: Batch runners (\d+)\b", events)
    if sizing is None:
        raise RuntimeError(f"Missing batch runner configuration; inspect {logs[0]}")
    queued = int(sizing[1]) != 0
    thread = r"batch_runner_\d+" if queued else "rendering"
    # Entry breadcrumbs retain info-level visibility in Release. Their producer
    # identity proves the route without requiring filtered per-operation detail.
    for operation in ("encode begin", "transcode request", "transcode begin"):
        if not re.search(rf"\[rendering:{thread}\].*Rendering Basis: {operation}", direct_events):
            raise RuntimeError(f"Incorrect {operation} thread or module context; inspect {direct_log}")
    print(f"Host acceptance passed with {expected_workers} worker(s) "
          "(50 asset cases, 23 rendering texture cases, 32 concurrent, 12 filesystem operations and bundle persistence).")


if __name__ == "__main__":
    main()
