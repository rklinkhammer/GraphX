# SAR Inspector Report

Inspector role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: current repository inspection only. No redesign. No implementation.

Repository state at inspection time:

- Observed: The working tree contains one unrelated modified file: `examples/SAR/config/sar_crsd_gotcha_local_validation.json`.
- Inferred: The report below describes current checked-out repository files and does not assume that modified config is intentional or complete.

## 1. Current Type Model

- Observed: The canonical SAR accel-token type model is in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` carries SAR identity, backend, transfer queue, merge, timing, and diagnostic fields.
- Observed: `AccelControlToken<SidecarT>` carries `sidecar`, `BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, `KernelTicket`, and presence flags.
- Observed: `SarAccelControlToken` is `AccelControlToken<SarSidecar>`.
- Observed: Contract comments state SAR identity must come from sidecar fields, while `host_view.host_ptr` and `device_view.ready_event` are opaque transport metadata.
- Observed: `SarPhaseHistoryModel.hpp` adds CRSD-focused phase-history types: vectors, segments, aperture frames, ownership, layout, sample format, partition metadata, and `SarPhaseHistoryControlMessage`.
- Observed: `examples/SAR/include/sar/io/NormalizedSarProduct.hpp` contains the normalized SAR product model used by GOTCHA/CRSD conversion paths.
- Observed: Standard-layout assertions exist for `SarSidecar`, `SarAccelControlToken`, and `SarDiagnosticsSnapshot`.
- Inferred: The current type model has two active layers: generic SAR accel-token transport and newer CRSD phase-history/focused-image payloads.
- Unknown: Whether downstream non-repository consumers depend on older planning-era SAR payload names.

## 2. Current Node Model

