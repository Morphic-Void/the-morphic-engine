Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   codebase-model.md  
Author: Ritchie Brannan  
Date:   17 Apr 26  

# codebase-model.md

This is a living document and will be updated as the codebase develops.

# Runtime Architecture Principles and Codebase Model

## Overview

This codebase is a low-level systems foundation.

Its current implemented centre of gravity is memory infrastructure, custom `noexcept` containers, slot-based structural machinery,
stable storage for non-trivial objects, small runtime primitives, and an intentionally limited but architecturally important debug layer.

The codebase does not aim to reproduce the standard library, general-purpose framework conventions, or exception-driven ownership models.
It defines and uses a smaller set of framework-native mechanisms with explicit ownership, explicit lifetime, controlled allocation, and narrowly scoped semantics.

This document explains the architectural rules that shape the codebase, what those rules mean in practice, and how new code is expected to fit the existing model.

## Engineering principles

The codebase favors low-surface-area designs built from a small number of well-understood mechanisms.
Prefer fewer mechanisms, fewer choices, fewer ownership models, fewer dependencies, and less handwritten repetition.
Seek more capability, more reuse, more consistency, and more useful work per unit of code.

Performance is treated as an architectural property, not a late optimization pass.
Algorithms, data layouts, memory use, and interfaces should be chosen with realistic scale in mind.
Memory efficiency is part of execution efficiency; wasted memory becomes wasted locality, cache, and time.

Maintenance is paid continuously. Known incorrectness should be corrected rather than preserved.
Compatibility, history, or attachment may justify transitional mechanisms, but not preservation of known error.

The codebase is designed for small-team survivability.
Systems should be buildable, understandable, and maintainable by as few people as are actually needed.
Excess coordination cost is itself a design failure.

Dependencies are minimized, and external dependencies are treated more strictly still.
Every dependency imports assumptions, coupling, fragility, and upgrade pressure.
Prefer owned understanding over imported complexity.

Ownership should remain narrow, explicit, and simple.
Many parts of a system may observe and use; few should own.
Shared ownership should be exceptional because it blurs authority, responsibility, teardown, and mutation boundaries.

Production smoothness is preferred over local convenience.
Repeated choice is a tax. Repeated discussion is a tax. Duplicated function is a tax.
Common tasks should converge on established patterns.

The broadest viable foundation is preferred over novelty.
Baselines should favor broad compatibility across platforms, toolchains, developers, and users.
The baseline should not be raised for novelty alone.

Abstraction should express real shared structure and should be avoided where it only adds indirection or ceremony.
The level of abstraction should match the problem.

Decisions should not be made from slogans when evidence is available.
Costs are meaningful only in context: placement, frequency, utility, and lifetime all matter.

## Structural codebase rules

### No exceptions in production code

Production code does not use exceptions.

Exceptions are not part of the codebase's control-flow model, ownership model, cleanup model, or API contract model.
Internal APIs are not expected to be exception-aware, and no exception may cross subsystem or module boundaries.

If a foreign SDK or library forces exception use, that exception behavior is foreign to the codebase and must be quarantined to the smallest practical integration scope.
Foreign exceptions must be translated into explicit non-throwing failure semantics before control returns to normal engine code.

### Constrained STL use in production code

Standard-library facilities are assessed against exception, allocation and portability constraints, not rejected merely because they are STL.
They must not introduce exceptions, allocate outside Morphic's memory system, or make required behaviour materially dependent on the library vendor or platform.
Framework-native containers, views, ownership holders, and low-level primitives remain the intended vocabulary.

A non-allocating, caller-buffer-based facility such as `std::to_chars` may be suitable after its specified behaviour and availability on supported toolchains have been checked.
New production code must not silently introduce foreign ownership, allocation or exception expectations.

Similarity of shape to familiar STL types does not imply STL semantics.

### Allocation is explicit and host-shaped

Normal dynamic storage is obtained through the framework memory layer and its installed allocator path.

Direct `new` and `delete` are not part of ordinary codebase ownership.
The base allocator may itself use `operator new`, but that is an implementation detail, not part of the ownership contract.

Foreign allocation semantics forced by SDKs are treated as foreign.
They must be locally contained and must not redefine the general allocation model of the codebase.

### Ownership is explicit and narrow

Owning types are move-only by default.

Observation and use are separated from ownership wherever practical.
Many parts of the system may hold views, endpoints, or access handles; that does not imply lifetime authority.

