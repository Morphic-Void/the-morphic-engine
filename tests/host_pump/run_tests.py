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
import sys
import tempfile
import time


REJECTED = ("bad-size", "bad-version", "bad-flags", "bad-callback",
            "bad-provider-size", "bad-provider-version", "bad-provider-flags", "bad-provider-count",
            "bad-provider-null", "bad-provider-contract", "bad-provider-duplicate", "bad-provider-query")
CASES = ("normal", "sequence", "empty-provider", "schema-process", "platform-shell", "platform-report", "platform-report-injected",
         "platform-report-cancel", "platform-report-shutdown", "platform-report-departure") + REJECTED + (
         "mixed", "activation-failed", "activation-null", "unauthorised-native-stop", "unauthorised-native-events", "invalid-submission", "pump-failed",
         "deactivate-failed", "shutdown-pending", "replacement", "inbox-full", "emergency",
         "cleanup-credits", "threaded-credits", "activation-timeout", "stop-timeout",
         "borrow-normal", "borrow-cancel", "borrow-abandoned", "borrow-shutdown", "borrow-provider-failed",
         "notify-normal", "notify-cancel", "notify-abandoned", "notify-replacement", "notify-shutdown",
         "modal-normal", "modal-full", "modal-shutdown", "modal-activation")
NATIVE_CASES = ("platform-native-move", "platform-native-size", "platform-native-menu", "platform-native-report")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--host-workers", type=int, choices=(1, 2), default=2)
    parser.add_argument("--native", action="store_true", help="Explicit Windows visible-window/modal acceptance")
    parser.add_argument("--cases", nargs="+", choices=CASES + NATIVE_CASES)
    args = parser.parse_args()
    if args.native and sys.platform != "win32":
        parser.error("Native acceptance requires a Windows interactive desktop.")
    if args.cases is None:
        args.cases = NATIVE_CASES if args.native else CASES
    if not args.native and any(case in NATIVE_CASES for case in args.cases):
        parser.error("Visible native cases require --native.")
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
        fatal_cases = ("activation-failed", "activation-null", "activation-timeout", "unauthorised-native-stop",
                       "unauthorised-native-events", "invalid-submission", "pump-failed", "borrow-provider-failed")
        expected_exit = 1 if case in fatal_cases + ("deactivate-failed", "emergency", "stop-timeout") else 0
        if result.returncode != expected_exit or f"Pump fixture: {case} passed" not in events or "Pump fixture failed" in events:
            raise RuntimeError(f"{case}: exit {result.returncode}, expected {expected_exit}; inspect {runtime}")
        for line in events.splitlines():
            if not re.search(r"\[(assert|error|critical|fatal):", line):
                continue
            if case in fatal_cases + ("deactivate-failed",) and re.search(r"\[endpoint_runtime\.cpp:\d+\] Host: module coordinator failed, module \d+, instance \d+, endpoint \d+$", line):
                continue
            if case == "stop-timeout" and "Host: shutdown grace expired; continuing progress with live resources retained" in line:
                continue
            raise RuntimeError(f"{case}: unexpected diagnostic {line}; inspect {runtime}")
        if case not in REJECTED + ("activation-null", "schema-process", "platform-shell", "platform-report", "platform-report-injected",
                                   "platform-report-cancel", "platform-report-shutdown", "platform-report-departure") + NATIVE_CASES:
            for marker in ("activation context and foreign-thread rejection passed", "begin stop", "stopped after continued progress"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
            terminal = "quiescent state retained" if case == "deactivate-failed" else "deactivated"
            if terminal not in events:
                raise RuntimeError(f"{case}: missing {terminal}; inspect {runtime}")
        if case == "deactivate-failed":
            for marker in ("Pump provider: quiescent state retained",
                           "Pump fixture: destroy refusal unload_failed received",
                           "Host: workers and batch runners stopped; retaining the complete runtime graph"):
                if events.count(marker) != 1:
                    raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
            for marker in ("Pump provider: deactivated", "Host: Module rendering endpoint retired",
                           "Module unloaded on Host worker, mount render", "fatal shutdown could not prove quiescence"):
                if marker in events:
                    raise RuntimeError(f"{case}: unexpected {marker}; inspect {runtime}")
        if case == "schema-process":
            configurations = re.findall(
                r"\[executable:host\].* Host: Background workers ([12]), dedicated conditioning workers ([01]), reported hardware threads \d+$",
                events, re.MULTILINE)
            if len(configurations) != 1:
                raise RuntimeError(f"{case}: expected one effective Host worker configuration; inspect {runtime}")
            workers, conditioning = map(int, configurations[0])
            if workers > args.host_workers or conditioning != workers - 1:
                raise RuntimeError(f"{case}: invalid effective Host worker configuration {configurations[0]}; inspect {runtime}")
            worker = "bg_conditioning" if conditioning else "bg_file_io"
            for marker in ("Host schema request conditioning", "Host schema output reclamation"):
                placements = [line for line in events.splitlines() if marker in line]
                if len(placements) != 2 or any(f"[executable:{worker}]" not in line for line in placements):
                    raise RuntimeError(f"{case}: expected two {marker} events on {worker} "
                                       f"({workers} effective workers); inspect {runtime}")
        if case == "platform-report" and sys.platform == "win32":
            for original, historical in (("cold", "historical"), ("refresh", "historical-refresh")):
                if (runtime / f"{original}.bsd").read_bytes() != (runtime / f"{historical}.bsd").read_bytes():
                    raise RuntimeError(f"Historical {original} changed across unload/reload; inspect {runtime}")
        if case in ("platform-report", "platform-report-cancel", "platform-report-shutdown", "platform-report-departure", "platform-report-injected"):
            expected = 2 if case == "platform-report-departure" else 1
            for marker in (f"Pump fixture: {case} passed", "Host: Module executive service thread joined",
                           "Host: Module executive endpoint retired", "Module unloaded on Host worker, mount executive"):
                if events.count(marker) != expected:
                    raise RuntimeError(f"{case}: expected {expected} {marker}; inspect {runtime}")
            expected_platform = 2 if case in ("platform-report", "platform-report-injected") else 1
            if events.count("Module unloaded on Host worker, mount platform") != expected_platform:
                raise RuntimeError(f"{case}: missing Platform retirement; inspect {runtime}")
            if "Host: Module platform service thread started" in events:
                raise RuntimeError(f"{case}: unexpected Platform service thread; inspect {runtime}")
        if case == "shutdown-pending" and "cleanup reply received after Executive exit request" not in events:
            raise RuntimeError(f"Shutdown progress missing; inspect {runtime}")
        if case == "threaded-credits":
            pending = re.findall(r"Pump fixture: threaded stop received with ([1-9]\d*) results pending", events)
            if len(pending) != 1:
                raise RuntimeError(f"Stop did not retain pending credit results; inspect {runtime}")
            for marker in ("Pump fixture: threaded self-stop published with",
                           "Pump fixture: threaded 600 retained disposals, releases and self-exit passed",
                           "Host: Module executive service thread joined", "Host: Module executive endpoint retired",
                           "Module unloaded on Host worker, mount executive"):
                if events.count(marker) != 1:
                    raise RuntimeError(f"Expected one {marker}; inspect {runtime}")
        if case == "replacement" and "independent provider survived Executive replacement" not in events:
            raise RuntimeError(f"Replacement did not preserve provider; inspect {runtime}")
        if case == "schema-process":
            for marker in ("Pump fixture: schema-process passed", "Host: Module executive service thread joined",
                           "Host: Module executive endpoint retired", "Module unloaded on Host worker, mount executive",
                           "Host schema output reclamation, backing"):
                if events.count(marker) != 2:
                    raise RuntimeError(f"{case}: expected two {marker}; inspect {runtime}")
            # Release removes MV_DETAIL calls. The replacement pass above is
            # gated by both typed/correlated disposal acknowledgements, and the
            # two reclamation events remain required in every configuration.
            if "[detail:event]" in events:
                for marker in ("Host asset disposal completed at client slot 291",
                               "Host asset disposal completed at client slot 293"):
                    if events.count(marker) != 1:
                        raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
        if case in ("inbox-full", "emergency") and "bounded inbox and outbound traffic exercised" not in events:
            raise RuntimeError(f"Backpressure not reached; inspect {runtime}")
        required = {
            "schema-process": ("Replacement composed retained declarations and read remapped text and span",),
            "platform-report-cancel": ("Platform consumed the pending-capture interruption receipt",),
            "platform-report-shutdown": ("Platform consumed the pending-capture interruption receipt",),
            "platform-report-departure": ("Host published maintenance capture across requester departure",
                                          "Replacement Executive acquired current after requester departure"),
            "platform-report-injected": ("Platform independently acquired and released the Host report",),
            "sequence": ("shared sequence pointer, wrap and reload passed",),
            "platform-shell": ("Platform unsupported service commands completed and route remained usable",
                               "Platform shell loaded on demand, routed and retired without a service thread"),
            "cleanup-credits": ("512 disposal credits retained by live acquisitions",
                                "zero-reply cancellation admitted with 512 reserved replies",
                                "512 retained disposals completed through common FIFO settlement"),
            "threaded-credits": ("threaded 600 retained disposals, releases and self-exit passed",),
            "activation-timeout": ("activation deadline initiated cooperative stop",),
            "stop-timeout": ("Host: shutdown grace expired", "live state survived shutdown grace expiry"),
        }
        for marker in required.get(case, ()):
            if marker not in events:
                raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
        if case == "platform-shell":
            if "Host: Module platform service thread started" in events:
                raise RuntimeError(f"Platform shell unexpectedly started a service thread; inspect {runtime}")
            for marker in ("Host: Module platform execution activated", "Module unloaded on Host worker, mount platform"):
                if events.count(marker) != 1:
                    raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
        if case == "platform-native-report" and "maximum window response ns" not in events:
            raise RuntimeError(f"Missing live refresh response sample; inspect {runtime}")
        if case in NATIVE_CASES:
            for marker in ("Platform native modal scope authenticated by Host",
                           "Platform native shutdown retired borrow and destroyed window",
                           "Host: Module executive service thread joined", "Host: Module executive endpoint retired",
                           "Module unloaded on Host worker, mount executive", "Module unloaded on Host worker, mount platform"):
                if events.count(marker) != 1:
                    raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
        if case.startswith("borrow-"):
            for marker in ("borrowed descriptor observed", "consumer quiescence received", "provider-owned storage destroyed after publisher drain"):
                if marker not in events:
                    raise RuntimeError(f"{case}: missing {marker}; inspect {runtime}")
            if case == "borrow-abandoned":
                order = []
                for marker in ("Pump fixture: consumer departed with a live borrow",
                               "Host: Module executive service thread joined",
                               "Pump provider: consumer quiescence received",
                               "Pump provider: provider-owned storage destroyed after publisher drain",
                               "Host: Module executive endpoint retired",
                               "Module unloaded on Host worker, mount executive"):
                    values = [int(match.group(1)) for line in events.splitlines()
                              if marker in line and (match := re.match(r"\[(\d+)\]", line))]
                    if len(values) != 1:
                        raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
                    order.append(values[0])
                if order != sorted(order) or len(set(order)) != len(order):
                    raise RuntimeError(f"{case}: native consumer retirement order invalid; inspect {runtime}")
        if case.startswith("modal-"):
            for marker in ("nested modal maintenance preserved accepted command state",
                           "modal budgets, deferred replies and borrowed retirement passed",
                           "modal borrow retired after invocation and quiescence",
                           "Host: Module executive service thread joined",
                           "Host: Module executive endpoint retired",
                           "Module unloaded on Host worker, mount executive"):
                if events.count(marker) != 1:
                    raise RuntimeError(f"{case}: expected one {marker}; inspect {runtime}")
            if case == "modal-activation" and "activation excluded simulated restriction beyond five seconds" not in events:
                raise RuntimeError(f"{case}: missing eligible activation evidence; inspect {runtime}")
        if case.startswith("notify-"):
            expected = 2 if case == "notify-replacement" else 1
            for marker in ("registered notification published durable data before wake",
                           "final notification signal drained after consumer quiescence",
                           "provider-owned storage destroyed after publisher drain",
                           "Host: Module executive service thread joined",
                           "Host: Module executive endpoint retired",
                           "Module unloaded on Host worker, mount executive"):
                if events.count(marker) != expected:
                    raise RuntimeError(f"{case}: expected {expected} {marker}; inspect {runtime}")
            if case in ("notify-abandoned", "notify-replacement"):
                order = []
                for marker in ("notification consumer relinquished local access before departure",
                               "Host: Module executive service thread joined",
                               "Pump provider: consumer quiescence received",
                               "final notification signal drained after consumer quiescence",
                               "provider-owned storage destroyed after publisher drain",
                               "Host: Module executive endpoint retired",
                               "Module unloaded on Host worker, mount executive"):
                    values = [int(match.group(1)) for line in events.splitlines()
                              if marker in line and (match := re.match(r"\[(\d+)\]", line))]
                    order.append(min(values))
                if order != sorted(order) or len(set(order)) != len(order):
                    raise RuntimeError(f"{case}: notification native-retirement order invalid; inspect {runtime}")
        if (case == "activation-timeout" and elapsed < 4.5) or (case == "stop-timeout" and elapsed < 10.0):
            raise RuntimeError(f"{case}: deadline was not exercised ({elapsed}s); inspect {runtime}")
        print(f"PASS {case} ({args.host_workers} requested worker(s)): {runtime}", flush=True)


if __name__ == "__main__":
    main()
