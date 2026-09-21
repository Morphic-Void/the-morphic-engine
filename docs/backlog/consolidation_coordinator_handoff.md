# Consolidation coordinator handoff

Updated 21 September 2026 after the manual style pass: image-view work is
complete in `5b1282f` and `98ce708`. The combined asset-service and Executive
acceptance pass is committed in `f74213f` after user and coordinator review. The
legacy client TGA flow and redundant catalogue identities are retired.
See [asynchronous asset services](../assets/asynchronous_asset_services.md) for the
settled contracts, review map and validation evidence. The subsequently authorised
module-worker migration is implemented and reviewed; see
[asynchronous module lifecycle](../modules/asynchronous_module_lifecycle.md) for
the Executive bootstrap, replacement, notification and shutdown contracts.
The follow-up adds explicit retained-asset disposal and replaces dependency-based
unload rejection with assertion logging and disposal before DLL unbinding.
The 21 September review tidy-up consolidates module declarations and transport
registrations and completes worker request diagnostics. That module/disposal
stage is committed as `491ce78` after user and coordinator review.
The separately authorised rendering DLL stub is now implemented in the shared
checkout. Lifecycle and Vulkan identity coordinator reviews are complete.
The user accepted the work and authorised its commit on 21 September.

Originally captured 19 September 2026 for the successor coordinator task, replacing
the dense discussion in task `01a09fd4-73ad-7ce2-a3e4-efcdadb454dd`.

## Read first and precedence

Use [current scope](current_scope_backlog.md), this handoff and the implemented
[image](../image/image_view.md), [asset](../assets/asynchronous_asset_services.md)
and [module](../modules/asynchronous_module_lifecycle.md) contracts for current
direction. The [asynchronous asset-operation notes](asynchronous_asset_operation_notes.md),
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
- `e60407f`: shared document reports and byte-view parsing.
- `5b1282f`, `98ce708`: image view, drawing, copy and TGA configuration utility.
- `f74213f`: consolidated asynchronous asset services and Executive acceptance.

The production changes passed coordinator and user review, four full
Debug/Release x64/x86 configurations and the recorded engine smoke exercises.
See the focused accounting, container and baked-storage specifications for
evidence. Baked blocks now own only CByteBuffer and construct borrowed views on
demand without allocation/revalidation. Arbitrary-byte binding remains checked;
the current production baked format remains immutable. Its version-4 header is
64 bytes, with counts and offsets; minimum extent/capacity are 114/128 bytes.

There is also separately committed schema design material (`b5f7441`,
docs/schema). It does not imply that schema implementation is complete.

## Completed module lifecycle and asset disposal

DLL loading/binding and unbinding/unloading run on the Host I/O worker. The Host
starts without an Executive, binds it asynchronously and then starts its thread.
Ordinary module requests receive acknowledgement and completion; self-termination
sets the Executive exit request immediately and sends neither notification.
Self-unload requests system shutdown; failed self-replacement logs an assertion
and shuts down. A failed ordinary replacement leaves the service unavailable.

Explicit asset disposal waits for accepted borrowers and rejects later saves by
that ID. Before module unload, the Host drains operations and joins the affected
Host-managed thread. Remaining dependent assets produce an assertion log and are
disposed of while their DLL is loaded. Unrelated retained assets survive replacement.
The rendering extension now adds a second DLL with a Host-managed thread.

Messages and registrations are grouped in `core/system/transported_types.hpp`;
the worker job and module records live in `host/runtime/module_service.hpp`.
All worker request handlers have detail diagnostics. The Core suites and twelve
lifecycle scenarios passed in Debug/Release x64/Win32 after the tidy-up. Debug x64
passed again after the manual style pass; see the lifecycle document for logs.

## Next stage and deferred scope

The rendering stage adds `MorphicRendering.vcxproj` to the solution, using the
existing `render_vulkan_windows` and `rendering` identities. Its only runtime
work is to wait for an exit request. Ordinary module completion waits for
thread startup, and failure cleanup runs on the I/O worker. Teardown joins the
thread before dependent-asset disposal and DLL unload. Rendering transitions
remain distinct from Executive self-termination. The normal Executive now chooses
and requests its Vulkan renderer before asset acceptance, checking acknowledgement,
identity and availability. It reuses an already available matching implementation
after Executive replacement. Selector Executives can omit rendering entirely;
the Host still bootstraps only Executive. This startup-policy follow-up has passed
coordinator review and is included in the user's commit authorisation after the
preceding unchanged style pass.

Successful lifecycle service cases now use the real stub. The Executive driver,
missing rendering export, failed rendering startup and disposal during rendering
exit remain purposeful fixtures. The stopped rendering package stays alive until
its final requests have been drained and accepted asset operations have replied.
Review this stage before selecting subsequent work; no rendering API, general
job framework or schema implementation is included.

Filesystem-image implementation, path navigation helpers, trust, overlays/layers,
automatic reclamation/cache eviction and the broader asynchronous framework
remain deferred. The possible baked numeric/Boolean mutation API for save-game state is not yet
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
- This handoff records current state; new stages require the user's direction.
  Do not reopen the completed image, asset-service or parser design discussions.
- Follow AGENTS.md. Commits require coordinator review and explicit user
  instruction; approval of one commit is not blanket permission for later commits.
  Ritchie handles all pushes. Do not push.
- Do not repeat passing validation matrices without a new change or concern.
  Preserve Visual Studio item/filter file exceptions and user formatting edits.

The original stage/baked implementing task is
`01a0a4d6-e506-7912-8cf8-0ace7c96453a`; its bounded work is complete. The completed
container task is `01a0aa63-8583-7b91-862e-b56199f9e039`. They are references, not
active assignments to resume automatically.
