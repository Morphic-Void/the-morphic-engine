# Consolidation coordinator handoff

19 September 2026. Starting point for the successor coordinator task, replacing
the dense discussion in task `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd`.

## Read first and precedence

Read [asynchronous asset-operation notes](asynchronous_asset_operation_notes.md)
first. They contain Ritchie's latest scope and ownership decisions, image-view
direction and explanation of the proof-of-concept line algorithm. They supersede
older statements that every transfer must create a permanent asset or that every
save requires separate admission and an asset-ID receipt.

Use this handoff and those notes for current direction. The
[design order](consolidation_design_order.md),
[consolidation plan](consolidation_pass.md) and
[stage discussion](consolidation_stage_1_specification.md) retain substantial
history, including superseded proposals and stale pending-work statements.
Do not restart completed work or promote an old stage into an active assignment.

## Completed baseline

- `d1d804c`: diagnostic-only memory accounting.
- `42a908d`: uniform container and live-document attribution/reattribution.
- `a75962f`: baked-document storage and views, version-4 stored section offsets,
  checked byte-buffer adoption, rounded capacity and aligned file loading.
- `7401671`: consolidated and deferred design history.
- `b7657f7`: future document navigation and save-game uses.

The production changes passed coordinator and user review, four full
Debug/Release x64/x86 configurations and the recorded engine smoke exercises.
See the focused accounting, container and baked-storage specifications for
evidence. Baked blocks now own only CByteBuffer and construct borrowed views on
demand without allocation/revalidation. Arbitrary-byte binding remains checked;
the current production baked format remains immutable. Its version-4 header is
64 bytes, with counts and offsets; minimum extent/capacity are 114/128 bytes.

There is also separately committed schema design material (`b5f7441`,
docs/schema). It does not imply that the asynchronous acceptance work is complete.

## Remaining work and next discussion

The next design discussion is the image view. Ritchie expects a richer class with
local mutable state, borrowing rectangular storage. Determine metadata, pixel
access, mutability and which state belongs in the view versus operation arguments.
Whether it is suitable as encode/save configuration or should supply smaller
settings remains open.

Ritchie is considering including a deliberately small drawing set: clipped,
non-antialiased Bresenham lines, filled/unfilled rectangles and rectangle copies.
Drawing state must inform the design even if implementation is staged. The latest
notes give inclusive endpoints, major-axis endpoint ordering and preservation of
the accumulator when clipping skips pixels. Translation stability and endpoint
reversal independence are requirements; mirror symmetry is an expected consequence
to verify. Exact ties/update ordering remain open. No source extraction is needed
merely to begin this discussion, and no drawing implementation is authorised yet.

The remaining consolidation develops retained and one-shot save ownership,
concrete raw/baked/JSON/TGA asynchronous services, and their end-to-end acceptance
coverage together. Every successful load creates a permanent Host-owned asset.
Ownership only moves to the Host. Retained assets live until application exit;
saving by ID does not dispose of them. A one-shot save may receive temporary
ownership and dispose after completion. Interfaces may permit limited mutation
without transferring ownership. Failure/cleanup, operation ordering and concurrent
mutation/save semantics still need design. Live-document baking placement is open.

The binary/JSON/TGA Executive workflows prove the mechanism and must not be
postponed until after claiming it complete. Module load/unload migration to the
Host worker remains a separate follow-up after that exercise.

Filesystem-image implementation, path navigation helpers, trust, overlays/layers,
general reclamation and the broader asynchronous framework remain deferred. The
possible baked numeric/Boolean mutation API for save-game state is not yet
selected. Preserve those ideas in the
[deferred resource](consolidation_deferred_design.md) and
[filesystem notes](filesystem_asset_mapping.md).

## Working arrangement

- Work directly in the shared main checkout at D:\TheMorphicEngine; no worktrees
  or other branches. Preserve unrelated user/task changes.
- The successor is the coordinator: aggregate decisions and review developed
  plans/implementations. Implementing tasks discuss substantive choices directly
  with Ritchie; avoid coordinator micromanagement.
- Continue sequentially. Create an implementing task only when Ritchie asks;
  use Astra High as his established task preference.
- This handoff authorises continuity and design discussion, not production edits.
  Start with a short acknowledgement and readiness for the image-view discussion.
- Follow AGENTS.md. Commits require coordinator review and explicit user
  instruction; approval of one commit is not blanket permission for later commits.
  Ritchie handles all pushes. Do not push.
- Do not repeat passing validation matrices without a new change or concern.
  Preserve Visual Studio item/filter file exceptions and user formatting edits.

The original stage/baked implementing task is
`01a0a4d6-e506-7912-8cf8-0ace7c96453a`; its bounded work is complete. The completed
container task is `01a0aa63-8583-7b91-862e-b56199f9e039`. They are references, not
active assignments to resume automatically.
