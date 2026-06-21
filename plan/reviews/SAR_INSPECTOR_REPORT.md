# SAR Inspector Report

Inspector role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: current repository inspection only. No redesign. No implementation.

Repository state at inspection time:

- Observed: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md` was already modified in the working tree before this report write.
- Observed: This inspector pass updates only `plan/reviews/SAR_INSPECTOR_REPORT.md`.
- Inferred: Findings describe the checked-out repository files and do not rely on external datasets, local hardware, or network state.
- Unknown: No test suite was executed during this inspector-only pass.

## 1. Current Type Model

- Observed: The canonical SAR accel-token type is defined in `examples/SAR/include/sar/SarMessages.hpp` as `SarAccelControlToken = graph::gpu::accel::ControlToken<SarSidecar>`.
- Observed: `SarSidecar` carries SAR identity, execution metadata, backend state, transfer/kernel queue IDs, merge state, counters, and `SarStageTimingMetrics`.
- Observed: `SarMessages.hpp` states that SAR identity derives from sidecar fields and that `device_view.ready_event` and `host_view.host_ptr` are opaque transport metadata only.
- Observed: `SarBackendKind` has `Host`, `SimulatedDevice`, and `NativeDevice`; `ToAccelBackendKind` maps both simulated and native device states to GraphX accel `Metal`.
- Observed: `SarDiagnosticsSnapshot` embeds the sidecar and normalized diagnostic counters/timings.
- Observed: Standard-layout assertions exist for `SarSidecar`, `SarAccelControlToken`, and `SarDiagnosticsSnapshot`.
- Observed: `examples/SAR/include/sar/SarPhaseHistoryModel.hpp` defines `SarPhaseHistoryVector`, `SarPhaseHistorySegment`, `SarAperturePartition`, `SarPhaseHistoryApertureFrame`, and `SarPhaseHistoryControlMessage`.
- Observed: `SarPhaseHistoryControlMessage` contains a `SarAccelControlToken control` plus a `SarPhaseHistoryApertureFrame frame`.
- Observed: `examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp` defines `FocusedImageResult`, which contains a `SarAccelControlToken control` plus focused-image grid, pixels, hashes, and lineage fields.
- Observed: Generic accel transport types live under `libgpu/include/gpu/accel/types`, including host/device views, leases, transfer tickets, kernel tickets, and `ControlToken<SidecarT>`.
- Inferred: The repository has three SAR-facing type layers: accel-token transport/identity, CRSD phase-history/focused-image payloads, and normalized SAR product I/O models.
- Unknown: Whether the CRSD phase-history/result types are intended as permanent public graph contracts or transitional product-lane contracts.

## 2. Current Node Model

- Observed: The definitive stripmap topology in `examples/SAR/config/sar_stripmap_definitive.json` is:

```text
SyntheticApertureIqSourceNode
  -> RangeWindowNode
  -> RangeCompressionNode
  -> AzimuthTileSplitNode
  -> H2DAsyncAccelNode
  -> SarBackprojectionTransformAccelNode
  -> D2HAsyncAccelNode
  -> ImageTileMergeNode
  -> SarDiagnosticsSinkNode
