Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   codebase-ethos-primer.md  
Author: Ritchie Brannan  
Date:   22 Apr 26  

# Codebase Ethos Primer

This codebase is a low-level systems foundation built around a small number of explicit, framework-native mechanisms.

## Core defaults

- No exceptions in production code.
- Constrained STL use: no exceptions, allocation outside framework control, or material vendor/platform dependence; see `codebase-model.md`.
- Allocation is explicit and routed through the framework memory layer.
- Owning types are move-only by default.
- Observation/use is separated from ownership wherever practical.
- Shared ownership is exceptional and generally avoided.
- Performance, memory efficiency, and locality are architectural concerns, not later optimization passes.
- Maintenance cost, dependency cost, and coordination cost are real design costs.

## Design bias

Prefer:
- fewer mechanisms
- fewer ownership models
- fewer dependencies
- less handwritten repetition
- explicit lifetime and failure behavior
- broad compatibility baselines
- abstractions only where they capture real shared structure

Avoid:
- policy-heavy convenience
- imported complexity
- hidden ownership
- hidden lifetime authority
- novelty without clear gain
- preserving known incorrectness for the sake of history

## Memory and lifetime model

- The memory substrate is mechanical: allocation, alignment, growth policy, allocator routing.
- Raw storage ownership and object lifetime are separate concerns.
- Byte form is primary; typed memory is a reinterpretation layer over compatible storage.
- POD/trivially-copyable storage and non-trivial object lifetime are intentionally separated.
- Non-trivial construction, destruction, and relocation belong to higher layers that explicitly own those semantics.

## Ownership model

- Ownership should remain narrow, explicit, and simple.
- Many parts of the system may observe or use; few should own.
- Views and endpoints are users, not owners.
- Where users are not natural owners, lifetime authority should remain external, typically in a host-controlled registry.
- `TInstance<T>` is the canonical narrow owner for one non-trivial object.

## Container and structural model

- Framework-native containers are not STL replacements and must not be treated as such.
- Similar shape does not imply shared semantics.
- POD and non-POD paths remain distinct by design.
- Slot machinery, stable storage, rank maps, and permutation validation are structural mechanisms, not incidental utilities.
- Public boundaries should restore invariants explicitly.

## Failure and debug model

- Failure is signalled explicitly through state and return values, not exceptions.
- Hard assertions expose misuse and invariant failure.
- Public observation should remain fail-safe where practical, collapsing broken states to canonical empty/not-ready behavior rather than amplifying faults.
- The debug core must remain usable without requiring allocation.

## Module / DLL model

- DLLs are internal module seams in one tightly coupled package build.
- They are not a stable external plugin ABI.
- No exceptions cross boundaries.
- POD exchange is preferred across module boundaries.
- Non-POD exchange is allowed only through explicitly designed framework-native boundary forms with controlled ownership and lifetime semantics.
- Allocator policy remains host-owned.
- The executable is the composition root and owns allocator installation, registry installation, module coordination, teardown, and reload orchestration.

## Registry model

- The registry is host-owned lifetime authority and teardown coordinator for selected shared infrastructure.
- It is not a general service locator or vague global object store.

## Guidance for new code

New code should:
- prefer existing framework mechanisms before inventing new ones
- keep ownership explicit and move-only where ownership is required
- provide non-owning views where observation is common
- keep allocation and failure behavior explicit
- respect the POD / non-POD split
- document non-obvious invariants, semantic limits, and non-goals

## Linkage and namespace guidance

- Do not use anonymous namespaces.
- Use `static` linkage for translation-unit-local declarations only when that
  restricted linkage is independently justified.
- Introduce a named namespace only when the grouping or scope it expresses is
  independently useful; a namespace is not a substitute for `static` linkage.
- Keep internal namespace names short and related to their contents, using
  names such as `util`, `tool`, `data`, or `internal` where they are accurate.
- Do not use `detail` as a generic namespace name.

If a design appears to require:
- exceptions
- STL containers or ownership forms
- casual direct `new` / `delete`
- hidden cross-boundary lifetime
- shared ownership as a default
- registry-as-global-bag semantics

then the design should be reconsidered first.

## Reading guidance

Assume:
- low-level, substrate-oriented intent
- explicit ownership and lifetime
- no exception-aware cleanup model
- no STL semantics unless explicitly stated
- consistency and survivability matter more than local convenience
- known residue should be cleaned toward the intended model, not preserved as precedent
-
