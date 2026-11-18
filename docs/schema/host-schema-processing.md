# Host schema processing

An authenticated module can submit an authored schema, combine selected sections from several schemas, or merge two schemas. Host returns an immutable asset and an acquisition on the request's async slot. A Host-local consumer can use the same admission, worker and publication paths. The service is available before Platform or Rendering loads.

## Submitting a request

Use `framework/assets/schema_messages.hpp` and `threading::make_owner_message<SchemaProcessRequest>(slot)`. Use a nonnegative async slot, contract version 1 and a fresh nonzero request ID. Allocate IDs in increasing order for each client installation. Keep correlation until the terminal reply. A replacement installation starts its own sequence.

Fill the first `input_count` entries of `inputs.slots`. An `owned_document` input contains an independently owned `CLiveDocument`. A `retained_document` input names a schema-document asset from a prior successful operation. Keep each retained asset live through admission. Leave its inactive alternative and the unused slots empty.

| Operation | Inputs | Selection |
| --- | --- | --- |
| `existing` | One | Preserve all contained sections |
| `compose_roles` | One to three | `types_source`, `instances_source` and `data_source` select input indices. Use −1 for an absent section. Every active input must supply a selected role. One input may supply several roles. |
| `merge_two` | Two | Apply Foundation's declaration compatibility proof and value conflict rules |

Set all three selectors to −1 for `existing` and `merge_two`. A selected section must exist in its nominated document. Schema-document output admits incomplete authoring content. Runtime output requires resolvable contained declarations, complete typed validation and a full bake. It includes retained strings, remapped text and collection IDs, and the complete appended payloads. Every runtime result is self-contained. The separate `SchemaPrepareRequest` service materialises a retained raw UTF-8 source into temporary prepared views. [Prepared-schema service](prepared-schema.md)

For example, submit a retained declaration asset alongside a single tree supplying both other roles. Complete construction of `observations` before moving it into the request.

```cpp
auto message = threading::make_owner_message<SchemaProcessRequest>(slot);
SchemaProcessRequest* const request = message.owner().payload<SchemaProcessRequest>();
if (!request) return false;
request->request_id = { next_id };
request->operation = schema::ESchemaAssembly::compose_roles;
request->output_kind = schema::ESchemaOutput::runtime_data_block;
request->input_count = 2u;
request->types_source = 0;
request->instances_source = 1;
request->data_source = 1;
request->inputs.slots[0].kind = ESchemaInput::retained_document;
request->inputs.slots[0].asset = declarations;
request->inputs.slots[1].kind = ESchemaInput::owned_document;
request->inputs.slots[1].document = std::move(observations);
```

Post through the module's authenticated Host owning-message transport. A pumped client calls `post_owner`. An explicit untransferred busy outcome permits retry of the same envelope. Once transport accepts it, discard every sender pointer or borrow into the envelope and its trees. Host owns their retirement even if admission fails. The sender must stop all mutable and read-only aliases before transfer.

Host-local callers use `CAssetService::post_internal_process` on the owner thread with a stable, installation-scoped `SClientRecord`. This entry point reserves the two ordinary reply credits. A false return leaves the message with the caller. Consume the same POD replies, release acquisitions and dispose retained assets using that record. Keep its restriction reader and memory context alive through retirement.

## Replies and cancellation

`SchemaProcessAccepted` confirms Host protected the retained inputs, reserved an acquisition and queued admission before dispatch. Both replies carry the request ID and arrive on the original async slot. A rejected request receives just `SchemaProcessResult`. To cancel before admission is known, remember the intent and send `SchemaProcessCancel` after Accepted. The original terminal result settles the request. If success won the race, release the acquisition and dispose or retain the asset in the usual way.

On success, check the reply version and output kind, the acquisition and asset IDs, and `descriptor->size`, `version` and `kind`. The acquisition already protects the descriptor and `descriptor->bytes`. Use the complete byte view, including its alignment, when attaching a runtime block. End every read before posting `AssetReleaseRequest`. `AssetDisposeRequest` closes the asset to new readers and drops its retention after previously admitted reads retire. Further acquisitions have independent tokens. Retained declarations can be reused after releasing the original acquisition and after the producing module unloads.

On failure, read the copied `failure` member. Admission failures use `ESchemaAdmissionReason`. Assembly, encoding and bake failures carry the Foundation `ESchemaReason`. Host lifecycle failures during publication use `ESchemaAdmissionReason`. Input indices are zero based, with −1 for unavailable. Roles use `ESchemaSource`. The location tag describes the two location components. Authored trees and retained documents carry no original UTF-8 parse positions. Host copies available record ordinals and retains additional schema diagnostics and bounded paths in its log under the exact client and request IDs.

## Execution and budgets

Host admits four operations, including pending publication and unpublished-output reclamation. A separate fixed rejection slot lets an already-transferred envelope receive a busy result after conditioning retires its inputs. When that slot is occupied, ingress backpressures the bounded owning transports. Host reserves both replies before consuming a request and settles the unused credit on rejection.

The conditioning worker runs Foundation assembly and encoding or baking. The single-worker configuration routes these operations and reclamation to the file I/O worker. Host protects producer code and contexts through the client and module drain barriers. A separately synchronised stop flag is the only part of executing input state that lifecycle progression changes. Entered synchronous work can finish. The worker destroys the envelope and scratch before acknowledgement. Host then drops the pins and checks the exact installation and eligible time before publication.

| Shared allowance | Default |
| --- | ---: |
| Distinct inspected source storage | 64 MiB |
| Peak owned storage | 64 MiB |
| Complete result size | 16 MiB |
| Work credits | 4,194,304 |
| Cumulative document-node admissions | 262,144 |
| Records | 262,144 |
| Nesting depth | 256 |

Callers may tighten the defaults. The worker charges distinct retained byte ranges and the attributed backing capacity of transferred trees. Repeated references to one retained block share source admission. Transferred tree storage stays charged while the worker owns it. The operation allocator covers assembly, declaration checks, resolution, typed staging and private output. Publication reserves the independent Host allocation while staging remains live. Work charges include source inspection, tree traversal, name comparisons, declaration-index probes, graph comparisons, value construction and output copying. Output encoding uses the same allowance as assembly. Mandatory destruction still runs after an exhausted limit.

Leave all eligible-time anchor fields zero to omit a deadline. Otherwise pass the anchor of a started `CHostEligibleBudget` from the same Host restriction publication. Queueing, retries and processing use that original eligible-time origin. The worker checks queued expiry on entry and the owner checks again at publication. A deadline cannot establish quiescence.

Result backing lives outside the four operation slots. Definite non-delivery retires it privately. A poisoned publication that may have exposed the pointer keeps its acquisition and backing until explicit release or proven recipient, queue and batch quiescence. Host never retries that terminal publication. Ordinary disposal posts heavy destruction to conditioning. Exceptional synchronous reclamation requires stopped workers and drained outputs. Retained assets use the Host asset repository, with its existing identities and allocation limits.

Processing an asset performs no file I/O or Platform semantic admission. Platform capture, report publication and report deadlines remain provider-specific. [Implementation and validation record](host-schema-processing-validation.md)