Multi-ownership is avoided.
Where users are not appropriate owners, lifetime authority should remain external and explicit, typically through a host-controlled registry or another explicit owning structure.

### Similar shape does not imply shared semantics

Types with overlapping purpose may still differ materially in guarantees, invalidation rules, relocation behavior, or lifetime semantics.

Contributors must read the documented contract of the actual type being used rather than projecting familiar behavior onto it.

## Current codebase scope

The current codebase consists primarily of:

- memory infrastructure
- `noexcept` container families for POD, non-POD, byte, and string-oriented storage
- slot-based structural machinery for ordered and unordered occupancy and ordering models
- validation and integrity support
- low-level bit and encoding support
- basic file load/save support
- basic log-file support
- a small set of SPSC transports
- a currently thin debug layer that is expected to expand
- a small number of forward foundational carryovers for higher layers, including spatial codes and float-16 support

This is a foundational systems layer, not yet a broad application or engine feature layer.

## Memory and storage model

### Memory substrate

The memory layer is mechanical.

It defines:

- allocation limits
- growth policies
- alignment policy
- allocator installation and routing
- raw byte allocation and deallocation
- trivially-copyable typed allocation and deallocation
- allocator-regime transition rules

It does not define higher-level ownership meaning beyond raw storage control, and it does not define non-trivial object lifetime semantics.

### Domain limits

The memory substrate uses a deliberate 32-bit element-domain cap.
This is an architectural choice.
It reflects the intended practical scale and locality-oriented design baseline of the codebase rather than theoretical maximum address-space modeling.

### Growth and alignment policy

Growth and alignment are not left as accidental local choices.
The substrate provides shared policy vocabulary so higher structures can converge on common behavior rather than repeatedly inventing local growth rules.

### Allocator installation and routing

The memory substrate supports a replaceable allocator interface and DLL-spanning allocation routing when a shared allocator is installed.

Allocator replacement is coordinated.
It is treated as a regime change and is rejected while incompatible live allocations remain.
Allocation enable/disable is intended primarily for test/debug control and affects allocation only; deallocation remains available.

## Memory ownership and view model

### Tokens own storage

Memory tokens own raw storage.

They manage address and alignment state, but they do not by themselves define element count semantics, object construction semantics, or higher container meaning.

### Views do not own

Memory views are lightweight non-owning access forms.

They describe address, alignment, and reinterpretation compatibility only.
They do not track extent unless a higher layer adds that meaning.

### Byte form is primary

Byte primitives define the base ownership and alignment model.

Typed memory primitives are reinterpretation layers over compatible storage.
Adoption functions define the checked crossing points between byte and typed forms.

### POD-only typed memory layer

Typed memory primitives apply only to tightly packed trivially-copyable `T[]` storage.

They do not perform construction, destruction, or non-trivial relocation.
Element count remains external.
Non-trivial object lifetime semantics belong to higher layers.

### Invariant and observation model

Memory primitives recognize canonical ready and canonical empty states, and they also acknowledge broken states.

Ordinary observers are fail-safe and collapse broken states to canonical empty or not-ready behavior where practical.
Validity observers expose invariant status directly.

This is deliberate.
It allows routine observation to remain safe without pretending that structural invalidity is acceptable or nonexistent.

### Alignment is part of the contract

Alignment is treated as meaningful state, not as incidental bookkeeping.

Owning byte tokens store normalized alignment intent.
Views report guaranteed alignment of the current address.
Subviews may reduce guaranteed alignment according to offset.
Typed adoption requires alignment compatibility.

## Container model

### Framework-native containers

The container layer is framework-specific.

Some container families overlap partially with familiar standard-library shapes, but they exist to satisfy this codebase's constraints:

- `noexcept` operation
- explicit ownership
- explicit lifetime boundaries
- controlled allocation
- low-surface-area mechanisms
- explicit invariants at public boundaries

They should not be treated as drop-in replacements for STL categories.

### POD and non-POD are intentionally separate

POD and non-POD handling are separated as an architectural choice.

For trivially-copyable packed storage, containers can use bytewise operations and do not need to model construction, destruction, or non-trivial relocation.

For non-trivial object types, object lifetime is explicit and higher-layer containers must manage it directly.

The codebase does not blur these models for convenience.

### Move-only ownership by default

Owning containers are move-only by default.

