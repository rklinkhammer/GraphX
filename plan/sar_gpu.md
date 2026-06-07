# SAR GPU Node Model Migration Plan

## 1) Objective

Migrate SAR node behavior to the GPU node model while preserving SAR domain semantics:

1. edges are control-plane tokens (metadata, leases, tickets),
2. edges do not imply byte movement,
3. backend work remains behind accel/capability boundaries.

This plan is intentionally SAR-local and execution-oriented.

## 2) Scope and Boundaries

Allowed code locations:

1. `examples/SAR/include/sar/...`
2. `examples/SAR/src/...`
3. `examples/SAR/test/...`

Out of scope unless blocked by hard dependency:

1. `libgpu/...`
2. `libgraph/...`
3. framework-wide resolver/provider changes

Non-goals:

1. full SAR math fidelity improvements,
2. scheduler/framework refactors,
3. full-suite validation.

## 3) Migration Contract (Definition of Done)

Each touched SAR stage is done only when all checks below pass:

1. **Token-edge compliance**
   - edge-visible structs carry token/context metadata,
   - no edge contract relies on payload pointer identity as data-plane ownership.
2. **Lease/ticket continuity**
   - transfer and kernel tickets propagate across H2D -> transform -> D2H -> merge/diagnostics.
3. **Marker continuity**
   - Data/Watermark/EOS semantics remain deterministic.
4. **SAR semantics preserved**
   - diagnostics counters and merge completeness behavior remain valid.

## 4) Work Phases (Smallest-change-first)

### Phase Entry Rule

Before starting any phase:

1. list candidate files to touch,
2. list intended metadata fields to add/update,
3. pre-select exact test filters for that phase.

### Phase A: Boundary Transfer Nodes

Targets:

1. `H2DAsyncNode`
2. `D2HAsyncNode`

Likely files:

1. `examples/SAR/src/H2DAsyncNode.cpp`
2. `examples/SAR/src/D2HAsyncNode.cpp`
3. `examples/SAR/include/sar/H2DAsyncNode.hpp` (only if config/contract surface changes)
4. `examples/SAR/include/sar/D2HAsyncNode.hpp` (only if config/contract surface changes)
5. `examples/SAR/test/test_sar_gpu_metadata_contract.cpp`

Required outcomes:

1. host/device views modeled as token handles + metadata,
2. transfer ticket fields always coherent,
3. no implicit byte-move semantics in edge contracts.

Implementation details:

1. Ensure `gpu.host_view.host_ptr` and `gpu.device_view.device_ptr` at SAR boundary are token/context handles, not ownership-bearing payload pointers.
2. Ensure `gpu.transfer_ticket` always sets:
   - `backend`,
   - `transfer_id`,
   - `execution_queue_id`,
   - `completion_event`,
   - valid `src/dst` view linkage.
3. Keep payload vectors (`range_bins`, `pixels`) as SAR-domain message payloads without using pointer identity as data-plane ownership proof.

Phase A acceptance checks:

1. `SarGpuMetadataContractTest.PropagatesAccelMetadataAcrossDeviceStagesAndMerge`
2. Any touched H2D/D2H node-specific tests (exact filter names only)

### Phase B: Kernel Intent Stage

Target:

1. `SarBackprojectionTransformNode`

Likely files:

1. `examples/SAR/src/SarBackprojectionTransformNode.cpp`
2. `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp` (if fields/config surface change)
3. `examples/SAR/test/test_sar_backprojection_transform_node.cpp`
4. `examples/SAR/test/test_sar_gpu_metadata_contract.cpp` (if kernel metadata propagation assertions expand)

Required outcomes:

1. kernel ticket consistently populated for device-intent execution,
2. queue/kernel metadata deterministic and propagated.

Implementation details:

1. Ensure `gpu.kernel_ticket` includes deterministic:
   - `backend`,
   - `kernel_id`,
   - launch dimensions,
   - `arg_count`,
   - `execution_queue_id`,
   - `completion_event`.
2. Ensure EOS path does not drop required metadata continuity unexpectedly.
3. Preserve current SAR tile output behavior while treating kernel metadata as intent contract.

Phase B acceptance checks:

1. `SarBackprojectionTransformNodeTest.*`
2. `SarGpuMetadataContractTest.PropagatesAccelMetadataAcrossDeviceStagesAndMerge`

### Phase C: Fan-in and Diagnostics

Targets:

1. `ImageTileMergeNode`
2. `SarDiagnosticsSinkNode`

Likely files:

1. `examples/SAR/src/ImageTileMergeNode.cpp`
2. `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
3. `examples/SAR/include/sar/ImageTileMergeNode.hpp` (if counters/config surface changes)
4. `examples/SAR/include/sar/SarDiagnosticsSinkNode.hpp` (if diagnostics contract surface changes)
5. `examples/SAR/test/test_image_tile_merge_node.cpp`
6. `examples/SAR/test/test_sar_diagnostics_contract.cpp`
7. `examples/SAR/test/test_sar_json_pipeline.cpp`
8. `examples/SAR/test/test_sar_json_runtime.cpp`

Required outcomes:

1. ticket/lease/timing metadata preserved through merge,
2. duplicate/missing/out-of-order accounting unaffected.

Implementation details:

1. Ensure merge output (`SarMergeStatusMessage`) carries expected GPU metadata fields from upstream (`lease`, `transfer_ticket`, `kernel_ticket` where present).
2. Ensure diagnostics aggregation includes timing and ticket-derived counters without altering deterministic expected values unexpectedly.
3. Keep watermark/EOS completion policy behavior intact.

Phase C acceptance checks:

1. `ImageTileMergeNodeTest.*`
2. `SarDiagnosticsContractTest.*`
3. `SarJsonPipelineTest.*`
4. `SarJsonRuntimeTest.*`

### Phase D: Upstream SAR Stages

Targets:

1. `SyntheticApertureIqSourceNode`
2. `RangeWindowNode`
3. `RangeCompressionNode`
4. `AzimuthTileSplitNode`

Likely files:

1. `examples/SAR/src/SyntheticApertureIqSourceNode.cpp`
2. `examples/SAR/src/RangeWindowNode.cpp`
3. `examples/SAR/src/RangeCompressionNode.cpp`
4. `examples/SAR/src/AzimuthTileSplitNode.cpp`
5. matching headers under `examples/SAR/include/sar/...` only if required
6. touched node-specific tests under `examples/SAR/test/...`

Required outcomes:

1. metadata continuity into boundary nodes,
2. no hidden data-plane assumptions at edge boundaries.

Implementation details:

1. Preserve envelope identity continuity (`sequence_id`, `tile_id`, marker fields) and backend metadata through preprocessing stages.
2. Avoid introducing direct backend calls in upstream SAR stages; keep backend behavior at boundary/capability intent stages.
3. Keep payload transformation semantics deterministic.

Phase D acceptance checks:

1. `SyntheticApertureIqSourceNodeTest.*`
2. `RangeWindowNodeTest.*`
3. `RangeCompressionNodeTest.*`
4. `AzimuthTileSplitNodeTest.*`

## 5) Test Strategy (Narrow Only)

Build target:

1. `cd /Users/rklinkhammer/workspace/GraphX/build && ninja test_sar_example_unit`

Run only touched tests with exact filters. Recommended baseline set:

1. `./examples/SAR/test/test_sar_example_unit '--gtest_filter=SarGpuMetadataContractTest.*'`
2. `./examples/SAR/test/test_sar_example_unit '--gtest_filter=SarJsonPipelineTest.*'`
3. `./examples/SAR/test/test_sar_example_unit '--gtest_filter=SarJsonRuntimeTest.*'`

Add any directly impacted node/unit filters only when those files are edited.

Per-phase command pattern:

1. Build once per change batch:
   - `cd /Users/rklinkhammer/workspace/GraphX/build && ninja test_sar_example_unit`
2. Run only phase filters, example:
   - `./examples/SAR/test/test_sar_example_unit '--gtest_filter=SarGpuMetadataContractTest.PropagatesAccelMetadataAcrossDeviceStagesAndMerge'`
   - `./examples/SAR/test/test_sar_example_unit '--gtest_filter=SarBackprojectionTransformNodeTest.*'`
   - `./examples/SAR/test/test_sar_example_unit '--gtest_filter=ImageTileMergeNodeTest.*:SarDiagnosticsContractTest.*'`

Review evidence requirement:

1. Every phase PR note must include exact filters used.
2. Every touched node must have at least one directly relevant test filter executed.

## 6) Execution Workflow

For each phase:

1. apply smallest code change,
2. run only directly impacted test filters,
3. proceed only if green,
4. avoid touching unrelated files.

Finalization:

1. stage only SAR files touched by this migration,
2. commit message: `SAR: migrate nodes to GPU token model`,
3. push current PR branch,
4. post concise PR update with evidence.

Change-size guardrails:

1. Prefer <= 3 files per micro-step when feasible.
2. If a phase requires > 8 SAR files, split into sub-phases and validate separately.
3. Do not mix documentation-only edits with behavior edits in the same commit.

## 7) PR Update Template

1. Changed files: `<list>`
2. Build command: `<exact command>`
3. Test command(s): `<exact command(s)>`
4. Result: `pass/fail` with exit code
5. Commit: `<hash>`
6. Note: one-line behavior/risk summary

## 8) Blocker Rule

If blocked by non-SAR dependency, stop and ask one concrete question including:

1. exact external file path needed,
2. why SAR-local implementation is insufficient,
3. minimal external change required.

## 9) Ordered Execution Recommendation

1. Phase A + `SarGpuMetadataContractTest`
2. Phase B + transform-focused tests
3. Phase C + diagnostics/runtime tests
4. Phase D + directly affected upstream tests
5. final narrow regression of touched filters
6. commit, push, PR update

## 10) Review Checklist (Gate)

Reviewer should confirm all items before phase sign-off:

1. No files outside SAR touched (unless blocker exception explicitly documented).
2. Edge contracts represent tokens/metadata, not implicit byte movement.
3. Lease/ticket fields are present and internally coherent in touched stages.
4. EOS/watermark/data marker behavior remains deterministic.
5. Only narrow SAR filters were run; no full-suite evidence in phase report.
6. Commit scope matches phase intent and excludes unrelated refactor noise.