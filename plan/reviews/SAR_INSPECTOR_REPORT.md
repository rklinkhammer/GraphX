# SAR Inspector Report

Inspector role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: current repository inspection only. No redesign. No implementation.

Repository state at inspection time:

- Observed: The working tree contains one unrelated untracked file: `reference_magnitude.png`.
- Observed: This report updates `plan/reviews/SAR_INSPECTOR_REPORT.md` only.
- Inferred: The findings below describe the current checked-out repository files and do not rely on external dataset state.

## 1. Current Type Model

- Observed: The canonical SAR accel-token model is defined in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` carries SAR identity and execution metadata, including stream/frame sequencing, tile identity, backend kind, transfer queue IDs, merge state, timing metrics, checksums, and diagnostics.
- Observed: `AccelControlToken<SidecarT>` carries `sidecar`, `BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, `KernelTicket`, and explicit presence flags.
- Observed: `SarAccelControlToken` is an alias for `AccelControlToken<SarSidecar>`.
- Observed: `SarMessages.hpp` explicitly states that SAR identity must derive from sidecar fields and that `device_view.ready_event` and `host_view.host_ptr` are opaque transport metadata only.
- Observed: `SarPhaseHistoryModel.hpp` defines CRSD/focused-image phase-history types: `SarPhaseHistoryVector`, `SarPhaseHistorySegment`, `SarAperturePartition`, `SarPhaseHistoryApertureFrame`, and `SarPhaseHistoryControlMessage`.
- Observed: `SarPhaseHistoryControlMessage` contains a `SarAccelControlToken control` plus a `SarPhaseHistoryApertureFrame frame`.
- Observed: `CrsdFocusedImageTransformNode` and `CrsdFocusedImageTransformMetalNode` output `FocusedImageResult`, not `SarAccelControlToken`.
- Observed: `examples/SAR/include/sar/io/NormalizedSarProduct.hpp` defines the normalized SAR product model used by GOTCHA/CRSD conversion paths.
- Observed: `examples/SAR/include/sar/io` also contains reader/writer/validator/chunker/mapping types for GOTCHA, CRSD, ordering, HDF5 phdata, and product validation.
- Observed: Generic GPU transport/kernel types live in `libgpu/include/gpu/accel/types`, including buffer views, leases, transfer tickets, and kernel tickets.
- Observed: Standard-layout assertions exist for `SarSidecar`, `SarAccelControlToken`, and `SarDiagnosticsSnapshot`.
- Inferred: The repository currently has three active SAR-facing type layers: accel-token transport, normalized SAR products for conversion, and CRSD phase-history/focused-image messages for image formation.
- Unknown: Whether all downstream users are expected to treat the CRSD phase-history/result types as permanent public contracts.

## 2. Current Node Model

