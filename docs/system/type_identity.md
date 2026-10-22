Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# System And Local Identity

## Categories

System type identities are the deliberate cross-component identity category.
Any C++ type used by a shared ABI, function query, erased payload, owner,
message, transport, or another component boundary requires a host-authored
system ID. Component-confined erased storage may instead use local identity.
`system/system_type_ids.def` is the sole canonical system definition list.
`MV_REGISTER_SYSTEM_TYPE` binds a C++ type to an existing system ID; it does
not author an ID or a name.

Local type identities belong to one binary component. They may identify a
structure, class, enum, or tag type, but they cannot cross a module boundary.
Copying a validated local short name into a later diagnostic event will be
serialization of text, not export of the identity.

A shared registration-category trait prevents one C++ type from receiving
both system and local registrations in a component.

## Encoding

Both strong ID types occupy 32 bits and use the existing alternating-bit
misuse detector. Fifteen even-position bits encode the ordinal payload:

```text
encoded payload mask  0x15555555
system type flag      0x40000000
allowed-bit mask      0x55555555
invalid-bit mask      0xaaaaaaaa
ordinal mask          0x00007fff
valid ordinals        0..0x7ffe
logical category bit  0x00008000
local tagged indices  0x0000..0x7ffe
system tagged indices 0x8000..0xfffe
invalid tagged index  0xffff
capacity              32767 per category
```

The encoded payload is `spread_to_even_bits(0x7fff - ordinal)`, so it is
always nonzero. A system ID sets the system flag; a local ID leaves it clear.
Consequently a missing category bit cannot accidentally produce a system
identity. Zero remains undefined for both strong types.

Definition lists supply ordinary ordinals. Named identities, their indices,
and definition counts remain directly in each `*_ids` namespace. Encoding,
validation, capacity, and layout operations live in the nested `*_ids::ops`
namespace. Category-specific `ops::encode_index` helpers apply the logical
category bit, and `ops::decode_index` removes it only after validating the
category. `ops::encode_id` accepts the resulting tagged index;
`ops::decode_id` returns it. This makes the former `0xfffe`/`0xffff` edge
explicit: `0xfffe` is the last valid system index and `0xffff` is invalid.

`system_type_id` and `local_type_id` are distinct strong C++ types. Their
validators also reject raw encodings from the other category.

`type_id` is a third four-byte strong type which preserves either category.
Its constructors from `system_type_id` and `local_type_id` are explicit and
canonicalise invalid or category-mismatched strong values to undefined.
`try_system_type_id()` and `try_local_type_id()` explicitly extract a category
and clear their output on mismatch. No implicit conversion exists among the
three strong types. All three have identical four-byte size and alignment on
x64 and x86, while remaining distinct C++ types.

`TSystemTypeId<T>` and `k_system_type_id_v<T>` expose an explicitly SYSTEM
binding. `TLocalTypeId<T>` and `k_local_type_id_v<T>` expose an explicitly
LOCAL binding. `TTypeId<T>` and `k_type_id_v<T>` select the registered category
and produce the category-bearing `type_id`; an unregistered type has no valid
selector.

All strong ID wrappers default to their canonical undefined value. Conversion
to the underlying integer representation is deliberately unavailable; ABI,
serialization, formatting, and dispatch code must request `raw_value()`
explicitly.

## Runtime Identity

Runtime identity uses one 64-bit alternating-bit layout with independent
fields for thread, mount-point, and module ordinals. A module ID combines a
mount-point field with a module field, and a system ID combines a module ID
with a thread field. The fields contain 16, 6, and 10 payload bits respectively.

Each field reserves zero as undefined and maps ordinal `n` to
`spread_to_even_bits(payload_mask - n)` at its field offset. Consequently every
ordinary ordinal from zero through `payload_mask - 1` has a distinct nonzero
encoding, and decoding every valid field reproduces its original ordinal.
Generated mount-point, module, and thread definition counts are checked against
those capacities at compile time.

The same catalog/operation split applies to runtime identity: named values and
indices live directly in `mount_point_ids`, `module_ids`, `thread_ids`, and
`system_ids`, while construction, validation, decomposition, capacity, and
layout details live beneath each category's `ops` namespace.

Structural validity means that an ID is nonzero, contains no bits outside its
declared fields, and has every required constituent field. Registration is a
separate stronger property established by lookup in the installed system
registry.

## Local Definitions

Each binary owns an immutable local definition table. Host definitions remain
under `host/` and Executive definitions remain under `executive/`. Both
components use the neutral in-directory names `local_type_ids.def`,
`erased_owner_payloads.def`, and `local_type_ids.*`.

The component catalog header reopens `local_type_ids`, so ordinary component
code uses names such as `local_type_ids::host_runtime` without an additional
component prefix. Component catalog headers are binary-private and two
components' catalogs must not be included in the same translation unit.
MorphicTests composes the same Host-private protocol fragment into its own
catalogue, alongside test-local types; it never links the Host catalogue.
Shared Host sources select `<local_type_ids.hpp>` using the target's private
include directory (`host/module/types` or `tests/environment`). Executive and
Rendering generate and install their own catalogues in their respective binaries.
Lifecycle fixture DLLs use the separate `tests/module_lifecycle` catalogue.