This keeps ownership explicit and avoids casual copying of lifetime-bearing structures.
Value-shaped transfer remains available through move semantics, but duplication of owning state is not assumed to be a normal operation.

### Owning containers commonly provide view forms

Where useful, owning containers provide lightweight non-owning mutable and const views.

This follows the broader codebase rule that observation should be cheap and explicit while ownership remains narrow.

## POD container model

POD containers manage contiguous tightly packed storage for trivially-copyable element types.

They do not construct or destroy elements and do not support non-trivial relocation semantics.

Operations are expressed in terms appropriate to packed trivial storage, including explicit zeroed and uninitialized growth where that is useful.

### Logical range versus spare capacity

The logical element range is semantically meaningful. Spare capacity is not.

Reallocation preserves only the logical prefix range that the operation declares survivable.
Spare capacity is storage detail and must not be treated as preserved semantic state.

### Explicit operation naming

The POD layer prefers narrow explicit operations over policy-heavy general ones.
For example, preserving order versus not preserving order is typically expressed through distinct operations rather than hidden policy flags.

### Fail-safe out-of-bounds behavior

Invalid element access is a hard assertion and then degrades to a deterministic last-gasp fallback form rather than exposing undefined ownership behavior through the public interface.

This does not make invalid access acceptable. It preserves debuggability and fail-safe observation under misuse.

## Stable non-POD collection model

For non-trivial object types, the framework separates:

- stable object backing
- slot-based public mutation identity
- key-based ordering metadata
- trivially relocatable sidecar metadata where appropriate

This avoids conflating object address stability with ordering metadata or public slot identity.

### Stable storage

Constructed non-trivial objects reside in stable storage.
Metadata reshaping operations such as sorting and packing may remap slots, metadata, and keys without relocating already-constructed objects.

### Slot-based identity

Some collections expose slot identity during mutation.
Slot identity is not automatically a permanent stable handle and must not be assumed to survive metadata-compacting operations unless explicitly documented.

### Explicit non-trivial lifetime

Construction and destruction of non-trivial objects are explicit at the container layer that owns non-trivial semantics.
Lower memory and POD layers do not assume those responsibilities.

### Structural extension through narrow callbacks

Reusable slot-management cores may delegate payload movement, reserve-side growth, and ordering comparison through narrow callbacks.
This allows shared structural machinery to remain centralized while dependent sidecar behavior stays local to the derived container.

## Slot and structural machinery

Ordered and unordered slot-management layers are structural machinery, not just specialized containers.

They manage occupancy, ordering, empty-slot handling, and related metadata structures on which higher containers build.
They exist to factor recurring structural problems into a small number of well-understood mechanisms.

Rank maps and permutation validation belong to this same structural tier.

Permutation validation is used for deep integrity auditing of container and mapping structure. It is not generic algorithm baggage.

## Ownership vocabulary

The codebase intentionally uses a small number of ownership-bearing framework primitives.

### Single-object ownership

`TInstance<T>` is the framework's unique move-only owner for a single constructed object.

Its narrowness is deliberate. It does not generalize to arrays, custom deleters, ownership release, or exposed allocated-but-unconstructed state.
Publicly, non-empty implies one live object exists.

### Shared infrastructure ownership without multi-ownership

Where visible participants are users but not natural owners, lifetime authority should remain external.

A host-controlled registry may own shared infrastructure objects such as transports or services whose role-specific users must not also become lifetime authorities.

Endpoints and views are users, not owners.

## Registry role

The registry is not intended as a general service locator or a global pointer bag.

Its role is to act as host-owned lifetime authority and shutdown coordinator for selected shared infrastructure whose users are not appropriate owners.

This includes cases where:

- multiple role-specific users exist
- none of those users should own destruction authority
- orderly teardown must be coordinated externally

The registry coordinates lifetime and destruction. It does not replace local ownership structure inside every subsystem.

## Failure, assertion, and debug model

### Explicit failure over exceptions

Operations signal failure through explicit state and return values rather than exception propagation.

Type requirements such as nothrow destructibility or nothrow constructibility are part of the contract of many framework types.

### Assertions and fail-safe observation

Hard assertions are used to expose invariant failure or contract misuse.

Where practical, public observation paths remain fail-safe and return canonical empty or last-gasp fallback forms rather than amplifying structural breakage through undefined behavior.

### Debug core must remain non-allocating

The debug layer is currently limited relative to the proof-of-concept codebase, but one hard requirement already exists: the debug core must be usable without requiring allocation.