- Observed: The definitive SAR topology in `examples/SAR/config/sar_stripmap_definitive.json` is:
  `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncAccelNode -> SarBackprojectionTransformAccelNode -> D2HAsyncAccelNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: Active SAR plugins include source, DSP/prep, transfer, backprojection, merge, diagnostics, visualization, CRSD input, CRSD aperture assembly, CRSD focused-image CPU transform, CRSD focused-image Metal transform, and CRSD focused-image sink nodes.
- Observed: `OrderedCrsdSetInputSourceNode` reads ordered CRSD sets and emits SAR tokens for CRSD segment streams.
- Observed: `CrsdApertureAssemblyAdapterNode` assembles ordered CRSD segment data into `SarPhaseHistoryControlMessage`.
- Observed: `CrsdFocusedImageTransformNode` is the CPU focused-image/backprojection path.
- Observed: `CrsdFocusedImageTransformMetalNode` is present and binds Metal capabilities, but the truth-in-labeling document and guardrails classify it as experimental incomplete.
- Observed: libgpu provides Metal nodes: `HostIngressPinnedSourceNodeMetal`, `H2DAsyncNodeMetal`, `D2HAsyncNodeMetal`, `PeerCopyNodeMetal`, `DeviceShardNodeMetal`, `LeaseReleaseNodeMetal`, `QueueSyncNodeMetal`, `HostEgressSinkNodeMetal`, `DeviceKernelNodeMetal`, `DeviceTransformNodeMetal`, `DeviceReduceNodeMetal`, and `CollectiveReduceNodeMetal`.
- Observed: `docs/sar/metal_node_truth_in_labeling.md` classifies `CollectiveReduceNodeMetal` as unsupported and `CrsdFocusedImageTransformMetalNode` as fallback plus experimental incomplete.
- Inferred: The repository currently distinguishes generic Metal primitives from SAR domain algorithm nodes, but both are active in the build/test surface.
- Unknown: Whether every config under `examples/SAR/config` is intended to remain maintained.

## 3. Current Token/Data Flow

- Observed: Definitive stripmap flow uses `SarAccelControlToken` on every SAR edge after source emission.
- Observed: `SyntheticApertureIqSourceNode` and replay/input nodes populate sidecar identity plus host views.
- Observed: `RangeWindowNode` and `RangeCompressionNode` preserve token semantics while changing DSP payload metadata.
- Observed: `AzimuthTileSplitNode` updates sidecar tile identity and uses opaque host pointer helpers.
- Observed: `H2DAsyncAccelNode` requires `has_host_view`, creates synthetic/device transport metadata, writes lease and transfer ticket fields, and updates sidecar H2D metadata.
- Observed: `SarBackprojectionTransformAccelNode` requires `has_device_view`, updates device view and kernel ticket, and can delegate native kernel execution to libgpu Metal `DeviceKernelNodeMetal` when capabilities are bound.
- Observed: `D2HAsyncAccelNode` emits host view, lease, and transfer ticket, with `host_ptr` explicitly documented as opaque.
- Observed: `ImageTileMergeNode` aggregates tile completion, merge counters, transfer byte counts, and stage timing into the sidecar.
- Observed: `SarDiagnosticsSinkNode` reports diagnostics from sidecar plus graph queue metrics.
- Observed: CRSD focused-image flow can be configured as `OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> CrsdFocusedImageTransformNode -> CrsdFocusedImageSinkNode`.
- Inferred: The mature stripmap path is accel-token native; the CRSD focused-image path carries a typed phase-history payload between adapter and transform rather than staying purely `SarAccelControlToken` through every stage.
- Unknown: Whether the phase-history control-message edge is intended as a permanent exception to the accel-token-only invariant.

## 4. Resolver Substitution Flow

- Observed: `ResolvingNodeProvider` resolves node intents through `NodeResolutionRegistry` plus JSON resolver mappings.
- Observed: Auto backend preference order is `metal`, `sycl`, `stub`, `cuda`.
- Observed: Strict backend policy uses only the requested backend; `allow_fallback` permits other backends.
- Observed: Definitive SAR config maps `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode` to concrete nodes with `SarAccelControlToken` input/output contracts.
- Observed: SAR JSON runtime tests assert maintained presets keep portable intent types and explicit `SarAccelControlToken` resolver contracts.
- Observed: Generic libgraph resolver contracts for non-SAR GPU nodes also exist and use generic accel buffer contracts.
- Inferred: SAR resolver substitution is currently token-contract aware, but SAR definitive mappings resolve to the same SAR concrete node names rather than separate backend-specific concrete node classes.
- Unknown: Whether generic GPU resolver mappings are exercised by production apps outside tests.

## 5. Violations Of Accel-Token Architecture

- Observed: Production SAR token nodes inspected do not derive SAR identity from `host_ptr` or `ready_event`.
- Observed: `test_sar_transport_opaque_contract.cpp` freezes `host_ptr` and `ready_event` as opaque transport-only fields.
- Observed: `test_sar_accel_token_guardrails.cpp` rejects legacy payload contracts under accel-token mode.
- Observed: `test_sar_token_contract.cpp` asserts canonical SAR GPU stages use explicit accel-token node names.
- Observed: `CrsdFocusedImageTransformNode` and `CrsdFocusedImageTransformMetalNode` operate on `SarPhaseHistoryControlMessage`, not `SarAccelControlToken`.
- Inferred: The main stripmap accel path follows the stated accel-token architecture, while the CRSD focused-image path is a deliberate typed-payload branch that may need an architecture decision if "all SAR edges" must literally mean `SarAccelControlToken`.
- Unknown: Whether the CRSD phase-history exception is accepted by the principal architecture.

## 6. Obsolete Abstractions

- Observed: Legacy SAR alias headers for generic `H2D`/`D2H`/backprojection names are absent from `examples/SAR/include/sar`.
- Observed: Older planning-era reports remain in `plan/reviews`, including many `SAR_CRSD_TO_FOCUSED_IMAGE_IMPL_PR*` and verifier reports.
- Observed: `examples/SAR/config` contains many scenario, manual, Metal, CRSD, and local validation configs in addition to the definitive stripmap config.
- Observed: `CrsdFocusedImageTransformMetalNode` is active but explicitly labeled experimental incomplete for the domain algorithm.
- Observed: `CollectiveReduceNodeMetal` is present as a plugin but documented/tested as unsupported.
- Inferred: Some active files are product code, while some are scaffolding, local-only workflows, or planning/verifier artifacts; the repository keeps them side by side.
- Unknown: Which active configs and reports are intended to be retained as product documentation versus history.

## 7. Complexity Hotspots

- Observed: SAR now contains both stripmap accel-token pipeline nodes and CRSD/focused-image pipeline nodes.
- Observed: CRSD support spans C++ reader/writer/model code, Python SarPy tooling, shell conversion scripts, generated binary fixtures, docs, and local-only validation tests.
- Observed: Metal has three layers: default simulated capabilities, native metal-cpp capabilities, and SAR/domain nodes that bind or wrap those capabilities.
- Observed: `CrsdFocusedImageTransformMetalNode` contains CPU fallback, capability binding, inline Metal source registration, seed-image construction, transfer orchestration, kernel launch, readback, and result construction in one class.
- Observed: Native Metal capabilities contain both real Metal objects and synthetic event bookkeeping.
- Observed: `tools/sarpy/reference_image_from_crsd.py` rejects quicklook as focused reference and emits a local surrogate reference path rather than true SarPy focused image formation.
- Inferred: The most complex boundary is not "can Metal dispatch?" but "does the domain algorithm actually execute in the intended backend and produce a scientifically meaningful focused image?"
- Unknown: Whether local real GOTCHA/CRSD workflows are stable across machines and dataset layouts.

## 8. Blockers For `AccelControlToken<SarSidecar>`

- Observed: The stripmap path has no immediate blocker for `SarAccelControlToken`.
- Observed: CRSD focused-image adapter-to-transform edges use `SarPhaseHistoryControlMessage`.
- Observed: `CrsdFocusedImageSinkNode` consumes `FocusedImageResult`, not an accel token.
- Observed: The focused-image artifact path records ordered CRSD lineage and hashes outside the plain sidecar.
- Inferred: If the architecture requires every SAR edge to be `AccelControlToken<SarSidecar>`, CRSD phase-history and focused-image result edges are blockers or intentional exceptions.
- Inferred: If typed payloads are allowed after assembly, the blocker becomes documentation/guardrails ensuring the exception is explicit and bounded.
- Unknown: Whether `SarPhaseHistoryControlMessage` should be embedded into an accel token, replaced by sidecar-referenced buffers, or accepted as a higher-level CPU/focused-image lane type.

## 9. Existing External Comparison/Baseline Hooks

- Observed: `tools/sarpy` contains local-only scripts for CRSD validation, CRSD reference image surrogate generation, GOTCHA reference generation, and image comparison.
- Observed: `examples/SAR/tools` contains local runner, image comparator, SarPy metadata harness, gotcha-back adapter, and CPU reference backprojection docs/tools.
- Observed: `SAR_EXTERNAL_BASELINE_POLICY.md` and `SAR_BASELINE_PACKAGE_REGISTRY.json` exist under `plan/reviews`.
- Observed: Tests cover SarPy probe/metadata/reference/compare harnesses, graphx image comparison lane, baseline comparison, local runner contract, and real full-aperture local validation.
- Observed: Docs state SarPy/reference workflows are optional/local-only and not GraphX runtime dependencies.
- Observed: `tools/sarpy/reference_image_from_crsd.py` marks true SarPy focused reference as unavailable and uses an independent local focused surrogate.
- Inferred: External tools currently validate or compare artifacts and do not dictate GraphX core runtime contracts.
- Unknown: Whether future work intends to replace the surrogate CRSD reference path with a real external focused-image implementation.

## Current-State Summary

- Observed: The repository has a mature SAR accel-token stripmap example path with diagnostics and guardrails.
- Observed: The repository also has a broad GOTCHA/CRSD/focused-image lane with binary CRSD fixtures, local-only real-data workflows, artifact persistence, comparison tooling, and truth-in-labeling guardrails.
- Observed: Metal infrastructure is real at the libgpu native capability layer, while some domain-level Metal claims are explicitly downgraded as experimental incomplete or unsupported.
- Inferred: The current architecture is closest to a hybrid state: canonical accel-token transport for stripmap/SAR GPU stages, plus typed CRSD phase-history/focused-image stages layered beside it.
- Unknown: Whether the next architecture decision should preserve that hybrid model or force CRSD phase-history/focused-image edges back into `SarAccelControlToken`.
