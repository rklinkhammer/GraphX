# SAR Simplifier Report

Role applied: `PRINCIPAL_ARCHITECT` decision pass using the requested simplifier output shape.

Authoritative inputs:

- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- Prior `plan/reviews/SAR_SIMPLIFIER_REPORT.md`

Scope: target architecture only. No implementation. No PR plan.

## 1. Target Type Model

- `SarAccelControlToken = AccelControlToken<SarSidecar>` is the only maintained SAR GPU/runtime edge type.
- `SarSidecar` is the only SAR identity, lineage, ordering, backend, diagnostic, timing, checksum, and artifact-reference carrier inside GraphX SAR execution.
- `host_view.host_ptr`, `device_view.device_ptr`, and `device_view.ready_event` are opaque transport handles only. They must never encode SAR identity, product identity, pulse identity, CRSD identity, file identity, or algorithm state.
- Generic libgpu buffer views, leases, transfer tickets, kernel tickets, queues, sync handles, and primitive kernel descriptors remain generic libgpu types.
- `NormalizedSarProduct` remains the C++ product/I/O domain model for GOTCHA/CRSD conversion, validation, reports, and file writers. It is not a graph runtime payload.
- CRSD reader, CRSD writer, GOTCHA reader, manifest, conversion report, validation report, comparison report, checksum, and sidecar JSON schemas are artifact/product contracts, not GPU graph message contracts.
- `SarPhaseHistoryControlMessage` is not a maintained GPU graph edge type. If phase-history data participates in GraphX execution, it is referenced through `SarAccelControlToken` sidecar lineage plus validated buffer/artifact references.
- `FocusedImageResult` is not a maintained internal GPU graph edge type. Focused-image pixels are either token-referenced output buffers or endpoint sink artifacts.
- External baseline outputs, SarPy validation products, gotcha-back references, and image-comparison artifacts are evidence artifacts only. They do not define GraphX runtime types.
- Planning-era names, PR-number names, compatibility aliases, and legacy SAR payload names are not valid active type names.

## 2. Target Node Model

- The only maintained SAR GPU runtime shape is:

```text
SAR source / product ingest
    ->
SAR tokenization / deterministic assembly
    ->
SarAccelControlToken
    ->
generic libgpu transfer / sync / kernel primitives through one sidecar-preserving token boundary
    ->
SarAccelControlToken
    ->
SAR merge / materialization / diagnostics / artifact sinks
```

- SAR source and ingest nodes stay in `examples/SAR`; they emit `SarAccelControlToken` with complete deterministic sidecar identity.
- SAR CPU preparation nodes stay in `examples/SAR` only when they preserve `SarAccelControlToken` and perform real, tested work.
- Generic GPU transfer, sync, memory, and primitive kernel nodes stay in `libgpu` and must not know SAR, GOTCHA, CRSD, SarPy, or focused-image concepts.
- SAR GPU work is expressed as SAR-owned descriptors and sidecar metadata feeding generic GPU primitives. Domain setup may be SAR-specific; transfer/kernel execution must be generic and topology-visible.
- `H2DAsyncAccelNode` and `D2HAsyncAccelNode` are not the target endpoint. They are replaced by one sidecar-preserving generic GPU transfer pattern.
- `SarBackprojectionTransformAccelNode` is not the target endpoint as a monolithic wrapper. It is replaced by SAR descriptor preparation plus a topology-visible generic GPU kernel stage.
- `OrderedCrsdSetInputSourceNode` may remain as product ingest only if its output is tokenized and deterministic.
- `CrsdApertureAssemblyAdapterNode` may remain only as a token-preserving assembly stage or a product/tooling stage outside the maintained GPU runtime path.
- `CrsdFocusedImageTransformNode` may remain as a CPU reference/product node only if honestly labeled and bounded outside native-GPU claims.
- `CrsdFocusedImageTransformMetalNode` is not a maintained production node until it implements a complete native Metal focused-image algorithm end to end; until then it is experimental/test-only or deleted from active configs.
- Sinks may emit images, reports, checksums, and comparison artifacts, but graph execution into those sinks remains tokenized.

## 3. Deletion List

