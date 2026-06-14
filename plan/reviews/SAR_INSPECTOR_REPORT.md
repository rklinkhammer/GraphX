# SAR Inspector Report

Inspector role source: plan/agents/GRAPHX_SAR_AGENT_ROLES.md

Scope: current repository inspection only. No redesign and no implementation.

## 1. Current Type Model

- Observed: The canonical SAR message/types header is examples/SAR/include/sar/SarMessages.hpp.
- Observed: SAR core enums are present: SarBackendKind, SarFrameMarker, SarTransferDirection.
- Observed: SarSidecar exists and carries SAR identity and runtime diagnostics fields, including sequence/batch/aperture/range identity, stream/tile identity, backend metadata, transfer queue IDs, transfer/kernel counters, merge counters, watermark/completion, and stage timings.
- Observed: AccelControlToken<SidecarT> exists and includes token_id, sidecar, BufferLease, DeviceBufferView, HostPinnedBufferView, TransferTicket, KernelTicket, plus presence flags.
- Observed: SarAccelControlToken is a concrete alias of AccelControlToken<SarSidecar>.
- Observed: SarDiagnosticsSnapshot is a sink/report structure that mirrors sidecar-derived diagnostics and graph queue metrics.
- Observed: Contract comments in SarMessages.hpp explicitly state SAR identity must come from sidecar fields and that host_view.host_ptr / device_view.ready_event are opaque transport metadata.
- Observed: Standard-layout static_assert checks exist for SarSidecar, SarAccelControlToken, and SarDiagnosticsSnapshot.
- Inferred: Current typing is intentionally centered on explicit token+sidecar transport rather than legacy payload-specific message structs.
- Unknown: Whether any downstream non-repo consumer still depends on older SAR message names from prior phases.

## 2. Current Node Model

- Observed: The definitive configured runtime topology in examples/SAR/config/sar_stripmap_definitive.json is:
  SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncAccelNode -> SarBackprojectionTransformAccelNode -> D2HAsyncAccelNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode.
- Observed: Source nodes include SyntheticApertureIqSourceNode and GotchaReplaySourceNode.
- Observed: Pre-GPU SAR pipeline nodes include RangeWindowNode, RangeCompressionNode, AzimuthTileSplitNode, and SarPulseFanoutNode.
- Observed: SAR GPU-edge/token nodes include H2DAsyncAccelNode, SarBackprojectionTransformAccelNode, and D2HAsyncAccelNode.
- Observed: Post-GPU nodes include ImageTileMergeNode, SarDiagnosticsSinkNode, SarVisualizationSinkNode, and SarMaterializedImageSinkNode.
- Observed: Plugin build wiring in examples/SAR/plugins/CMakeLists.txt compiles these SAR nodes as dynamic plugins in examples/SAR/plugins.
- Observed: SarBackprojectionTransformAccelNode has dual runtime behavior inside one node: simulated token/device-view path and native Metal path via libgpu DeviceKernelNodeMetal when capabilities are bound.
- Inferred: The current node model is a SAR-owned token path that delegates native kernel execution to libgpu rather than replacing the node with a generic kernel node in definitive config.
- Unknown: Whether all non-definitive scenario configs in examples/SAR/config are still active in maintained workflows.

## 3. Current Token/Data Flow

