# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Run input acceptance through real Host and independently scheduled modules."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import platform

CASES = ("lifecycle", "pressure", "transitions", "work-pressure", "second-consumer", "shutdown-blocked",
         "ingress-loss", "acquisition-cleanup", "source-cleanup", "load-smoke", "load")


def process_cpu_seconds(process):
    if os.name != "nt":
        import resource
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        return usage.ru_utime + usage.ru_stime
    import ctypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
    kernel.GetProcessTimes.restype = ctypes.c_int
    values = [ctypes.c_uint64() for _ in range(4)]
    if not kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(value) for value in values)):
        return None
    return (values[2].value + values[3].value) / 10_000_000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--host-workers", type=int, choices=(1, 2), default=2)
    parser.add_argument("--case", choices=CASES, default="lifecycle")
    parser.add_argument("--source", choices=("synthetic", "native", "ordinary"), default="synthetic")
    parser.add_argument("--trace", choices=("on", "off"), default="on")
    parser.add_argument("--rate", type=int, choices=(120, 500, 1000), default=1000)
    parser.add_argument("--pipes", type=int, choices=(1, 4), default=4)
    parser.add_argument("--seconds", type=int, choices=(60, 120), default=60)
    args = parser.parse_args()
    if args.source != "synthetic" and args.case != "lifecycle":
        parser.error("Native/ordinary sources support the interactive lifecycle case only.")
    if args.source != "synthetic" and os.name != "nt":
        parser.error("Native observation currently requires Windows.")
    repository = Path(__file__).resolve().parents[2]
    subprocess.run([sys.executable, "-B", str(repository / "tests/input_host/check_sources.py"),
                    "--root", str(repository)], check=True)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    runtime = Path(tempfile.mkdtemp(prefix=f"{args.case}-{args.host_workers}-", dir=args.output_directory)).resolve()
    development = runtime / "development"
    development.mkdir()
    manifest = repository / "development/root-manifest.json"
    shutil.copyfile(manifest, development / "root-manifest.json")
    for root in json.loads(manifest.read_text(encoding="utf-8"))["roots"].values():
        (development / root["source"]).mkdir(parents=True, exist_ok=True)
    config = runtime / "bootstrap.cfg"
    config.write_text("executive=package:/bin/MorphicInputExecutive.${module_extension}\n"
                      "log-tag=input-host\n"
                      f"host-workers={args.host_workers}\n"
                      "log-directory=development/logical-roots/test-logs\n", encoding="utf-8")
    env = dict(os.environ, MORPHIC_INPUT_CASE=args.case, MORPHIC_INPUT_SOURCE=args.source, MORPHIC_INPUT_TRACE=args.trace,
               MORPHIC_INPUT_RATE=str(args.rate), MORPHIC_INPUT_PIPES=str(args.pipes), MORPHIC_INPUT_DURATION=str(args.seconds))
    with (runtime / "console.log").open("w", encoding="utf-8") as console:
        prior_cpu = process_cpu_seconds(None) if os.name != "nt" else 0.0
        started = time.perf_counter()
        process = subprocess.Popen([str(args.engine.resolve()), str(config)], cwd=runtime, env=env,
                                   stdout=console, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=max(120, args.seconds + 30))
        except subprocess.TimeoutExpired:
            # Do not turn a watchdog into permission to reclaim live borrows.
            raise RuntimeError(f"Watchdog expired; cleanup unconfirmed for PID {process.pid}. "
                               f"The runner did not terminate it. Inspect {runtime}") from None
        elapsed = time.perf_counter() - started
        cpu = process_cpu_seconds(process)
        if cpu is not None:
            cpu -= prior_cpu
    logs = list((development / "logical-roots/test-logs").glob("morphic_debug*.log"))
    events = "\n".join(log.read_text(encoding="utf-8") for log in logs)
    errors = [line for line in events.splitlines() if re.search(r"\[(assert|error|critical|fatal):", line)]
    passed = not result and "Input Host: acceptance passed" in events and not errors
    rows = [line.split(",") for line in (runtime / "console.log").read_text().splitlines()
            if line.startswith(("METRIC,", "WINDOW,", "PIPE,", "COUNTER,", "SOURCE,", "EXPECTED,", "DIAGNOSTIC,"))]
    report = {"source": args.source, "trace": args.trace, "workers": args.host_workers,
              "case": args.case, "engine": str(args.engine.resolve()), "rate": args.rate, "controllers": args.pipes,
              "passed": passed, "exit_code": result, "errors": errors,
              "elapsed_seconds": elapsed, "process_cpu_seconds": cpu,
              "platform": platform.platform(), "processor": platform.processor(), "logical_processors": os.cpu_count(),
              "metrics": [], "windows": [], "pipes": [], "expected_outputs": []}
    for row in rows:
        if row[0] == "METRIC":
            report["metrics"].append(dict(zip(("window", "stage", "samples", "p50_ns", "p95_ns", "p99_ns", "max_ns"),
                                               [int(row[1]), row[2], *map(int, row[3:])])))
        elif row[0] == "WINDOW":
            report["windows"].append(dict(zip(("window", "rate", "controllers", "duration_ns", "offered", "admitted",
                                                "generator_max_late_ns", "waits", "rounds", "missing", "invalid", "excluded", "stale"), map(int, row[1:]))))
        elif row[0] == "PIPE":
            report["pipes"].append(dict(zip(("pipe", "action_high_water", "text_high_water", "published_actions", "published_text",
                                              "applied", "cancellations", "action_loss", "text_loss", "ingress_loss"), map(int, row[1:]))))
        elif row[0] == "COUNTER":
            report["counter_ticks_per_second"] = int(row[1])
        elif row[0] == "EXPECTED":
            report["expected_outputs"].append(dict(zip(("window", "pipe", "expected", "traced_applied"), map(int, row[1:]))))
        elif row[0] == "DIAGNOSTIC":
            report["diagnostics"] = dict(zip(("source_failed", "clock_errors", "publication_trace_overflow",
                                                "pump_trace_overflow", "consumer_trace_overflow"), map(int, row[1:])))
        else:
            report["source_losses"] = {"feed": int(row[1]), "history": int(row[2])}
    (runtime / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not passed:
        raise RuntimeError(f"Input acceptance failed (exit {result}); inspect {runtime}")
    print(f"PASS {args.case}, workers={args.host_workers}, source={args.source}, trace={args.trace}: {runtime}", flush=True)


if __name__ == "__main__":
    main()