This is structural, not optional. Debug paths must remain viable when allocation failure or allocator corruption is itself part of the fault surface.

### Current debug status

The current debug layer is intentionally incomplete and does not represent the full intended diagnostic model.
It is expected to expand, but some of that work depends on planned foundational and threading-related work.

## Runtime primitives and support layers

### File and log support

Basic file load/save and basic log-file support are runtime primitives. They are deliberately simple and foundational.

### SPSC transports

The SPSC transports are shared infrastructure, not ownership models.

They are examples of the broader rule that producer and consumer endpoints are users of shared machinery, not automatically owners.
Their lifetime should remain explicit and externally coordinated where appropriate.

### Bit operations and encoding support

Bitwise operations are substrate support, especially for alignment, growth policy, allocation math, and representation-level work.

Spatial codes and float-16 support are early low-level carryovers included because they belong to anticipated higher layers and memory-locality work.
Their presence should not be misread as random utility accumulation.

## Module and DLL model

### DLLs are internal module seams

DLLs and the executable are treated as one tightly coupled package build produced by the same toolchain and settings.

The DLL boundary is an internal modular isolation seam, not a stable external plugin ABI.

### Boundary rules

Across module boundaries:

- POD exchange is the default and preferred form
- declared interfaces may cross where explicitly bounded by module lifetime
- explicitly designed framework-native ownership carriers may cross where their allocation, lifetime, and failure semantics are controlled and documented
- arbitrary non-trivial engine ownership types must not casually cross the boundary
- no exceptions may cross the boundary
- allocator policy remains host-owned
- constrained STL use does not introduce vendor-dependent types or ownership across the boundary

### Composition root

The executable is the composition root.

It owns allocator installation, registry installation, module loading, module coordination, teardown, and reload orchestration.

### Reload model

Reload is orderly teardown and reconstruction, not in-place hot patching.

Reload is valid only when the relevant module contract remains compatible.
All interfaces and dependent assets must be destroyed or made unreachable before module unload.

### Platform and renderer coordination

Platform-specific windowing, pump, dialog, and native event collection belong naturally to platform abstraction modules.

Renderer and platform modules should not own each other's lifetimes.
The executable should mediate presentation descriptors and module coordination so that native presentation state can be withdrawn and rebuilt under host control.

## Foreign dependency containment

Foreign semantics must be quarantined.

### Foreign allocation

If an external SDK imposes its own allocation and destruction rules, those rules remain local to the integration scope.
Foreign lifetime should be adapted and terminated before it can leak into the wider engine ownership model.

### Foreign exceptions

If an external SDK uses exceptions, that exception behavior remains foreign.
It must be translated to explicit non-throwing failure semantics before control returns to normal engine code.

### Foreign semantics do not cross internal boundaries

Foreign allocation and exception semantics must not cross ordinary engine ownership boundaries or DLL seams.

## Guidance for adding new code

New code should:

- prefer existing framework mechanisms before inventing a new one
- preserve the POD versus non-POD boundary
- keep ownership explicit and move-only where ownership is required
- provide non-owning views where observation is common and ownership is not needed
- keep allocation and failure behavior explicit
- respect the existing memory substrate and allocator routing model
- use abstraction only where it captures real shared structure
- keep local convenience subordinate to codebase consistency
- document non-obvious invariants, semantic limits, and non-goals

If a design appears to require shared ownership, exception propagation, STL containers, or hidden cross-boundary lifetime, the design should be reconsidered before implementation.

## Prohibited patterns

The following are prohibited unless the architecture is explicitly revised:

- introducing exceptions into ordinary production-code control flow
- allowing foreign exceptions to propagate into general engine code
- using STL containers or ownership forms in production code
- introducing casual direct `new` or `delete` as an ownership model
- treating users, views, or endpoints as implicit owners
- allowing foreign allocation semantics to cross normal codebase boundaries
- relying on spare capacity as semantic state
- assuming STL semantics from similarly shaped framework types
- conflating slot identity with stable object identity where documentation says otherwise
- using the registry as a general service locator or vague global object store
- unloading a module while live interfaces or dependent assets from that module remain reachable

## Transitional reality

The codebase is still under active cleanup and consolidation.

Some legacy comments, naming residue, and proof-of-concept carryovers remain.
These should be interpreted in light of the architectural model described here.
Where code and intent diverge, the correct direction is toward the model described here,
not toward preserving known architectural residue.
