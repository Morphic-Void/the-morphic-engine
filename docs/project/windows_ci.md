Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Windows continuous integration

The workflow in `.github/workflows/windows-ci.yml` runs on pushes, pull requests,
and manual dispatch. It builds `MorphicEngine.sln` on GitHub-hosted
`windows-2022` runners using Visual Studio 2022 and the projects' v143 toolset.
Each run has four independent jobs: Debug and Release on x64 and x86.
Jobs have a 45-minute limit to accommodate the slower Debug Win32 suites and
fixture builds.
One failed job does not cancel the other configurations. A newer run for the
same workflow, event, and ref cancels an older run still in progress.

Each job checks out the repository and its submodules, acquires the pinned DXC
distribution, verifies line endings and the pinned Audio initializer, builds the
solution, and runs `MorphicTests.exe -t1`, the Host-worker acceptance harness,
the DLL lifecycle harness, and the Host-pump harness. The ordinary suites include
the synthetic Platform input and configuration tests and prepared-schema tests.
The existing `MorphicPolicy.targets` integration builds and runs `MorphicPolicyValidator`
before compilation of consuming projects. Policy errors fail the build;
the workflow does not bypass them. Test failures also fail the job.

Foundation (`MorphicFoundation`) and Framework shared implementations are compiled once
inside each participating executable or fixture DLL. Host-private module,
filesystem-image and thread owners are explicit Host/test sources. The shared
imports preserve this layout across both architectures and configurations.

The solution platform named `x86` maps to project platform `Win32`, so its test
executable is loaded from `build/bin/Win32/<configuration>`. The x64 jobs use
`build/bin/x64/<configuration>`. Tests run from the repository root and write
their output beneath `build/ci/tests`.

Each job builds the six standalone lifecycle fixture roles using the MSBuild
selected by CI, reusing the solution build through `-SkipSolutionBuild`. It runs
the lifecycle cases with one requested Host worker, then reuses those binaries
with `-SkipBuild` for two requested workers except on x86 Debug. The Host may
reduce the latter to one worker according to its hardware-thread policy; this does not guarantee a
dedicated conditioning worker on every runner. Cases include replacement,
dependent-asset disposal, startup failures, rendering drain, bundle ownership and
notification lifetimes, and normal Host/Executive/Rendering acceptance. Debug
bundle cases verify I/O building and conditioning-worker preparation, including
the shared-worker fallback.

Each job also builds the three Host-pump fixture roles and runs the portable
pump cases with one requested Host worker, with a second pass requesting two
workers except on x86 Debug. These cover Platform startup,
provider validation, replacement, service borrows, notifications, modal work,
deadlines, and shutdown. The visible native-window cases require an interactive
desktop and are not part of CI.

The real Host Input fixtures use the same worker-pass selection. Windows x86
Debug is a compatibility check: it runs the ordinary suites, Host-worker checks,
all lifecycle and portable pump cases, and execution fixture regressions, but
does not duplicate the lifecycle/pump/Input matrices with two requested workers.
Its Input runner receives `-CompatibilityCheck`, excluding only `load-smoke`
and `work-pressure` from the eleven synthetic cases. Other Windows jobs retain
both worker passes and all eleven Input cases. Local runners still permit full
coverage on Win32 Debug. Production deadlines and acceptance thresholds are
unchanged. The temporary ordinary-suite and lifecycle-build/test exclusions
used during Input CI diagnosis have been removed.

The Pump `cleanup-credits` case remains required on every configuration. Its
512 sequential transfer/acquisition pairs and disposal-credit setup have a fixed
20-second fixture allowance; subsequent operations retain the ordinary
10-second allowance. Progress and busy retries do not restart either deadline.
The process watchdog remains 35 seconds. Failure logs identify the cause,
operation state, elapsed time and allowance; provider checkpoints record setup
and settlement progress. Each Pump process also writes `process-result.json`,
including on failure, so its elapsed time and exit status survive checker errors.
These are fixture limits, not production service deadlines.

Every job attempts to upload a separate diagnostics artifact, including after
failure. It contains the MSBuild text and binary logs, test console output,
test logs and output files, Host-worker/lifecycle console output and isolated
runtime logs and test output, Host-pump console output and isolated runtime
diagnostics, and policy reports that were produced. Artifacts are retained for
seven days. Failed checkout or setup steps may produce no
diagnostics artifact; their errors remain in the Actions job log.

The workflow does not run the more expensive `-t2` and `-t3` modes. Linux CI
also runs ordinary suites and Host asset/texture/filesystem/bundle acceptance; the
standalone DLL lifecycle harness is currently Windows-only.

## Enabling and using the workflow

Commit the workflow and this document separately from unrelated work, then
push that commit to GitHub. Only committed content available on GitHub is
built; local uncommitted changes are not included. No additional repository
secrets are needed for the current public submodules. GitHub Actions
must be enabled and the referenced actions allowed by repository or
organization settings.

After pushing, open the repository's Actions tab and select **Windows CI**.
Inspect each job's build and test steps, or download its diagnostics artifact.
The **Run workflow** button becomes available once the workflow is on the
default branch. Branch protection and required checks are configured
separately; adding this file does not change those settings.

The actions are pinned to commit hashes. When upgrading them, verify their
upstream release and update the hash and version comment together. The runner
label selects the Windows/Visual Studio generation, but GitHub updates the
installed tools within that image over time.
