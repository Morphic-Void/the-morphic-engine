Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Windows continuous integration

The workflow in `.github/workflows/windows-ci.yml` runs on pushes, pull requests,
and manual dispatch. It builds `MorphicEngine.sln` on GitHub-hosted
`windows-2022` runners using Visual Studio 2022 and the projects' v143 toolset.
Each run has four independent jobs: Debug and Release on x64 and x86.
One failed job does not cancel the other configurations. A newer run for the
same workflow, event, and ref cancels an older run still in progress.

Each job checks out the repository and its submodules, verifies line endings,
builds the solution, and runs `MorphicTests.exe -t1` and the DLL lifecycle harness.
The existing `MorphicPolicy.targets` integration builds and runs `MorphicPolicyValidator`
before compilation of consuming projects. Policy errors fail the build;
the workflow does not bypass them. Test failures also fail the job.

The solution platform named `x86` maps to project platform `Win32`, so its test
executable is loaded from `build/bin/Win32/<configuration>`. The x64 jobs use
`build/bin/x64/<configuration>`. Tests run from the repository root and write
their output beneath `build/ci/tests`.

Each job builds the four standalone lifecycle fixture roles using the MSBuild
selected by CI, reusing the solution build through `-SkipSolutionBuild`. It runs
all 26 lifecycle cases with one requested Host worker, then reuses those binaries
with `-SkipBuild` for two requested workers. The Host may reduce the latter to
one worker according to its hardware-thread policy; this does not guarantee a
dedicated conditioning worker on every runner. Cases include replacement,
dependent-asset disposal, startup failures, rendering drain and normal
Host/Executive/Rendering acceptance.

Every job attempts to upload a separate diagnostics artifact, including after
failure. It contains the MSBuild text and binary logs, test console output,
test logs and output files, lifecycle console output and isolated runtime logs
and test output, and policy reports that were produced. Artifacts
are retained for seven days. Failed checkout or setup steps may produce no
diagnostics artifact; their errors remain in the Actions job log.

The workflow does not run the more expensive `-t2` and `-t3` modes. Linux CI
continues to run its existing ordinary suites and texture acceptance; the
standalone DLL lifecycle harness is currently Windows-only.

## Enabling and using the workflow

Commit the workflow and this document separately from unrelated work, then
push that commit to GitHub. Only committed content available on GitHub is
built; local uncommitted changes are not included. No additional repository
secrets are needed for the current public SuiteUTF submodule. GitHub Actions
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