Component `.def` files enumerate ordinary zero-based ordinals. Repeated
inclusion generates IDs, C++ bindings, immutable registrations, and count
assertions. Names come from stringified identifier tokens and contain at most
15 bytes plus a terminator. Their complete 16-byte values are constructed at
compile time, terminated, and zero-filled.

The repeated declaration and definition bodies live in Core's
`local_type_ids.hpp.inl` and `local_type_ids.cpp.inl`. Identical component
wrappers include their adjacent `local_types.hpp`, `local_type_ids.def`, and
`erased_owner_payloads.def` files before invoking those Core bodies. The
catalogs expose list macros consumed by Core, so no component path or
component-specific configuration macro enters the shared machinery. IDs,
payload eligibility, query functions, and static tables remain instantiated
separately in each binary.

The component installs and validates its complete table during explicit
bootstrap. Lookup revalidates category, decoded ordinal, range, exact entry
identity, termination, and zero fill. Missing, fabricated, corrupt, and
unavailable registrations resolve to failure without allocation or dynamic
registration.

Host file, TGA, document-conditioning, bundle-preparation, module-work and
filesystem-scan protocols belong to its LOCAL table, together with their private
retained owners. Public asset, bundle, module and filesystem requests, and the
texture and shader conditioning exchanges, retain SYSTEM identity. Their shared
contracts live in Core; rendering implementation types remain private.
`TErasedPod` and
`CASyncState` carry `type_id`, retain their existing fixed layouts, and accept
either registered binding category.

Destination-aware admission belongs to the concrete erased-message and
erased-owner wrappers, not the generic queue and owning transport templates.
Each concrete wrapper stores its destination module. On `post()`, the source
is the ambient module: a registered SYSTEM identity may be admitted to any
valid destination, while a registered LOCAL identity requires source and
destination to be the same module. Undefined, malformed, unavailable, and
unregistered identities are rejected before copying, moving ownership, or
changing memory attribution. Retrieval does not revalidate identity; owning
retrieval only performs its required exit-side memory re-attribution.

Admission classification validates source, destination, and structural type
identity in that order. SYSTEM registration is then resolved. LOCAL boundary
agreement precedes LOCAL registration lookup, so a cross-component LOCAL value
is classified as a boundary violation even if it is also unavailable locally.
The quiet policy query remains available for focused checks. Concrete posting
wrappers emit one diagnostic-only `MV_ASSERT_REPORT` for the first identity-policy
rejection, including the failing carrier role, identity, source, destination, and reason. Diagnostic
submission failure does not change the Boolean rejection. Successful admission
and routine queue, allocation, or attribution failure emit no identity-policy
diagnostic.

Owning wrappers additionally require the declared destination to match the
module represented by the recipient context, or by the transport context when
there is no distinct recipient. `SErasedMsgHeader`, `CErasedPodMsg`,
`CErasedOwnerMsg`, `CErasedOwner`, and `CAssetRecord` expose category-bearing
identity without changing their established fixed layouts. `CErasedOwner`
supports explicitly eligible SYSTEM and LOCAL payloads. SYSTEM operations are
compiled into every receiving component; LOCAL operations exist only in the
defining component. Direct LOCAL-owner reattribution also requires source and
target contexts to belong to that component, independently of transport
admission.

Message dispatch compares the umbrella value locally. An unrecognised value
must be explicitly extracted as SYSTEM before it can enter the SYSTEM-only
`UnrecognisedMsg` response or existing debug argument transport. Unknown LOCAL
identity uses the debug system's bounded local-identity failure form.

## System Name Authority

Only the executable hosts system type, mount-point, module, thread, and
composed system names. Modules retain compiled numeric constants required by
the ABI but contain no independent system-name arrays.

The host installs an immutable view containing host-lifetime pointers and
fixed-width counts. Shared lookup code is compiled into each component and
operates over its component-local copy of that view. Before installation,
lookups safely return unresolved. Global names have no local short-name limit.
View validation checks table shape and encoded capacity before traversal, then
requires each entry to match its ordinal, encoded ID, relationship fields, and
name. The public installed-registry surface exposes the immutable view,
validated lookup and formatting, and one complete `validate_all()` diagnostic;
it does not duplicate raw table access or per-table validation APIs.

Debug arguments copy a registered LOCAL short name into the event while its
originating catalogue is available. Unresolved LOCAL identities use the bounded
failure form. Neither representation exports a pointer into a component table.

SYSTEM ordinals are compiled into all binaries. Removing private entries during
the messaging consolidation renumbers later identities. Transport IDs are not
persisted in the current asset formats, but modules and lifecycle fixtures must
be rebuilt and deployed with the Host. Registry validation checks table
consistency, not agreement with another build's compiled ordinals; the binding
ABI version alone does not detect this catalogue change.
