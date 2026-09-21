Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   erased_owner.md
Author: Ritchie Brannan
Date:   26 Jul 2026

# CErasedOwner

## Purpose

`CErasedOwner` is the move-only carrier for one explicitly eligible registered
payload. SYSTEM payloads are the deliberate cross-component representation.
LOCAL payloads are valid while ownership remains inside the binary component
that defines their identity and operations. The carrier is intended for
non-POD payload ownership and is distinct from `TErasedPod`, which stores
bounded trivially copyable data inline.

The carrier owns direct payload storage through `CMemoryToken`. It contains no
virtual interface, destructor pointer, operation pointer, or other executable
address that could outlive a module. The category-bearing type ID selects an
operation from the registry installed in the component executing the action.

The carrier layout is fixed at 32 bytes on x64 and 24 bytes on x86. The token
remains outside the allocation it may release.

## Registration

SYSTEM payload identity is declared in `system/system_type_ids.def` and
associated with a C++ type through `MV_REGISTER_SYSTEM_TYPE`. LOCAL identity is
declared in the defining component's local type list and associated through
`MV_REGISTER_LOCAL_TYPE`.

The owner stores and exposes the category-bearing `type_id`, matching other
erased carriers. `create<T>()` and `payload<T>()` use `k_type_id_v<T>` and are
neutral to the registered category.

Owning-erasure eligibility is a separate explicit trait declared with
`MV_REGISTER_ERASED_OWNER_PAYLOAD`. A type ID does not by itself permit a type
to be carried by `CErasedOwner`.

Eligibility declarations also generate component-local operation entries.
Each entry contains destruction, nested allocation accounting, memory-source
validation, reattribution preflight, and post-accounting context replacement.
The common factories express the no-nested-allocation case and the nested
storage-member case without duplicating operation logic.

SYSTEM operation declarations are compiled into every component that may
receive and destroy those payloads. A component separately generates the LOCAL
entries for its own eligible types; common core sources never reference those
component-local C++ definitions. Valid type registration, erased-owner
eligibility, and available runtime operations remain three distinct checks.

Eligible payloads must be nothrow default constructible, move constructible,
move assignable, and destructible. They must also fit the memory-token stride
field.

## State And Lifetime

The canonical empty state has:

- type ID zero;
- empty token storage;
- hazard mask zero.

`create<T>()` allocates and directly placement-constructs `T`. Allocation
failure returns the canonical empty state.

Move construction and move assignment transfer the token without relocating
the payload, preserving the payload address. The moved-from carrier becomes
canonical empty. Move assignment first destroys any payload already owned by
the destination.

Ownership is singular and nominal. Only the owner may mutate the payload,
attribution, or hazards; access to the payload does not independently confer
ownership or concurrent mutation authority. The carrier contains no atomics.
Transport and access structures provide cross-thread synchronization and
ownership-transfer ordering.

`destroy()` resolves the concrete destructor through the executing component's
installed operation view, destroys the payload, deallocates the token, and
restores canonical empty state. Missing or incomplete authority for a ready
owner is a critical architectural contract failure. The carrier still
deallocates its direct token and becomes canonical empty when authority is
unexpectedly unavailable, but it cannot safely invoke a missing destructor or
discover unknown nested ownership.

`payload<T>()` returns a pointer only when the registered type ID matches.

## Component Operation Authority

Each binary owns one immutable registry view with separate dense SYSTEM and
LOCAL categories. Lookup is category-directed and then indexed by the decoded
type ordinal. Empty slots distinguish an ordinary registered type from an
erased-owner-eligible type with complete operation authority.

Installation is explicit and one-shot; there is no uninstall operation. The
host installs its SYSTEM and host-LOCAL views before creating the host runtime.
A DLL installs its compiled SYSTEM view and its own LOCAL view during bootstrap,
before it can become ready or create threads. Function pointers exist only in
these component-local static tables and never enter the carrier, an ABI
structure exchanged with another component, or a transport.

Host operation authority has executable lifetime. DLL authority remains valid
until native module unload. Shutdown must stop and join module threads, destroy
module-local owners and drain/deallocate their component-local transports before
unloading the DLL. The component-specific memory context provides the final
host-visible audit: its allocation count and attributed bytes must both be zero,
or the DLL remains loaded. A LOCAL owner cannot be admitted to another component,
so it cannot legitimately survive there after its defining DLL is unloaded.

## Hazards

The carrier stores a non-atomic 32-bit mounting-point hazard mask. Singular
ownership and the surrounding communication structures provide the threading
contract; the nominal owner is responsible for mutation.

