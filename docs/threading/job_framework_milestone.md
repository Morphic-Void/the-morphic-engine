Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   job_framework_milestone.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   7 Aug 2026

# Host-Owned Job Framework Milestone

Status, 21 September 2026: this is a deferred design milestone. The implemented
[asynchronous module lifecycle](../modules/asynchronous_module_lifecycle.md)
loads and unloads DLLs through a Host worker, but does not implement the provider,
scheduler or executable-obligation framework described here. The rendering
DLL stub is now implemented as a separate bounded thread-lifecycle stage and
is accepted; it does not execute scheduled jobs.

## Purpose

This document defines the initial milestone for the host-owned job framework
required before introducing the first separate module which executes scheduled
job work.

The framework is not yet a general workflow engine or a full scheduler study.
Its purpose is to establish the host/module execution contract intentionally so
that the first module proves a designed boundary rather than creating one
accidentally.

## Scope

This milestone establishes:

- host ownership of execution policy, provisioning, tracking, and unload safety;
- explicit module and provider binding for job execution;
- data-only job routing to module-owned execution code;
- parent-operation tracking for correlation, progress, cancellation, and completion;
- deferred dependency handling through follow-up work rather than blocked runners;
- a narrow module-visible participation surface;
- a first implementation direction for unrestricted execution mode;
- an explicit prohibition on work stealing.

It does not establish:

- a general DAG or workflow engine;
- rich priority or fairness policy;
- final unrestricted-mode runner counts or final worker naming;
- cross-group load stealing or other adaptive balancing machinery;
- a final continuation representation beyond what the first module boundary proves;
- workflow-specific shader or geometry policy inside the generic framework.

## Architectural Position

The framework is owned by the host.

Modules provide bounded execution capability through explicitly bound
providers. They do not own native worker-thread policy, cross-module
execution-state tracking, or unload safety decisions.

The core design rule is:

```text
modules provide execution capability;
the host owns execution policy.
```

This split preserves module flexibility while keeping scheduling policy,
machine tuning, and teardown obligations in one place.

## Goals

The first framework milestone should satisfy the following goals:

- module-executed jobs are scheduled, tracked, and drained by host-owned code;
- jobs carry routing and provenance data, not executable identity;
- parent operations are first-class so the host can track queued, running, and
  resumable obligations;
- module unload is blocked until the host proves that no executable obligation
  remains against that module;
- the module-visible surface remains conservative and does not expose
  host-internal machinery unnecessarily;
- unrestricted mode supports large development hardware without treating the
  full hardware-thread count as a required target;
- restricted mode remains free to use a small fixed runner inventory suitable
  for the final product.

## Non-Goals

The following are explicitly outside this milestone:

- general work stealing;
- module ownership or leasing of native worker threads;
- cross-module transport of callbacks, lambdas, vptr-bearing task objects, or
  other executable identity;
- a scheduler designed around tiny fine-grained microtasks;
- automatic assumptions that more hardware threads always improve throughput;
- freezing speculative `jobs_worker_XX` identities into a public contract.

Work stealing is not merely deferred. It is intentionally excluded because the
current project does not justify the complexity, cross-group interference, or
debugging opacity that it introduces.

## Expected Workload Shape

Current shaping assumptions come from likely unrestricted-mode work such as:

- shader compilation, reflection, validation, and metadata generation;
- geometry classification, matching, reconfiguration, and stitching.

These workloads are expected to consist primarily of relatively long-running
coarse jobs rather than tiny ultra-frequent tasks. They may spawn additional
work or depend on asynchronous file and job completion, but such dependencies
should normally resume through follow-up work rather than by blocking a runner
thread for long periods.

These examples shape the framework, but the framework should not be defined in
shader-specific or geometry-specific terms.

## Runtime Model

The framework's first-cut runtime model consists of:

- `module_binding`, a host-known residency and unload-tracking record;
- `provider_binding`, a host-known execution binding exported by a module;
- `operation`, a parent record representing one overall unit of intent;
- `job`, one schedulable stage tied to an operation and provider;
- `runner_group`, a host-provisioned execution domain with its own cap and policy;
- `continuation`, a follow-up job record created when a dependency completes.

Jobs are data records. They are not self-executing objects and must not transport
executable addresses whose validity depends on a module remaining loaded.

## Host Responsibilities

The host is responsible for:

- binding and unbinding modules;
- registering and unregistering providers;
- provisioning runner groups and native worker threads;
- routing jobs to providers;
- tracking queued, running, deferred, and completed work;
- tracking per-provider and per-module executable obligations;
- enforcing global and per-group concurrency caps;
- blocking unsafe module unload;
- hiding internal scheduling machinery from modules and ordinary job clients.

The host must retain complete visibility of the mechanism because it owns both
execution policy and unload safety.

## Module Responsibilities

Participating modules are responsible for:

- exporting provider bindings through a narrow host-consumable ABI;
- executing one job stage when invoked by the host;
- emitting child jobs or follow-up jobs through host APIs when more work is required;
- reporting completion, deferral, failure, or cancellation outcomes;
- participating in orderly close and unbind by ceasing new work and draining
  obligations under host control.

