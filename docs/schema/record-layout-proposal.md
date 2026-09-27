# Resolved record layout decision

The user accepted this proposal on 27 September 2026. Its decisions are now
consolidated into the canonical documents:

- [Design: runtime representation](design.md#runtime-representation) defines
  the allocation limit, private records, physical/logical tags and access model.
- [Implementation contract: runtime records and limits](implementation-contract.md#runtime-records-and-limits)
  defines the required storage, checked arithmetic and validation boundaries.
- [Resolved schema API](runtime-api.md) documents observations, lifetime rules,
  measured record sizes and current internal range access.

Those documents supersede the proposal text. This page is a decision pointer,
not a second normative specification. The accepted pass implements compact
records and range access only; the future instance, binary, remap and output
operation boundaries do not authorise those implementations.
