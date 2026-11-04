# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Portable real-module pump lifecycle acceptance, using a fresh runtime per case."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


REJECTED = ("bad-size", "bad-version", "bad-flags", "bad-callback", "mixed",
            "bad-provider-size", "bad-provider-version", "bad-provider-flags", "bad-provider-count",
            "bad-provider-null", "bad-provider-contract", "bad-provider-duplicate", "bad-provider-query")
CASES = ("normal", "empty-provider") + REJECTED + (
         "activation-failed", "activation-null", "invalid-prefix", "pump-failed",
         "deactivate-failed", "shutdown-pending", "replacement", "inbox-full", "emergency",
         "cleanup-credits", "threaded-credits", "activation-timeout", "stop-timeout",
         "borrow-normal", "borrow-cancel", "borrow-abandoned", "borrow-shutdown", "borrow-provider-failed",
         "notify-normal", "notify-cancel", "notify-abandoned", "notify-replacement", "notify-shutdown",
         "modal-normal", "modal-full", "modal-shutdown", "modal-activation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--host-workers", type=int, choices=(1, 2), default=2)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=CASES)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest = repository / "development/root-manifest.json"
    for case in args.cases:
        runtime = Path(tempfile.mkdtemp(prefix=f"{case}-{args.host_workers}-", dir=args.output_directory)).resolve()
        development = runtime / "development"
        development.mkdir()
        shutil.copyfile(manifest, development / "root-manifest.json")
        for root in json.loads(manifest.read_text(encoding="utf-8"))["roots"].values():
            (development / root["source"]).mkdir(parents=True, exist_ok=True)
        config = runtime / "bootstrap.cfg"
        config.write_text("executive=package:/bin/MorphicPumpExecutive.${module_extension}\n"
                          "log-tag=pump\n"
                          f"host-workers={args.host_workers}\n"
                          "log-directory=development/logical-roots/test-logs\n", encoding="utf-8")
        env = dict(os.environ, MORPHIC_PUMP_CASE=case)
        started = time.monotonic()
        with (runtime / "console.log").open("w", encoding="utf-8") as console:
            result = subprocess.run([str(args.engine.resolve()), str(config)], cwd=runtime, env=env,
                                    stdout=console, stderr=subprocess.STDOUT, timeout=35)
        elapsed = time.monotonic() - started
        logs = list((development / "logical-roots/test-logs").glob("morphic_debug*.log"))
        events = "\n".join(log.read_text(encoding="utf-8") for log in logs)
        expected_exit = 1 if case in ("deactivate-failed", "emergency", "stop-timeout") else 0
        if result.returncode != expected_exit or f"Pump fixture: {case} passed" not in events or "Pump fixture failed" in events:
            raise RuntimeError(f"{case}: exit {result.returncode}, expected {expected_exit}; inspect {runtime}")
        for line in events.splitlines():
            if not re.search(r"\[(assert|error|critical|fatal):", line):
                continue
            if case == "deactivate-failed" and re.search(r"\[host\.cpp:\d+\] Module shutdown failed, status 9$", line):
                continue
            if case == "stop-timeout" and "Host: shutdown grace expired; continuing progress with live resources retained" in line:
                continue
            raise RuntimeError(f"{case}: unexpected diagnostic {line}; inspect {runtime}")
        if case not in REJECTED + ("activation-null",):
            for marker in ("activation context and foreign-thread rejection passed", "begin stop", "stopped after continued progress"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
            terminal = "quiescent state retained" if case == "deactivate-failed" else "deactivated"
            if terminal not in events:
                raise RuntimeError(f"{case}: missing {terminal}; inspect {runtime}")
        if case == "shutdown-pending" and "cleanup reply received after Executive exit request" not in events:
            raise RuntimeError(f"Shutdown progress missing; inspect {runtime}")
        if case == "replacement" and "independent provider survived Executive replacement" not in events:
            raise RuntimeError(f"Replacement did not preserve provider; inspect {runtime}")
        if case in ("inbox-full", "emergency") and "full inbox and outbound backpressure established" not in events:
            raise RuntimeError(f"Backpressure not reached; inspect {runtime}")
        required = {
            "cleanup-credits": ("512 disposal credits retained by live acquisitions",
                                "zero-reply completion admitted with 512 reserved replies",
                                "512 retained disposals completed through independent cleanup credits"),
            "threaded-credits": ("threaded 600 retained disposals, releases and self-exit passed",),
            "activation-timeout": ("activation deadline initiated cooperative stop",),
            "stop-timeout": ("Host: shutdown grace expired", "live state survived shutdown grace expiry"),
        }
        for marker in required.get(case, ()):
            if marker not in events:
                raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
        if case.startswith("borrow-"):
            for marker in ("borrowed descriptor observed", "consumer quiescence received", "provider-owned storage destroyed after publisher drain"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
        if case.startswith("modal-"):
            for marker in ("nested modal maintenance preserved offered prefixes",
                           "modal budgets, deferred replies and borrowed retirement passed",
                           "modal borrow retired after outer prefix and quiescence"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
            if case == "modal-activation" and "activation excluded simulated restriction beyond five seconds" not in events:
                raise RuntimeError(f"{case}: missing eligible activation evidence; inspect {runtime}")
        if case.startswith("notify-"):
            for marker in ("registered notification published durable data before wake",
                           "final notification signal drained after consumer quiescence"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
            if case == "notify-replacement" and events.count("registered notification published durable data before wake") != 2:
                raise RuntimeError(f"{case}: replacement did not acquire a fresh registration; inspect {runtime}")
        if (case == "activation-timeout" and elapsed < 4.5) or (case == "stop-timeout" and elapsed < 10.0):
            raise RuntimeError(f"{case}: deadline was not exercised ({elapsed}s); inspect {runtime}")
        print(f"PASS {case} ({args.host_workers} requested worker(s)): {runtime}", flush=True)


if __name__ == "__main__":
    main()