Modules should not require knowledge of host-private queue mechanics, runner
inventory, wake policy, or machine-specific concurrency tuning.

## ABI Boundary

The provider boundary should remain deliberately narrow and conservative even
if both sides are implemented in C++.

The binding seam should avoid exposing:

- STL-heavy ABI types;
- exceptions as a transport contract;
- raw transported callbacks or lambdas;
- virtual task objects whose executable identity may outlive a module;
- scheduler-policy details that the host may later revise.

The key safety rule is that queued or deferred work may identify module-owned
execution capability only through host-managed provider binding records.

## Operation Tracking

Parent operations are first-class framework records.

The host requires them in order to track:

- overall intent and correlation;
- child-job ancestry;
- progress and completion state;
- cancellation and failure propagation;
- deferred dependency state;
- future rights to re-enter module code.

This is required for unload safety. A module may appear idle in the immediate
moment while still being the future execution target of deferred or resumed
work owned by a live operation.

## Dependency Handling

The default model for dependency handling is:

```text
run until dependency boundary
-> request dependency work or asynchronous service
-> record enough state to resume
-> return the runner to the host
-> post a follow-up job when the dependency completes
```

This model is preferred over keeping a runner occupied by a long blocked wait.

The initial framework need not define a rich general continuation engine.
Simple follow-up posting is sufficient for the first module boundary so long as
the host can track the associated operation and module obligations.

## Runner Groups

Runner groups are host-owned execution domains with independent policy and
concurrency limits.

They exist so that unrestricted mode can support multiple workload families
without requiring a single undifferentiated global worker pool. The first
likely shaping examples are shader-oriented and geometry-oriented lanes, but
the framework should define groups generically.

Runner groups are host-private policy objects. Modules may target groups or
submission lanes only through host-defined routing choices; they do not own the
underlying native threads.

## Unrestricted Mode Direction

The unrestricted mode should initially favour:

- persistent host-owned runners that park while idle;
- multiple runner groups where concrete workload classes justify them;
- tunable caps per group;
- conservative default concurrency for memory-bandwidth-heavy work;
- scalability to large development machines without assuming that all hardware
  threads should be active.

On a high-thread-count workstation, the framework should be able to represent a
large runner inventory if policy later justifies it, but exported interfaces
must not assume that all hardware threads are productive or desirable targets.

## Restricted Mode Direction

The restricted final-product mode should remain free to use:

- a small fixed runner inventory;
- simple and predictable admission control;
- conservative resource use;
- the same provider and operation contract as unrestricted mode.

Restricted and unrestricted modes should differ primarily in provisioning and
policy rather than in the fundamental host/module execution contract.

## Work Stealing Restriction

The first framework explicitly forbids work stealing.

This prohibition includes:

- queue stealing between runner groups;
- opportunistic migration of queued jobs between unrelated providers;
- scheduler designs whose primary balancing mechanism assumes worker theft of
  another worker's queue.

If a future need for redistribution emerges, it should be expressed through
host-directed reposting or other explicit policy rather than implicit theft.

## Unload Safety

A module is not safe to unload until the host proves all of the following:

- no queued jobs target that module's providers;
- no runner is currently executing that module's providers;
- no deferred or resumable operation state can later re-enter that
  module's providers.

Suggested unload sequence:

1. Mark the module and its providers closing.
2. Reject new submissions targeting them.
3. Prevent new resumptions into them except where an explicit grandfathering
   rule exists.
4. Drain queued and running obligations.
5. Verify that no live operation retains future re-entry rights against them.
6. Unbind providers.
7. Unload the module.

This rule is a core framework obligation rather than an optional higher-level
policy.

## Minimal Visible Surface

The first module-visible framework surface should be conservative.

Modules and ordinary job clients likely need only enough access to:

- register or unregister providers;
- submit jobs;
- emit child jobs or follow-up jobs;
- report completion, deferral, failure, or cancellation;
- participate in orderly closure.

The following should remain host-private unless later evidence proves
otherwise:

- runner-thread identities and counts;
- runner-group topology;
- queue internals;
- parking and wake policy;
- balancing and admission heuristics;
- detailed inflight accounting structures;
- machine-specific unrestricted-mode tuning.

## Initial Proof Obligations

Before the first separate module boundary is considered proven, the framework
and first participating module should demonstrate:

- provider binding and unbinding;
- host dispatch of a data-only job into module-owned execution code;
- module emission of child or follow-up work;
- parent-operation tracking across multiple jobs;
- host-side tracking of queued, running, and deferred obligations;
- unload prevention while work remains queued, running, or resumable;
- successful drain, unbind, and unload once those obligations clear.

## Open Decisions

The following remain intentionally open after this milestone definition:

- final runner-group inventory;
- final unrestricted-mode default caps;
- final continuation-record representation;
- final exported symbol names;
- final worker-thread identity naming and provisioning scheme.

These details should be fixed by concrete implementation and validation rather
than by premature commitment.

## Summary

This milestone defines the host-owned job framework as module-boundary and
residency infrastructure first, scheduler sophistication second.

The first goal is to make module-executed jobs safe, explicit, and unloadable
under host control. Once that boundary is proven, later workflow and policy
work can evolve on top of it without reopening the basic execution contract.
