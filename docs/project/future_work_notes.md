Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   future_work_notes.md
Primary draft: OpenAI tools
Reviewed and accepted by: Ritchie Brannan
Date:   26 Jul 2026

# Future Work Notes

## Purpose

This document retains cross-task context that remains useful to future work.
It is not the active-priority list and does not repeat permanent subsystem
contracts.

Use:

- `docs/backlog/current_scope_backlog.md` for active priorities;
- `docs/backlog/engine_backlog.md` for the broader backlog;
- `docs/backlog/consolidation_pass.md` for parser and Host consolidation;
- `completed_milestones.md` for completed cross-cutting work;
- subsystem documents for implemented behavior.

Remove notes from this file when they are completed, rejected, or promoted into
a permanent subsystem contract.

## Debug Infrastructure

For `CErasedOwnerTransport`, an unexpected read-time reattribution failure is
an accounting or corruption boundary. The item is still delivered rather than
discarded, and the critical-reporting path records the violation.

## Erased Payload Context

Future provisioning or payload-context metadata must remain semantically
separate from the mounting-point hazard mask. Hazards represent module-lifetime
dependencies only; they are not visibility, access-rights, ownership, or
automatic-provisioning state.

Add such metadata only when concrete provisioning behavior and layout needs
are known.

## MPMC Consumer Facades

The MPMC transport foundation is complete. Add thin debug-system, job-system,
or other consumer facades only when concrete access patterns justify their
shape. These facades are downstream integration rather than unfinished
transport work.

## Memory And Template Coverage

Add instantiation coverage for supported latent templates as they are touched.
Absence of a current instantiation is not evidence that a template or
capability is dead.

Complete allocator bootstrap ordering before removing the temporary fallback
allocator.

The established non-transactional reserve collaboration between slot metadata
and backing storage remains intentional. A coordinated transactional redesign
is a separate architectural decision, not unfinished sandwich-refactor work.

## Cross-Platform Validation

Resume thread provisioning, TLS, formal threading stress tests, and
sanitizer-backed validation when the Linux build path makes that work
practical. Additional MPMC stress in that environment is validation expansion,
not a completion condition for the existing transport family.
