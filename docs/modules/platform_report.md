# Host-owned Platform report

Platform activation schedules one whole desktop discovery after the installation handshake. The Host records availability and update history. Executive, Platform and other authenticated modules use the same Framework messages. The Platform window and input services have independent lifetimes.

## Reading and updating

Include `framework/platform/report_messages.hpp`. Post an empty `platform::reports::PlatformReportRequest` or `PlatformReportUpdateRequest` to Host with a reply slot. Both return `PlatformReportResult`. A current read observes the existing snapshot. An update requests one capture. Overlapping updates receive `update_in_progress`. With no Platform installed, the result is `unavailable`. With an installed provider and no published snapshot, a current read returns `not_ready`.

On success, the acquisition and complete immutable byte view arrive together. For typed offline inspection, copy the complete aligned range into a local `CBakedDocumentBlock`, adopt it with `schema::CBakedDataBlock::adopt_local`, then bind `schema::CDataBlockView`. The inspector demonstrates that sequence. Quiesce all users of that range before releasing the token through `AssetReleaseRequest` on the same authenticated client. The schema report header's `capture_id` and `platform_instance` identify the observations. The byte address and all capture-local IDs are scoped to that snapshot. An unsuccessful update returns no acquisition. Read current to acquire the previous publication. Host can distribute a new current snapshot or unload the producer while readers retain their earlier bytes.

## Format and provenance

`framework/platform/report_types.inl` is the schema source. `MorphicSchemaFixture platform-layout <output.inl>` resolves it using the shared schema generator and emits the layout and ABI assertions. `framework/platform/report.hpp` declares the version-1 limits and collection names. The typed `PlatformReportHeader` instance is named `report`. Positive ranges identify records within named collections. Present-empty and absent have distinct meanings. Use the collection/query associations when interpreting labels, tagged identity payloads, catalogue filters, known-field masks and refresh precision.

The Windows provider observes the calling session with CCD, GDI and DXGI. Modes belong to individual query catalogues. Active paths connect a source to a display. An available inactive target can be visible without an active path. A documented denial or unsupported CCD inventory can publish its limitation and absent topology. An unsupported native backend reports capture failure. Discovery does not require a window or a rendering device.

Read each known optional fact alongside its supporting query. Host checks the query kind, subject and successful outcome for present labels, GDI current fields, primary/attachment annotations and native identity evidence. A successful query may supply a subset of its fields. A failed or blocked query can explain an absent label, unknown annotation or empty known-field mask. DXGI output description and output-interface acquisition have separate outcomes so a mode-list failure can coexist with an observed output name.

## Transport and retirement

The Host reserves a backing slot, outcome capacity, the producer request slot and two control slots before dispatching the installation-specific `PlatformReportCaptureRequest`. For a public update it also retains the reply obligation, delivery record and acquisition slot. Platform advances capture on its ordinary outer pump. An eligible-time anchor carries the Host admission instant across the boundary. Platform transfers a complete `CBakedDocumentBlock` in `PlatformReportCaptured` or posts a fixed `PlatformReportCaptureFailed`. The owning offer uses `buffer_bytes()`, including the appended schema payloads. A busy offer keeps the same frozen owner until transport accepts it.

Host authenticates the installation and capture ID, copies the complete range to its aligned storage and checks its schema, references, query outcomes, relationships and effective limits. Publication changes current on the Host owner thread after the lifecycle/deadline recheck. The update reply acquires that same backing. Host releases incoming ownership and sends `PlatformReportCaptureReceipt`. The consumed receipt prefix or a proved terminal drain retires the capture. Host waits for the consumed request prefix before sending `PlatformReportCaptureCancel`. A departing requester leaves capture with Host. An ambiguous public delivery holds the exposed backing through recipient quiescence and drain.

## Qualification

This checkpoint implements the initial two-format, 68-byte mode rows and synchronous Platform bake. The agreed single-RGBA8 profile, 52-byte values, sharing of identical mode lists and generic Host schema composition/baking on conditioning are follow-up work. Owner-thread placement and the Win32 Debug native deadline remain open. [Implementation and validation record](platform_report_validation.md) · [Staged design](pso_creation_stage1.md)
