Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:    host-schema-processing-review.md
Authors: Ritchie Brannan / OpenAI Codex
Date:    18 Nov 26

# Generic Host schema processing coordinator review

The focused stage-1 coordinator reviewed the uncommitted service against baseline
`c818202c319c542f16cd948a2818996f23ff1ac8` and the agreed contract in
[the staged design](../modules/pso_creation_stage1.md#agreed-generic-host-schema-service-contract).
The review includes the new untracked Host implementation, usage guide and
validation record, together with the tracked Foundation, Framework, Host, fixture
and build changes. The coordinator's agreement/handoff documentation changes are
separate from the implementing chat's production work.

**Disposition: no blocking findings. The generic Host schema service passes its
independent implementation review.** This is not acceptance of the outstanding
Platform capture pipeline changes or Rendering prerequisites. Changes remain
uncommitted; this review grants no permission to commit or push.

## Contract and implementation assessment

[Public messages](../../framework/assets/schema_messages.hpp) retain the agreed
owning request, explicit operations/outputs, three source slots, role selectors,
copied limits and optional deadline. The Accepted/Cancel/result path preserves
per-installation correlation and separates transport ownership from admission.
The terminal reply and copied failure body have explicit 48/32-byte assertions,
including field offsets on both supported Windows architectures.

[Admission and publication](../../host/runtime/asset_schema_process.cpp) protect
retained inputs before Accepted, reserve both replies and acquisition capacity,
and defer worker dispatch until admission publication. Four normal operation slots
are supplemented by a fixed rejection-retirement slot; a transferred rejection
does not require large normal-path destruction on the owner thread. When that slot
is occupied, existing owning transports apply backpressure.

Retained-source admission is deliberately restricted to immutable schema-document
outputs of this service. Runtime outputs, raw assets and unclassified baked assets
are not silently treated as equivalent schema sources. This boundary is documented
in the usage guide; owned authored documents supply the other input path.

[Worker conditioning](../../host/runtime/host_worker_thread.cpp) executes generic
Foundation operations and retires the envelope and source reads before completion.
Host keeps source pins and producer/client lifetime barriers until that completion
or proved terminal drain. Cancellation is separately synchronised. Inspection found
no normal-path module callback submitted as worker code, source mutation during a
worker read, or timeout-based reclamation of executing inputs.

Output backing and descriptors live outside recyclable operation slots. Success
publication protects both descriptor and complete bytes with the granted acquisition.
Disposal closes new derivations without invalidating admitted readers. Definite
non-delivery and potentially exposed poisoned publication take different retirement
paths; ambiguous exposure retains backing until explicit release or recipient,
queue and batch quiescence. Normal heavy output reclamation uses conditioning.

[Atomic Foundation processing](../../foundation/schema/document/schema_runtime.cpp)
uses one operation scope across source admission, composition/merge, resolution,
encoding/bake and independent publication allocation. The runtime branch uses the
complete schema construction path, not ordinary document encoding. The review
inspected the added traversal, compatibility and index charges and their failure
propagation. Input preservation, empty failed output and sticky shared-budget
exhaustion are covered by focused boundary regressions. The separate prepared-schema
materialisation service remains intact.

## Verification inspected

The coordinator inspected current source and the saved verification evidence rather
than repeating an already passing matrix. Final focused logs report zero failures:

| Target | Passing focused checks |
| --- | ---: |
| Windows x64 Debug | 896 |
| Windows x64 Release | 896 |
| Windows Win32 Debug | 895 |
| Linux x64 GCC Debug | 896 |

Tier-one runs are recorded for all four targets. Six real-module replacement runs
passed: Windows x64 Debug one/two workers, Win32 Debug one worker, x64 Release two
workers, and GCC Debug one/two workers. The runner checks exactly two conditioning
and two heavy-reclamation events on the selected worker. The fixture exercises
retained declaration reuse after producer unload, a fresh request sequence on the
replacement installation, complete text/span targets and acquisition/disposal.

Injected service tests separately cover invalid envelopes and sources, role and
merge conflicts, each limit, shared-work exhaustion through the final bake,
backpressure, cancellation/expiry, worker failure, poisoned publication and drain.
These injected cases are not described as observed native platform faults.

`git diff --check` passed during review. The implementation record also contains
successful build-integrated source-policy, Visual Studio filter and line-ending
checks. See [the validation record](host-schema-processing-validation.md) for exact
commands and artifact paths.

## Release rebuild limitation

The incrementally linked x64 Release test executable previously reported the
rejection slot available at saturation while the fifth request correctly received
busy. The cause was not isolated. Do not label it a proven stale-object, compiler
or source-code defect without further evidence.

The inspected clean test rebuild reports all 35,523 functions compiled without a
usable prior IPDB/IOBJ; the clean Host rebuild reports all 7,779 functions compiled.
The final focused, tier-one and two-worker replacement checks pass on those builds.
The clean rebuilds are the Release verification boundary for this review. The
earlier unexplained incremental result remains explicitly documented; recurrence
requires investigation rather than dismissal as an expected test failure.

## Remaining work and ownership

The compact Platform filter/layout, authored capture handoff into this service,
native capture execution placement and native Win32 five-second qualification are
separate follow-ups. The generic service's independent acceptance does not close
those gates or start Rendering. This review changed no production source, tests,
usage guide or validation evidence and created no commit.
