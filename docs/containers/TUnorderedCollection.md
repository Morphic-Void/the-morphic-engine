Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   TUnorderedCollection.md  
Author: Ritchie Brannan  
Date:   1 Apr 2026  

# TUnorderedCollection<T>

## Overview

TUnorderedCollection<T> is a move-only unordered collection wrapper
over TUnorderedSlots and stable CMemoryToken storage.

Slot metadata is managed by TUnorderedSlots. Object storage is provided
by CMemoryToken in stable mode.

Public identity during the mutable phase is slot_index.

Constructed objects have stable addresses in the stable token.

pack() remaps slot metadata and slot-side payload but does not relocate
live T objects. Pointers and references to constructed objects remain
valid across pack().

## Requirements and scope

Move assignment destroys all live destination objects and releases their backing
before taking the source's storage and slot metadata. Source object addresses,
slot identities and allocation attribution are preserved. The source becomes
empty and can be initialised again. Self-move is a no-op; moving requires no new
container allocation. Element destructors still run normally.

- Requires C++17 or later.
- No exceptions are used.
- Indices, sizes, and capacities are expressed in elements.

Scope:

- Provides slot-based identity and stable object storage.
- Does not expose storage_index as public identity.
- Does not define ordering or rank semantics.

## State model

Each slot is in one of the following states:

Unmapped:

- slot has a valid storage_index binding
- backing storage has not been mapped

Mapped:

- backing storage is mapped
- no constructed object exists

Constructed:

- a live T object exists at the bound storage_index

Only constructed slots expose live objects.

Empty slots may retain valid internal storage_index bindings.

## Identity model

slot_index:

- public identity during mutation
- not stable across pack()

storage_index:

- internal mapping to stable storage
- not exposed as public identity

## Storage and stability

Objects are placement-constructed in stable token storage.

- addresses are stable once constructed
- slot remapping does not relocate objects
- storage_index determines object location

The stable token uses `sizeof(T)` stride,
`memory::t_default_align<T>()` storage alignment, and the established
floor-32 per-buffer capacity hint. First storage mapping uses
`map_index()`; subsequent access uses `index_ptr()`. The collection does
not request a contiguous view of object storage.

## Traversal model

Traversal follows TUnorderedSlots behaviour.

- traversal order is list order
- traversal order does not imply rank
- traversal order does not imply insertion order

## Pack behaviour

pack():

- compacts slot metadata
- remaps slot indexing
- does not move constructed objects
- preserves object addresses

If external slot-index keyed state must survive pack(), call
build_rank_map() before pack() and apply the remap from that
pre-pack map.

The rank map must be produced before pack() to be meaningful. A rank
map produced after pack() is only the post-pack identity mapping.

Remap semantics follow the underlying TUnorderedSlots contract:

- mapping is only defined over the occupied domain
- empty slots do not participate in rank identity and will contain -1 sentinels

## Lifetime model

Pointers and references returned by the collection:

- are non-owning
- refer to placement-constructed objects
- must not be destroyed with delete

Object lifetime must be managed through the collection API.

A const owning collection controls both collection and pointee constness
at its public surface. Mutable non-owning access remains available only
through an explicitly mutable view.

## Behavioural notes

- slot_index remapping does not imply object relocation
- internal storage_index bindings may exist for non-constructed slots
- collection is intended as a higher-level wrapper over slot and
  storage primitives