```

- Observed: SAR plugins exist for synthetic source, GOTCHA replay source, range window, range compression, azimuth split, pulse fanout, H2D, backprojection, D2H, image merge, diagnostics sink, materialized image sink, visualization sink, CRSD input source, CRSD aperture adapter, CRSD focused-image CPU transform, CRSD focused-image Metal transform, and CRSD focused-image sink.
- Observed: Stripmap DSP/transport nodes use `SarAccelControlToken` ports.
- Observed: `H2DAsyncAccelNode`, `D2HAsyncAccelNode`, and `SarBackprojectionTransformAccelNode` are real GraphX named nodes with `SarAccelControlToken` input/output ports.
- Observed: `SarBackprojectionTransformAccelNode` embeds/uses `graph::gpu::metal::nodes::DeviceKernelNodeMetal` for native Metal kernel execution when GPU capabilities are bound.
- Observed: `OrderedCrsdSetInputSourceNode` emits `SarAccelControlToken` records for CRSD segments.
- Observed: `CrsdApertureAssemblyAdapterNode` consumes `SarAccelControlToken` and emits `SarPhaseHistoryControlMessage`.
- Observed: `CrsdFocusedImageTransformNode` and `CrsdFocusedImageTransformMetalNode` consume `SarPhaseHistoryControlMessage` and emit `FocusedImageResult`.
- Observed: `docs/sar/metal_node_truth_in_labeling.md` and tests classify `CrsdFocusedImageTransformMetalNode` as an experimental incomplete domain algorithm.
- Inferred: The maintained SAR stripmap lane is accel-token centered, while the CRSD focused-image lane crosses into typed domain messages after aperture assembly.
- Unknown: Which non-definitive SAR configs are intended as maintained user-facing presets versus fixtures, experiments, or local-only lanes.

## 3. Current Token/Data Flow

- Observed: The stripmap flow preserves `SarAccelControlToken` from source through diagnostics sink.
- Observed: `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode` populate sidecar identity and host-view metadata.
- Observed: `RangeWindowNode`, `RangeCompressionNode`, `AzimuthTileSplitNode`, and `ImageTileMergeNode` update sidecar metadata and stage timing while preserving token structure.
- Observed: `H2DAsyncAccelNode` validates host input, creates synthetic device-view metadata, records lease/transfer-ticket fields, and updates H2D sidecar fields.
- Observed: `D2HAsyncAccelNode` validates device input, creates opaque host-view metadata, records lease/transfer-ticket fields, and updates D2H sidecar fields.
- Observed: `SarBackprojectionTransformAccelNode` updates device/kernel metadata and either uses the native Metal kernel node path or a simulated device path.
- Observed: `runtime::OpaqueHostPointer`, `runtime::OpaqueReadyEventNotSignaled`, and `runtime::SyntheticDevicePointer` are centralized in `examples/SAR/include/sar/SarRuntimeHelpers.hpp`.
- Observed: `test_sar_transport_opaque_contract.cpp` verifies that `host_ptr` and `ready_event` do not define SAR identity.
- Observed: The CRSD focused-image Metal graph routes `OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> CrsdFocusedImageTransformMetalNode`, so graph-level H2D/kernel/D2H stages are not separately visible in that topology.
- Inferred: The stripmap token/data flow is consistent with the sidecar-as-identity model; the CRSD image-formation flow preserves token lineage inside larger message/result structs rather than as the edge type itself.
- Unknown: Whether CRSD typed domain edges are intentionally exempt from a strict all-edge accel-token architecture.

## 4. Resolver Substitution Flow

- Observed: `libgraph/src/graph/NodeResolutionRegistry.cpp` defines default generic GPU intents for `H2DAsyncNode`, `D2HAsyncNode`, `DeviceTransformNode`, `DeviceKernelNode`, `DeviceReduceNode`, and `QueueSyncNode`.
- Observed: `NodeResolutionRegistry::AddMappings` loads JSON resolver mappings into the registry.
- Observed: `ResolvingNodeProvider` resolves intent types using execution backend, fallback policy, backend preferences, and concrete variants.
- Observed: Auto backend preference order is `metal`, `sycl`, `stub`, `cuda`.
- Observed: `sar_stripmap_definitive.json` adds resolver mappings for `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode`.
- Observed: The definitive SAR mappings declare `input_token_type` and `output_token_type` as `SarAccelControlToken`.
- Observed: The SAR mappings for metal/stub/cuda/sycl all resolve to the same SAR-specific concrete class names, not to generic libgpu node class names.
- Observed: `test_sar_json_runtime.cpp` verifies portable intent names, strict resolver contracts, selected backend diagnostics, and `SarAccelControlToken` mapping strings.
- Inferred: SAR resolver diagnostics can identify a selected backend while still instantiating the same SAR-specific concrete node.
- Unknown: Whether any production SAR topology is expected to substitute directly to generic libgpu nodes through the default registry.

## 5. Violations Of Accel-Token Architecture

- Observed: The stripmap SAR GPU path uses `SarAccelControlToken` edge types and rejects legacy payload-contract names in guardrail tests.
- Observed: Legacy payload names such as `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` appear as negative-validation artifacts, not active stripmap payload types.
- Observed: `CrsdFocusedImageTransformNode`, `CrsdFocusedImageTransformMetalNode`, and `CrsdFocusedImageSinkNode` expose `SarPhaseHistoryControlMessage` and `FocusedImageResult` as graph edge types.
- Observed: Several CRSD configs declare top-level `"edge_contract": "accel-token"` even when the full graph contains non-token phase-history/result edges.
- Observed: SAR-specific H2D/D2H accel nodes do not substitute directly to generic libgpu `H2DAsyncNodeMetal` or `D2HAsyncNodeMetal`.
- Observed: `SarBackprojectionTransformAccelNode` wraps native Metal kernel execution internally instead of exposing a graph-level generic `DeviceKernelNodeMetal` stage in the definitive stripmap topology.
- Observed: `examples/SAR/main.cpp` reports graph load/completion plus queue backpressure and peak queue depth, but detailed timing/performance reporting is primarily in `examples/SAR/src/sar_benchmark.cpp` and diagnostics tests.
- Inferred: Under a strict "every SAR graph edge is `AccelControlToken<SarSidecar>`" reading, the CRSD focused-image graph violates the architecture.
- Inferred: Under a narrower "stripmap GPU transport must preserve accel-token identity" reading, the stripmap path largely satisfies the architecture and the ambiguity is scoped to CRSD/focused-image product edges.
- Unknown: Which interpretation is intended as the current repository rule.

## 6. Obsolete Abstractions

- Observed: Old alias headers named `H2DAsyncNode.hpp`, `D2HAsyncNode.hpp`, and `SarBackprojectionTransformNode.hpp` are absent under `examples/SAR/include/sar`.
- Observed: Legacy SAR payload names remain only in tests/guardrails that reject them.
- Observed: `CollectiveReduceNodeMetal` is present in the Metal inventory but documented/tested as unsupported.
- Observed: `CrsdFocusedImageTransformMetalNode` is active and dynamically loadable, but it self-reports `experimental_incomplete` status.
- Observed: `SarBackendKind`, graph resolver backend strings, node-level `backend`, and node-level `backend_id` coexist.
- Observed: `SarRuntimeHelpers.hpp` contains `ResolveDiagnosticsSink`, which uses `NodeFacadeAdapterWrapper` to locate `SarDiagnosticsSinkNode` after graph execution.
- Inferred: The legacy message-payload abstraction has mostly been removed from active stripmap code.
- Inferred: SAR-specific transfer nodes may become obsolete if the intended future model is graph-level generic GPU nodes with sidecar-preserving token boundaries.
- Unknown: Whether the experimental/unsupported Metal nodes are retained as intentional future extension points or temporary scaffolding.

## 7. Complexity Hotspots

- Observed: SAR has parallel lanes: stripmap accel-token processing, CRSD ordered-set ingestion, aperture assembly, focused-image formation, GOTCHA/CRSD conversion, and external comparison tooling.
- Observed: `CrsdFocusedImageTransformMetalNode` combines CPU seed-image generation, capability binding, host/device allocation, H2D/D2H operations, inline Metal source, kernel launch, fallback behavior, diagnostics, and result construction.
- Observed: SAR configs contain overlapping backend controls: top-level `execution_backend`, resolver mappings, node-level `backend`, node-level `backend_id`, and some node-specific `execution_backend` fields.
- Observed: Resolver mappings repeat the same SAR concrete node for multiple backend variants in maintained stripmap configs.
- Observed: `examples/SAR/src/sar_benchmark.cpp` is a large multi-purpose executable covering graph execution, baseline parity, benchmark statistics, trace JSON, and optional device-reduce evaluation.
- Observed: `examples/SAR/main.cpp` and `sar_benchmark.cpp` have separate reporting surfaces with different diagnostic depth.
- Observed: `SarRuntimeHelpers.hpp` defines one SAR-specific `ElapsedUs` helper; no duplicate `elapsedUs` function was found in `examples/SAR`, `libgraph`, `libgpu`, or `libdsp`.
- Observed: `CrsdFocusedImageTransformMetal.cpp` also performs a local microsecond duration calculation for focused-image transform elapsed time.
- Inferred: The highest current complexity risk is truth-in-labeling across Metal, CRSD, fallback, and experimental-incomplete paths.
- Unknown: Whether all local-only GOTCHA/SarPy workflows remain stable across developer machines.

## 8. Blockers For `AccelControlToken<SarSidecar>`

- Observed: `SarAccelControlToken` exists and is used throughout the definitive stripmap pipeline.
- Observed: Generic libgpu nodes operate on generic accel buffer views rather than `AccelControlToken<SarSidecar>`.
- Observed: SAR-specific H2D/D2H nodes preserve sidecar identity while constructing synthetic or opaque transport metadata.
- Observed: The default resolver registry has generic GPU contracts but no default sidecar-preserving SAR token adapters.
- Observed: CRSD aperture assembly emits `SarPhaseHistoryControlMessage`, and focused-image transforms/sinks use `FocusedImageResult`.
- Observed: Focused-image results carry image pixels, grid metadata, hashes, pulse counts, and lineage outside plain sidecar fields.
- Inferred: The definitive stripmap lane is not blocked on `AccelControlToken<SarSidecar>`.
- Inferred: A single all-SAR-edge token architecture is blocked by the current CRSD phase-history/result edge model.
- Inferred: A single generic-GPU-node SAR topology is blocked by the absence of graph-visible sidecar-preserving wrappers around generic libgpu transfer/kernel node contracts.
- Unknown: Whether the target architecture should embed CRSD image payloads into accel-token sidecars, carry them as token-referenced host/device buffers, or keep them as higher-level domain messages.

## 9. Existing External Comparison/Baseline Hooks

- Observed: `examples/SAR/tools/sar_image_comparator.py` supports deterministic internal references and external baselines.
- Observed: `examples/SAR/tools/gotcha_back_adapter.py` scaffolds and normalizes gotcha-back external baseline artifacts.
- Observed: `examples/SAR/tools/sar_local_runner.py` prepares local GraphX/external reference boundaries and explicitly does not download data or run external tools automatically.
- Observed: `tools/sarpy` contains scripts for CRSD validation, CRSD reference-image generation, image comparison, classic MAT conversion, and SarPy-based CRSD writing.
- Observed: `.github/workflows/sarpy-integration.yml` is opt-in and labeled for SarPy integration.
- Observed: Tests cover external baseline policy/registry, SarPy harness behavior, image comparison, gotcha-back adapter contracts, local runner contracts, and local-only validation gates.
- Observed: Docs state SarPy is optional/local-only and not a GraphX runtime dependency.
- Inferred: External packages are currently comparison, validation, writer, and local workflow helpers rather than GraphX core runtime dependencies.
- Unknown: Whether the SarPy writer bridge is intended as the long-term standards-targeted CRSD writer path.

## Current-State Summary

- Observed: The repository contains a mature stripmap SAR lane built around `AccelControlToken<SarSidecar>`, sidecar identity, resolver diagnostics, plugin loading, and transport opacity guardrails.
- Observed: The repository also contains a broader GOTCHA/CRSD/focused-image lane with normalized products, CRSD readers/writers, SarPy-assisted tooling, local-only real-data validation, image comparison, and truth-in-labeling guardrails.
- Observed: Generic Metal/GPU infrastructure exists in `libgpu`, while the SAR stripmap graph currently uses SAR-specific accel-token nodes and an internal native-kernel bridge.
- Observed: The main executable is covered by `test_sar_main_executable.cpp` for runtime/completion/basic diagnostics; richer performance and trace reporting live in benchmark/diagnostics paths.
- Inferred: The current architecture is hybrid: strict accel-token transport for stripmap SAR processing plus typed CRSD phase-history/focused-image product edges beside it.
- Inferred: The central architectural ambiguity is whether CRSD focused-image edges are accepted as a bounded product-level exception or should be converted into the same accel-token architecture as the stripmap lane.
- Unknown: No conclusion is made here about which architecture should be chosen next, because this inspector report is limited to current-state analysis.