- Delete active maintained graph edges that use `SarPhaseHistoryControlMessage`.
- Delete active maintained graph edges that use `FocusedImageResult`.
- Delete or quarantine configs that declare `"edge_contract": "accel-token"` while containing non-token runtime edges.
- Delete active SAR-specific transfer nodes after the generic sidecar-preserving transfer boundary exists.
- Delete monolithic SAR GPU wrapper behavior that hides generic transfer/kernel stages from the graph.
- Delete resolver mappings that imply backend substitution while every backend maps to the same SAR concrete type.
- Delete duplicate backend control surfaces where one topology uses overlapping top-level resolver settings, node `backend`, node `backend_id`, and node-specific `execution_backend` fields for the same decision.
- Delete unsupported `CollectiveReduceNodeMetal` from active supported-node/plugin claims, or keep it only in explicit unsupported inventory with no production config exposure.
- Delete production claims for `CrsdFocusedImageTransformMetalNode` while it is experimental incomplete.
- Delete active tests, docs, configs, scripts, fixtures, and user-visible strings that exist only to preserve historical PR/planning states.
- Delete tracked Python cache artifacts under SAR/tooling directories.
- Delete compatibility aliases and shims for obsolete SAR GPU names or legacy SAR payload names.
- Delete documentation or output that implies intermediate GraphX artifacts, surrogate reference imagery, or external harness products are standards-compliant CRSD or complete native Metal SAR results when they are not.

## 4. Replacement List

- Replace SAR-specific H2D/D2H graph nodes with one sidecar-preserving token adapter around generic libgpu H2D/D2H transfer primitives.
- Replace monolithic SAR backprojection GPU nodes with SAR descriptor construction plus topology-visible generic libgpu kernel execution.
- Replace CRSD phase-history graph payloads with token-referenced validated phase-history buffers or artifact handles.
- Replace focused-image internal graph payloads with token-referenced output buffers and sink-owned image artifacts.
- Replace mixed-message `"edge_contract": "accel-token"` configs with all-token runtime configs or explicitly non-runtime product/tooling configs.
- Replace repeated backend fields with one canonical backend/resolver control surface per topology.
- Replace vague Metal labels with truth-in-labeling states: `native-complete`, `native-primitive`, `fallback`, `experimental-incomplete`, or `unsupported`.
- Replace the SarPy-backed CRSD writer facade as a core-looking writer with an explicit external harness boundary unless a native C++ CRSD writer is approved.
- Replace coupled tiny-JSON/binary CRSD reader behavior with separate fixture-reader and real-CRSD-reader boundaries if both remain.
- Replace planning-era filenames and test names with capability names; keep historical reports only in explicit history locations.
- Replace local-only workflow sprawl with explicit environment-gated scripts/tests that are excluded from required CI unless they use tiny deterministic fixtures.

## 5. Architecture Invariants

- Correctness outranks determinism; determinism outranks architecture; architecture outranks observability; observability outranks performance; performance outranks convenience.
- There is exactly one canonical SAR GPU/runtime path.
- Every maintained SAR GPU/runtime edge carries `SarAccelControlToken`.
- `SarSidecar` is the only SAR identity carrier inside runtime execution.
- Transport handles are never identity.
- Generic libgpu APIs remain SAR-free.
- SAR code may own domain descriptors, metadata mapping, ingest, validation, diagnostics, and artifact semantics, but not a parallel GPU runtime architecture.
- Resolver diagnostics must tell the truth: selected backend must not imply a different implementation when the concrete node did not change.
- Metal-named domain nodes must execute the advertised native algorithm end to end or be explicitly experimental/unsupported and excluded from production claims.
- Simulated capabilities and CPU fallback are not evidence of native Metal algorithm support.
- `examples/SAR/main.cpp` remains tested and must report meaningful runtime/performance diagnostics, not just completion.
- CRSD compliance is literal. Intermediate GraphX products and local harness outputs must not be named or reported as CRSD unless standards-targeted CRSD is actually produced.
- `--mode crsd` means standards-targeted CRSD only; otherwise it fails before writing misleading output.
- External packages validate or compare GraphX artifacts only. They do not define `libgraph`, `libgpu`, or SAR runtime contracts.
- Local real-data workflows are optional, explicitly gated, and outside required CI.
- Backward compatibility with obsolete SAR architecture is not required.
- Compatibility shims are forbidden.

## 6. Open Questions That Block Planning

- Is CRSD focused-image processing part of the maintained GraphX runtime path, or should it be demoted to a product/tooling lane until it can be tokenized?
- If CRSD focused-image processing remains in runtime, what is the exact token-referenced buffer contract that replaces `SarPhaseHistoryControlMessage`?
- Should `FocusedImageResult` be deleted from internal graph edges entirely and kept only as a sink artifact object?
- Is a native C++ standards-targeted CRSD writer required, or is the SarPy writer bridge acceptable as an explicitly external harness?
- Which `examples/SAR/config` files are maintained canonical configs, which are fixtures, and which should be deleted?
- Should unsupported or experimental Metal plugins remain buildable, or should they be moved behind non-default inventory/test-only gates?
- What exact performance fields must `examples/SAR/main.cpp` report to satisfy the architecture role requirements?
- Should binary CRSD reading and tiny JSON fixture reading be split into separate reader classes?
- Which historical reports remain in active `plan/reviews`, which move to history, and which are deleted?
- Are non-GPU product conversion lanes allowed to use `NormalizedSarProduct` while the runtime GPU path remains token-only?