- Observed: SyntheticApertureIqSourceNode and GotchaReplaySourceNode both emit SarAccelControlToken values with sidecar identity populated and host_view present.
- Observed: AzimuthTileSplitNode produces new token_id values and updates sidecar tile identity; it sets host_view.host_ptr using runtime::OpaqueHostPointer().
- Observed: H2DAsyncAccelNode requires has_host_view, emits device_view, lease, transfer_ticket, and writes sidecar backend/h2d_queue/timing fields.
- Observed: SarBackprojectionTransformAccelNode requires has_device_view, emits updated device_view and kernel_ticket, and writes sidecar backend/kernel_queue/timing fields.
- Observed: D2HAsyncAccelNode requires valid device_view, emits host_view, lease, transfer_ticket, and writes sidecar backend/d2h_queue/timing fields.
- Observed: ImageTileMergeNode aggregates tile/stream/marker state from sidecar and accumulates transfer counters and stage timing totals into sidecar.
- Observed: SarDiagnosticsSinkNode consumes merged tokens and publishes diagnostics from sidecar plus graph queue metrics.
- Observed: runtime::OpaqueHostPointer(), runtime::OpaqueReadyEventNotSignaled(), and runtime::NextOpaqueEventId() are centralized in examples/SAR/include/sar/SarRuntimeHelpers.hpp.
- Observed: runtime::SyntheticDevicePointer() constructs synthetic pointer values from byte count and sequence, used by H2D/backprojection simulated paths.
- Inferred: SAR identity propagation is sidecar-first through the entire configured path.
- Unknown: Whether any optional/experimental SAR path bypasses sidecar propagation under plugin combinations not used by definitive config.

## 4. Resolver Substitution Flow

- Observed: Graph config parser accepts resolver fields execution_backend, backend_fallback_policy, resolver_diagnostics, edge_contract, resolver_mappings in libgraph/src/graph/GraphConfigParser.cpp.
- Observed: edge_contract currently validates to empty or accel-token; invalid values are rejected.
- Observed: With edge_contract=accel-token, parser rejects legacy SAR payload contracts in resolver mappings and edge payload_contract fields.
- Observed: GraphBuilder constructs default NodeResolutionRegistry then calls AddMappings(parsed_config.resolver_mappings), so JSON mappings can add/override contracts before ResolvingNodeProvider creation.
- Observed: ResolvingNodeProvider backend preference order is metal -> sycl -> stub -> cuda for auto; strict mode does not fallback unless allow_fallback is set.
- Observed: Definitive SAR JSON defines resolver_mappings directly for H2DAsyncAccelNode, SarBackprojectionTransformAccelNode, and D2HAsyncAccelNode with SarAccelControlToken input/output types.
- Observed: Default NodeResolutionRegistry contracts in libgraph/src/graph/NodeResolutionRegistry.cpp are generic GPU intent names (for example H2DAsyncNode, D2HAsyncNode, DeviceKernelNode) with DeviceBufferView/HostPinnedBufferView contracts.
- Inferred: SAR definitive flow relies on SAR intent names and mappings, while generic default contracts remain available for non-SAR graphs.
- Unknown: Runtime prevalence of fallback decisions outside tested presets.

## 5. Violations Of Accel-Token Architecture

- Observed: No production SAR node inspected derives SAR identity from host_ptr or ready_event.
- Observed: Multiple SAR node comments explicitly state host_ptr/ready_event are opaque transport metadata and sidecar carries identity.
- Observed: Guardrail tests exist for transport opacity in examples/SAR/test/test_sar_transport_opaque_contract.cpp.
- Observed: JSON/runtime tests assert sequence identity is not encoded as transfer completion events or host pointer values in examples/SAR/test/test_sar_json_runtime.cpp.
- Observed: Token guardrail and accel-node tests exist in examples/SAR/test/test_sar_accel_token_guardrails.cpp and examples/SAR/test/test_sar_accel_nodes.cpp.
- Inferred: Current inspected path is aligned with accel-token architecture intent.
- Unknown: Whether uninspected plugins outside examples/SAR might still contain legacy identity assumptions.

## 6. Obsolete Abstractions

- Observed: Generic default resolver contracts for H2DAsyncNode/D2HAsyncNode/DeviceKernelNode still exist in libgraph, separate from SAR accel-token node intents.
- Observed: SAR config set includes multiple scenario and specialized configs beyond definitive, including metal/fanout/materialized/external-manual variants.
- Observed: sar_gotcha_external_manual.json is present as a non-default external/manual topology scaffold.
- Observed: Current SarMessages.hpp no longer defines legacy SAR payload message structs; the active surface is token/sidecar-centric.
- Inferred: The main residual abstraction overlap is architectural surface area (generic resolver contracts plus SAR-specific token contracts), not duplicate legacy message structs in current SAR headers.
- Unknown: Which non-definitive configs are still required versus retained for historical/testing support.

