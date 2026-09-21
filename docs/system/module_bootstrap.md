Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Module Bootstrap ABI Version 3

The [asynchronous module lifecycle](../modules/asynchronous_module_lifecycle.md)
is the implemented Host orchestration around this ABI. The Host I/O worker loads,
binds, installs and unloads DLLs; per-thread preparation still runs on each new
module thread. The Host starts its workers before asynchronously bootstrapping
the Executive. The ABI and installation ordering below remain unchanged.

The category-bearing type-identity transition removes component-confined
continuation types from the system identity table. Later system ordinals
therefore change and define a new DLL ABI. The exported entry point is
`morphic_module_bootstrap_v3`. Host and module exchange the same
`SAdvertisedIdentity` representation: claimed module ID, version, and inclusive
functional-major range. No version-1 compatibility structure or adapter is
retained because the DLL ABI is new and all components are rebuilt together.

`advertised_module_id` is deliberately named as such even within the advertised
identity structure: it is the component's claim and is distinct from the
ambient module identity assigned and installed later for execution.

The functional-major range describes the shared bootstrap/core protocol majors
with which each participant can interoperate. Binding requires the two ranges
to overlap and negotiates the highest common major. That selected major governs
the core function table and subsequent function queries. Host, module, and
functional majors are currently all 3.

The executive validates and installs its immutable local-type table and its
component-local erased-owner operation authority before returning bootstrap
functions. That authority combines a SYSTEM table compiled into the DLL with
the DLL's own LOCAL table. The host then exchanges numeric advertised
identities, populates the version-3 core surface, and installs services in this
order:

1. host-owned system-registry view;
2. ambient module identity;
3. module memory context;
4. debug service.

The module memory context is component-specific: its system identity must belong
to the installed ambient module. The host and executive contexts may share an
allocator, but they remain distinct attribution authorities with independent
live-allocation and byte counters. A context must be empty when installed.

Only after all four services, the advertised peer identity, negotiated
functional major, local type table, and erased-owner operation authority are
installed is the module ready for function queries or thread setup.
Thread identity, provisioning, and thread memory are installed later on each
created module thread.

Before registry installation, name lookup is unresolved and diagnostic paths
use numeric fallback. Any bootstrap failure leaves the module non-ready and
the host unloads it without starting module work.

Operation authority is installed once and has no uninstall path. Its function
pointers remain in component-local static storage and are not part of the
bootstrap ABI exchanged with the host. Shutdown first stops and joins all
module threads, which destroys module-local runtime state and deallocates their
transports. Accepted asset operations are drained; any retained assets still
flagging a dependency on the outgoing module trigger an assertion log and are
disposed of before unbinding. The host then requires the module context's allocation count and
attributed bytes both to be zero before native unload. A failed audit leaves the
DLL loaded, preserving its code and component-local operation authority.
Consequently all LOCAL owners are gone before their defining DLL and operation
authority become unavailable.
The system registry view points to immutable host static storage and remains
valid for host lifetime, including while queued host-owned debug records drain.
No local identity is transported in this phase.