- Observed: The definitive stripmap topology in `examples/SAR/config/sar_stripmap_definitive.json` is `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncAccelNode -> SarBackprojectionTransformAccelNode -> D2HAsyncAccelNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: Active SAR plugins include synthetic source, GOTCHA replay source, range window, range compression, azimuth split, SAR H2D, SAR backprojection, SAR D2H, tile merge, diagnostics sink, fanout, materialized image sink, visualization sink, CRSD input source, CRSD aperture assembly adapter, CRSD focused-image CPU transform, CRSD focused-image Metal transform, and CRSD focused-image sink.
- Observed: `OrderedCrsdSetInputSourceNode` reads CRSD path/directory/manifest inputs and emits `SarAccelControlToken` records for ordered CRSD segments.
- Observed: `CrsdApertureAssemblyAdapterNode` consumes `SarAccelControlToken` records and emits `SarPhaseHistoryControlMessage` after assembling an aperture frame.
- Observed: `CrsdFocusedImageTransformNode` is the CPU focused-image transform.
- Observed: `CrsdFocusedImageTransformMetalNode` binds Metal capabilities and launches a Metal kernel path, but the repository truth-in-labeling guardrails classify it as fallback plus experimental incomplete.
- Observed: `CrsdFocusedImageSinkNode` consumes `FocusedImageResult` and writes focused-image artifacts.
- Observed: libgpu provides generic Metal node plugins for host ingress, H2D, D2H, peer copy, device shard, lease release, queue sync, host egress, device kernel, device transform, device reduce, and collective reduce.
- Observed: `docs/sar/metal_node_truth_in_labeling.md` classifies `CollectiveReduceNodeMetal` as unsupported and `CrsdFocusedImageTransformMetalNode` as a domain algorithm that is experimental incomplete.
- Observed: libdsp provides CPU DSP plugins for sine signal, FFT, and spectrum sink; these are not part of the SAR accel-token model.
- Inferred: SAR has both domain-specific SAR nodes and generic GPU nodes in the repository, but maintained SAR configs primarily use SAR-specific token nodes rather than graph-level generic GPU primitives.
- Unknown: Which non-definitive SAR configs under `examples/SAR/config` are intended as maintained product presets versus fixtures or local experiments.

## 3. Current Token/Data Flow

- Observed: The definitive stripmap flow uses `SarAccelControlToken` for every edge after source emission.
- Observed: `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode` populate sidecar identity and host-view metadata.
- Observed: `RangeWindowNode` and `RangeCompressionNode` preserve token structure while updating SAR DSP metadata and stage timing.
- Observed: `AzimuthTileSplitNode` updates tile identity and emits tokenized tile records.
- Observed: `H2DAsyncAccelNode` requires a host view, constructs a synthetic device view, writes lease/transfer-ticket fields, and updates H2D sidecar metadata.
- Observed: `SarBackprojectionTransformAccelNode` requires a device view, updates device view/kernel ticket metadata, and can delegate native kernel launch to libgpu `DeviceKernelNodeMetal` when native capabilities are bound.
- Observed: `D2HAsyncAccelNode` constructs a host view and writes D2H lease/transfer-ticket metadata.
- Observed: `ImageTileMergeNode` aggregates tile completion, merge counters, transfer byte counts, and stage timing into the token sidecar.
- Observed: `SarDiagnosticsSinkNode` consumes token diagnostics and exposes queue/backpressure metrics.
- Observed: CRSD focused-image flow can be configured as `OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> CrsdFocusedImageTransformNode -> CrsdFocusedImageSinkNode`.
- Observed: The CRSD focused-image Metal config uses `OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> CrsdFocusedImageTransformMetalNode`; it does not expose separate graph-level H2D/kernel/D2H nodes in that pipeline.
- Observed: `host_ptr` and `ready_event` uses in SAR source/transfer/benchmark code are accompanied by comments or tests that treat them as opaque transport metadata, not SAR identity.
- Inferred: The mature stripmap path is accel-token native, while the CRSD image-formation path transitions from token transport into typed phase-history and focused-image payloads.
- Unknown: Whether the phase-history and focused-image payload transition is an accepted architectural exception or a temporary bridge.

## 4. Resolver Substitution Flow

- Observed: `libgraph/src/graph/GraphBuilder.cpp` adds parsed JSON resolver mappings into `NodeResolutionRegistry`.
- Observed: `ResolvingNodeProvider` resolves node intents using configured backend preference and fallback policy.
- Observed: Auto backend preference order is `metal`, `sycl`, `stub`, `cuda`.
- Observed: Strict backend policy uses only the requested backend; `allow_fallback` permits remaining backends.
- Observed: The default `NodeResolutionRegistry` registers generic GPU intents such as `H2DAsyncNode`, `D2HAsyncNode`, `DeviceTransformNode`, `DeviceKernelNode`, `DeviceReduceNode`, and `QueueSyncNode`.
- Observed: The definitive SAR config adds local resolver mappings for `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode`.
- Observed: Those SAR mappings use `SarAccelControlToken` input/output contracts.
- Observed: Those SAR mappings map metal/stub/cuda/sycl variants back to the same SAR concrete node names, not to generic libgpu concrete nodes.
- Observed: Resolver diagnostics record intent type, concrete type, selected backend, fallback reason, token types, and fallback use.
- Inferred: SAR resolver diagnostics can show a selected backend even when the concrete node type remains the same SAR-specific class.
- Unknown: Whether any production topology outside tests relies on default generic GPU resolver mappings for SAR workloads.

## 5. Violations Of Accel-Token Architecture

- Observed: The stripmap SAR GPU path uses explicit `SarAccelControlToken` edges and no legacy SAR payload message types.
- Observed: `test_sar_transport_opaque_contract.cpp` freezes `host_ptr` and `ready_event` as opaque transport fields.
- Observed: `test_sar_accel_token_guardrails.cpp` rejects legacy SAR payload contracts when `edge_contract` is `accel-token`.
- Observed: Legacy names such as `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` appear in parser/guardrail tests as rejected inputs, not active payload types.
- Observed: `CrsdFocusedImageTransformNode` and `CrsdFocusedImageTransformMetalNode` do not preserve an all-token edge contract after aperture assembly; they consume `SarPhaseHistoryControlMessage` and output `FocusedImageResult`.
- Observed: Several CRSD configs declare top-level `"edge_contract": "accel-token"` even though the full configured pipeline includes non-token edges after CRSD aperture assembly.
- Observed: SAR H2D/D2H transfer nodes are SAR-specific token nodes; they do not substitute directly to generic libgpu `H2DAsyncNodeMetal` or `D2HAsyncNodeMetal`.
- Observed: `SarBackprojectionTransformAccelNode` wraps/uses a generic Metal kernel primitive internally for native execution instead of exposing a graph-level generic `DeviceKernelNodeMetal` stage in the definitive topology.
- Observed: `examples/SAR/main.cpp` reports graph completion and diagnostics queue/backpressure metrics, but detailed performance trace reporting lives in `examples/SAR/src/sar_benchmark.cpp`.
- Inferred: If the role-file invariant is interpreted literally as exactly one canonical SAR GPU path through generic GPU transfer/kernel nodes, the current repository still violates it through SAR-specific H2D/D2H nodes, a SAR-specific backprojection wrapper, and CRSD typed-payload stages.
- Inferred: If the invariant is interpreted as "SAR GPU transport must preserve `AccelControlToken<SarSidecar>` identity," the stripmap path largely satisfies it and the remaining violation is the CRSD focused-image branch.
- Unknown: Whether the principal architecture currently permits CRSD phase-history/result payloads as a bounded non-GPU product lane.

## 6. Obsolete Abstractions

- Observed: Old alias headers `examples/SAR/include/sar/H2DAsyncNode.hpp`, `examples/SAR/include/sar/D2HAsyncNode.hpp`, and `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp` are absent.
- Observed: Legacy SAR payload names are retained only in negative tests and parser guardrails.
- Observed: `CollectiveReduceNodeMetal` is present as a plugin but documented and tested as unsupported.
- Observed: `CrsdFocusedImageTransformMetalNode` is active but explicitly labeled experimental incomplete.
- Observed: `SarBackendKind::SimulatedDevice` coexists with graph-level resolver backend strings and node-level integer backend/backend_id fields.
- Observed: Tracked Python cache artifacts exist under `examples/SAR/tools/__pycache__` and `tools/sarpy/__pycache__`.
- Inferred: The old multi-message SAR payload model has been mostly deleted from active code.
- Inferred: SAR-specific H2D/D2H nodes may be obsolete if the target architecture is graph-level generic GPU transfer nodes wrapped by accel-token preserving adapters.
- Unknown: Whether the unsupported collective node and experimental CRSD Metal node are intentionally retained as guardrailed future extension points.

## 7. Complexity Hotspots

- Observed: SAR has both a stripmap accel-token pipeline and a CRSD phase-history/focused-image pipeline.
- Observed: CRSD support spans C++ reader/writer/model code, Python SarPy tooling, shell conversion scripts, binary fixtures, JSON sidecars/reports, docs, local-only validation, and comparison harnesses.
- Observed: `CrsdReader.cpp` supports tiny JSON CRSD helper fixtures and binary CRSD parsing in one reader.
- Observed: `CrsdIO.hpp` implements a C++ CRSD writer facade that shells out to `python3 tools/sarpy/write_crsd_from_graphx_product.py` for standards-targeted CRSD output.
- Observed: `CrsdFocusedImageTransformMetalNode` combines CPU seed-image generation, capability binding, transfer orchestration, inline Metal source, kernel launch, readback, fallback behavior, and result construction.
- Observed: SAR configs contain overlapping backend declarations: top-level `execution_backend`, top-level resolver mappings, node-level `backend`, node-level `backend_id`, and node-specific `execution_backend` fields.
- Observed: Maintained resolver mappings repeat the same concrete SAR node for multiple backend variants.
- Observed: `examples/SAR/main.cpp` and `sar_benchmark.cpp` have separate reporting surfaces; the benchmark has richer trace/performance schema coverage than the example executable.
- Observed: `SarRuntimeHelpers.hpp` contains one `ElapsedUs` helper; no duplicate `elapsedUs` function was found in `examples/SAR`, `libgraph`, `libgpu`, or `libdsp`.
- Observed: Doxygen comments in some recently documented files are generic and do not always describe actual behavior in depth.
- Inferred: The highest-risk complexity is truth-in-labeling: a user can select "Metal" or "CRSD" paths whose real behavior depends on guardrails, external Python helpers, fallback status, or experimental-incomplete algorithms.
- Unknown: Whether current local real-data workflows are stable across all supported developer machines.

## 8. Blockers For `AccelControlToken<SarSidecar>`

- Observed: `SarAccelControlToken` exists and is used throughout the definitive stripmap SAR pipeline.
- Observed: Generic libgpu nodes consume and produce generic buffer views, not `AccelControlToken<SarSidecar>`.
- Observed: SAR H2D/D2H nodes preserve sidecar identity themselves and produce synthetic transport metadata.
- Observed: The default resolver registry does not provide generic sidecar-preserving SAR token adapters around libgpu H2D/D2H/kernel nodes.
- Observed: CRSD focused-image adapter-to-transform edges use `SarPhaseHistoryControlMessage`.
- Observed: CRSD focused-image sink edges use `FocusedImageResult`.
- Observed: Focused-image artifacts carry CRSD lineage, hashes, and image metadata outside the plain sidecar.
- Inferred: The stripmap lane is not blocked on `AccelControlToken<SarSidecar>`, but a single all-SAR-edge token architecture is blocked by the CRSD phase-history/result model.
- Inferred: A single generic-GPU-node architecture is blocked by the absence of graph-level generic sidecar-preserving wrappers/adapters for the current SAR token.
- Inferred: If CRSD focused-image work remains a product/image-formation lane, the blocker may be documentation and guardrails rather than type mechanics.
- Unknown: Whether `SarPhaseHistoryControlMessage` should be embedded into an accel token, carried by sidecar-referenced buffers, or accepted as a separate higher-level message type.

## 9. Existing External Comparison/Baseline Hooks

- Observed: `plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md` states that external SAR packages are comparators only and must not define GraphX runtime architecture.
- Observed: `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json` records SarPy as primary comparator, ISCE3 and gotcha-back as secondary comparators, and OpenSAR/OpenSARLab as survey-only candidates.
- Observed: The registry marks external baseline use as comparator-only and local-only, with no GraphX core contract changes for external packages.
- Observed: `tools/sarpy` contains scripts for CRSD validation, CRSD reference-image surrogate generation, GOTCHA reference generation, image comparison, classic MAT conversion to v7.3/HDF5-compatible form, and SarPy-based CRSD writing.
- Observed: `examples/SAR/tools` contains local runner, image comparator, SarPy metadata harness, gotcha-back adapter, focused-image artifact schema, and local-runner documentation.
- Observed: Tests cover external baseline policy/registry, SarPy harness behavior, image comparison, local runner contracts, and local-only GOTCHA validation gates.
- Observed: External tools live outside `libgraph` and `libgpu`.
- Inferred: External packages are currently used as validation/comparison and writer/utility harnesses, not as core GraphX runtime APIs.
- Unknown: Whether the SarPy writer bridge is intended to remain as the standards-targeted CRSD writer path or be replaced by a native C++ writer.

## Current-State Summary

- Observed: The repository has a mature SAR stripmap example path built around `AccelControlToken<SarSidecar>`, explicit sidecar identity, resolver guardrails, and transport opacity tests.
- Observed: The repository also has a broad GOTCHA/CRSD/focused-image lane with normalized product models, binary CRSD reading, SarPy-assisted CRSD writing, local-only real-data workflows, image comparison tooling, and truth-in-labeling guardrails.
- Observed: Metal infrastructure is real at the generic libgpu capability layer, while `CollectiveReduceNodeMetal` and the CRSD focused-image Metal domain node are explicitly downgraded by guardrails.
- Inferred: The current architecture is a hybrid: canonical accel-token transport for the stripmap/SAR GPU stages, plus typed CRSD phase-history/focused-image stages layered beside it.
- Inferred: The primary architectural ambiguity is whether the CRSD focused-image lane is an accepted product-level exception or should be forced into the one canonical accel-token/generic-GPU path required by the role file.
- Unknown: Whether future cleanup should prioritize deleting the hybrid exception, formalizing it, or moving it behind a narrower adapter boundary.