`add_hazard()`, `remove_hazard()`, and `has_hazard()` take strong mounting-point
IDs. The bit position is the generated zero-based mounting-point index.
`has_any_hazard()` provides the fast unload/destruction gate and
`hazard_mask()` provides diagnostic detail.

The build fails if the registered mounting-point count exceeds 32. The wider
runtime ID encoding capacity does not enlarge this carrier field.

Hazards describe module-lifetime dependency. They are not provisioning,
visibility, access-rights, or ownership metadata.

Hazards normally accumulate as work moves through the system. Removal is an
explicit owner-authorized safety transition after every dependency on the
mounting point has been relinquished. Moves preserve the complete mask and
reattribution does not alter it. There is intentionally no public whole-mask
setter.

## Reattribution

`can_reattribute_to()` preflights the carrier token and the registered
payload's nested ownership against a proposed target context.

`reattribute()` gathers the carrier and nested payload allocation count and
size, performs one aggregate accounting adjustment, then replaces every
participating token context without further accounting. Carrier type, payload address, and
hazards remain unchanged. Empty owners succeed without becoming configured,
preserving canonical emptiness.

Registered payloads include byte and image storage, baked and live documents,
and owning request envelopes. The catalogue in
`core/system/system_erased_owner_payloads.def` selects each payload's nested
storage member. Those owners expose the common attribution interface, including
explicitly unsafe context replacement without accounting. The carrier uses
that hook only after preflight and the aggregate adjustment; see the
[uniform attribution contract](../memory/memory_subsystem.md).

For LOCAL identity, both source and target memory contexts must belong to the
currently executing component's ambient module. This is checked even for a
direct call outside transport admission. Failure occurs before accounting or
context mutation, leaving owner identity, payload address and contents,
hazards, contexts, and accounting unchanged.

## Erased Owner Transport

`CErasedOwnerTransport` is the attribution-aware composition
over the non-reattributable `TOwning<CErasedOwner>` primitive.

The wrapper stores an explicit destination module. Admission happens only on
post, deriving the source from the ambient module and rejecting unavailable
identity registrations or LOCAL identity intended for another component
before ownership or attribution is mutated. Rejection emits one `MV_ERROR`;
diagnostic failure does not change the Boolean result. The destination must
agree with the fixed recipient context, or with the transport context when no
separate recipient is configured. This provenance check uses
`CMemoryContext::belongs_to_module()`.

For `CErasedOwnerMsgTransport`, message identity is admitted first and an
optional owned-payload identity second. The first rejected identity is the only
one diagnosed. Both decisions precede transport validity, capacity,
reattribution, and queue mutation. A direct owner transport quietly rejects a
canonical empty owner, and ordinary capacity, allocation, closed-transport, or
attribution-compatibility failure is not reported as an identity breach.

On successful post:

- readiness, writable capacity, and source compatibility are checked before
  mutation;
- carrier and payload attribution move to the fixed transport context;
- ownership then moves into the underlying transport.

On successful read:

- ownership first moves out of the underlying transport;
- attribution then moves to the optional fixed recipient context;
- without a recipient context, transport attribution is retained.

Identity is not revalidated on read. The owning read side exists to perform
the required memory re-attribution as ownership exits the transport.

Transport and recipient allocator compatibility is validated before
initialisation and ordinary operation. An unexpected read-time reattribution
failure still delivers the item and triggers `MV_CRITICAL_ASSERT`; it is not
treated as an ordinary incompatibility or disposed of. Accounting discrepancies
alone cannot cause that failure: the primitive reports them without rejecting
reattribution. The assertion protects structural/ownership invariants after
successful preflight; see [memory accounting](../memory/memory_subsystem.md).

Thin producer and consumer endpoints expose only their role-specific wrapper
operations. The underlying `TOwning` and context mutation are not exposed.
Plain transports remain non-reattributable.

`MV_CRITICAL_ASSERT` remains reserved for failures that represent broken
architecture or ownership contracts after successful admission.

## Virtual Interface Boundary

Virtual classes remain valid internal implementation tools when their objects
and executable pointers cannot cross module lifetime boundaries. The
non-virtual design of `CErasedOwner` is specifically required because a SYSTEM
owner may cross components. A LOCAL owner may move between threads in its
defining component but may not cross the component boundary. This is not a
codebase-wide prohibition on virtual dispatch.

The wider fence applies to any transported or retained vptr, callback,
deleter, function pointer, or operation descriptor whose defining module may
be unloaded. Such executable identity must not escape its valid module
lifetime.