## 7. Complexity Hotspots

- Observed: SarBackprojectionTransformAccelNode combines SAR token semantics, simulated kernel metadata, native Metal capability binding, and inline Metal source generation.
- Observed: ImageTileMergeNode owns completion policy, ordering/duplication accounting, transfer counter accumulation, and stage timing rollups.
- Observed: Resolver behavior spans config JSON, parser validation, registry overlays, provider selection, and plugin availability.
- Observed: SAR benchmarking logic in examples/SAR/src/sar_benchmark.cpp is extensive and includes profile generation, execution traces, and reporting logic.
- Observed: External comparison tooling spans Python tools and C++ harness tests (SarPy tools, local runner, comparator contracts, baseline policy/registry validation).
- Inferred: Current complexity is concentrated in boundary layers (resolver + benchmark + comparison harnesses) rather than in token type definitions.
- Unknown: Operational complexity impact in CI/runtime environments with differing plugin availability.

## 8. Blockers For AccelControlToken<SarSidecar>

- Observed: The type exists and is used end-to-end in definitive SAR config and runtime nodes.
- Observed: Parser guardrails enforce accel-token compatibility and reject legacy payload contracts under accel-token mode.
- Observed: Transport-opaque contract tests are present and exercised in SAR unit suite files.
- Observed: Native-backend behavior depends on capability binding for SarBackprojectionTransformAccelNode; code supports fallback/simulated path.
- Inferred: There is no obvious structural blocker to using AccelControlToken<SarSidecar> as the canonical SAR contract in current inspected code.
- Unknown: Environment-specific blockers (for example missing plugin/capability combinations) outside repository inspection scope.

## 9. Existing External Comparison/Baseline Hooks

- Observed: Policy and registry artifacts exist at plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md and plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json.
- Observed: Policy/registry enforcement tests exist in examples/SAR/test/test_external_baseline_policy_registry.cpp.
- Observed: Local-only scenario runner scaffolding exists in examples/SAR/tools/sar_local_runner.py.
- Observed: gotcha-back adapter and invocation scaffolding exist in examples/SAR/tools/gotcha_back_adapter.py and related tests.
- Observed: SarPy probe/compare toolchain is present with tests in examples/SAR/test/test_sarpy_reference_compare_tools.cpp and examples/SAR/test/test_sarpy_metadata_harness.cpp.
- Observed: Additional baseline/fixture comparison tests exist (for example test_sar_baseline_compare.cpp, test_graphx_image_comparison_lane.cpp, test_local_gotcha_validation_lane.cpp).
- Inferred: External baselines are integrated as comparison harnesses and policy-controlled artifacts, not as core GraphX runtime contracts.
- Unknown: Real-world parity status versus external baselines, since this inspection did not run benchmark or parity lanes.

## Focus Checks Requested In Role

- Observed: host_ptr usage in SAR nodes is present as opaque sentinel assignment, not identity derivation.
- Observed: ready_event usage in SAR nodes is present as opaque transport metadata/event ticket field.
- Observed: accel-token tests and transport-opaque tests are present.
- Observed: examples/SAR/src/main.cpp has executable coverage via examples/SAR/test/test_sar_main_executable.cpp.
- Observed: examples/SAR/src/main.cpp prints runtime status and two diagnostics counters (queue_backpressure_events, peak_queue_depth).
- Observed: Detailed performance reporting is implemented in examples/SAR/src/sar_benchmark.cpp, not in main.cpp.
- Observed: Duplicate elapsedUs helper functions were not found in inspected SAR code; one helper exists in examples/SAR/include/sar/SarRuntimeHelpers.hpp.

Stop point: current-state report only.
