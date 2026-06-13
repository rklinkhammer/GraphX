# SAR Inspector Report

Inspector role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: current repository inspection only. This report does not redesign or implement.

## 1. Current Type Model

- Observed: SAR declares `SarBackendKind`, `SarFrameMarker`, `SarTransferDirection`, `SarStageTimingMetrics`, `SarSidecar`, `AccelControlToken<SidecarT>`, `SarAccelControlToken`, `SarDiagnosticsSnapshot`, and `SarIqSample` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` carries SAR identity and diagnostics fields: sequence, batch, aperture, pulse range, stream, tile, backend, marker, synthetic flag, payload byte count, queue IDs, merge counters, transfer/kernel counters, watermark/merge completion, and stage timings.
- Observed: `AccelControlToken<SidecarT>` carries `token_id`, `sidecar`, `BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, `KernelTicket`, and presence flags.
- Observed: The comment contract in `SarMessages.hpp` states that SAR identity must come only from sidecar fields and that `device_view.ready_event` and `host_view.host_ptr` are opaque transport metadata, not identity fields.
- Observed: `libgpu/include/gpu/accel/types/AccelTypes.hpp` defines the generic GPU transport types used inside the SAR token: `DeviceBufferView`, `HostPinnedBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`, `TensorLayout`, and related backend/data-type enums.
- Observed: GOTCHA support currently models normalized JSON replay records, not direct MATLAB input. `GotchaNormalizedPulseRecord` contains frame/pass/pulse/range/aperture fields, platform position/velocity, scene center, RF metadata, calibration fields, polarization, coordinate frame, sample layout, endianness, and IQ samples.
- Inferred: The canonical SAR runtime payload in the current example is already `SarAccelControlToken`, but several type names retain older config-facing names through aliases.
- Unknown: Whether any downstream external consumers rely on the alias names; comments explicitly say the aliases are retained until maintained presets/downstream users migrate.

## 2. Current Node Model

- Observed: SAR source nodes include `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode`.
- Observed: SAR DSP/source-side nodes include `RangeWindowNode`, `RangeCompressionNode`, `AzimuthTileSplitNode`, and `SarPulseFanoutNode`.
- Observed: SAR GPU-edge nodes include `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode`.
- Observed: SAR sink/merge/diagnostic nodes include `ImageTileMergeNode`, `SarDiagnosticsSinkNode`, `SarVisualizationSinkNode`, and `SarMaterializedImageSinkNode`.
- Observed: Config-facing aliases remain:
  - `H2DAsyncNode = H2DAsyncAccelNode`
  - `D2HAsyncNode = D2HAsyncAccelNode`
  - `SarBackprojectionTransformNode = SarBackprojectionTransformAccelNode`
- Observed: libgpu also has generic backend nodes such as `H2DAsyncNodeMetal`, `D2HAsyncNodeMetal`, `DeviceKernelNodeMetal`, `DeviceTransformNodeMetal`, `DeviceReduceNodeMetal`, `QueueSyncNodeMetal`, and SYCL/CUDA analogs.
- Observed: Generic Metal H2D/D2H nodes use `HostPinnedBufferView -> DeviceBufferView` and `DeviceBufferView -> HostPinnedBufferView`, while SAR H2D/D2H accel nodes use `SarAccelControlToken -> SarAccelControlToken`.
- Inferred: There are two conceptual GPU transfer models in the repo: generic libgpu transfer nodes and SAR-local transfer wrappers that preserve SAR sidecars.

## 3. Current Token/Data Flow

- Observed: The definitive SAR topology is `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: `examples/SAR/config/sar_stripmap_definitive.json` declares `edge_contract: "accel-token"` and resolver mappings for `H2DAsyncNode`, `SarBackprojectionTransformNode`, and `D2HAsyncNode` with `input_token_type` and `output_token_type` set to `SarAccelControlToken`.
- Observed: `SyntheticApertureIqSourceNode` produces `SarAccelControlToken` values.
- Observed: `GotchaReplaySourceNode` reads normalized JSON fixture records and produces `SarAccelControlToken` values; external fixture paths are blocked unless `allow_external_fixture=true` and `GRAPHX_SAR_ALLOW_EXTERNAL_DATA=1`.
- Observed: `H2DAsyncAccelNode` requires `has_host_view`, creates a synthetic `DeviceBufferView`, sets opaque `ready_event = 0`, creates transfer/lease metadata, and updates `sidecar.backend_id`, `sidecar.backend`, `sidecar.h2d_queue_id`, and H2D timing.
- Observed: `SarBackprojectionTransformAccelNode` has two paths:
  - Native path: when `backend == NativeDevice` and native Metal kernel capabilities are bound, it calls `DeviceKernelNodeMetal` and then re-wraps the returned `DeviceBufferView` in the same SAR token.
  - Simulated path: it creates a synthetic `DeviceBufferView`, a `KernelTicket`, opaque ready event, and updates sidecar backend/kernel timing fields.
- Observed: `D2HAsyncAccelNode` requires a valid device view, creates a synthetic/opaque host view, creates transfer/lease metadata, and updates `sidecar.backend_id`, `sidecar.backend`, `sidecar.d2h_queue_id`, and D2H timing.
- Observed: `ImageTileMergeNode` reads sidecar marker, tile, sequence, stream, and payload byte count; it aggregates sidecar timing and transfer counters and emits completion state.
- Inferred: The runtime path preserves SAR identity through sidecar fields rather than through `host_ptr` or `ready_event`.
- Unknown: Whether all dynamically loaded plugins outside the inspected SAR plugin set preserve that same sidecar discipline.

## 4. Resolver Substitution Flow

- Observed: `NodeResolutionRegistry::CreateDefault()` maps generic intents:
  - `H2DAsyncNode`: `HostPinnedBufferView -> DeviceBufferView`, variants `H2DAsyncNodeMetal`, `H2DAsyncNodeSycl`, `H2DAsyncNode`, `H2DAsyncNode`.
  - `D2HAsyncNode`: `DeviceBufferView -> HostPinnedBufferView`, variants `D2HAsyncNodeMetal`, `D2HAsyncNodeSycl`, `D2HAsyncNode`, `D2HAsyncNode`.
  - `DeviceKernelNode`: `DeviceBufferView -> DeviceBufferView`, metal variant `DeviceKernelNodeMetal`.
- Observed: Config-provided resolver mappings are added after defaults and overwrite contracts by intent type.
- Observed: SAR configs override `H2DAsyncNode` and `D2HAsyncNode` mappings to use `SarAccelControlToken -> SarAccelControlToken` and currently resolve to SAR-local `H2DAsyncNode`/`D2HAsyncNode` aliases.
- Observed: `ResolvingNodeProvider` chooses backends in auto order `metal`, `sycl`, `stub`, `cuda`; strict mode only checks the requested backend, while fallback mode appends remaining backends.
- Observed: `GraphConfigParser` accepts only `edge_contract: "accel-token"` when a non-empty edge contract is present.
- Observed: `GraphConfigParser` rejects resolver mappings and edge payload contracts that use legacy SAR payload names when the graph declares `edge_contract: "accel-token"`.
- Inferred: Resolver metadata can say a node intent is tokenized while still selecting a config-facing compatibility alias whose concrete implementation is SAR-specific.
- Unknown: Whether resolver diagnostics are consumed by CI beyond assertions in SAR JSON/runtime tests.

## 5. Violations Of Accel-Token Architecture

- Observed: No direct current implementation use was found where SAR identity is derived from `host_ptr` or `ready_event`; inspected SAR nodes use sidecar fields for identity and explicitly comment that transport fields are opaque.
- Observed: Test files include guardrails that mutate `host_ptr`/`ready_event` and assert sidecar identity remains stable.
- Observed: `H2DAsyncNode.hpp`, `D2HAsyncNode.hpp`, and `SarBackprojectionTransformNode.hpp` preserve compatibility aliases for config-facing names.
- Observed: The default generic resolver contracts for `H2DAsyncNode`/`D2HAsyncNode` are not SAR-token contracts; SAR configs must override them.
- Inferred: The existence of SAR-local transfer nodes plus generic libgpu transfer nodes is a dual abstraction surface, even though the active SAR config uses tokenized SAR aliases.
- Observed: `examples/SAR/config/sar_stripmap_definitive_metal.json` and `sar_stripmap_definitive_nonmetal.json` are marked deprecated but remain in the repo.
- Unknown: Whether any non-definitive SAR config is still required for supported workflows.

## 6. Obsolete Abstractions

- Observed: Compatibility aliases remain for `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode`.
- Observed: Deprecated definitive metal/nonmetal configs remain with `deprecated: true` and replacement notices pointing to `sar_stripmap_definitive.json`.
- Observed: Multiple PR-era configs remain (`sar_stripmap_pr1.json`, `sar_stripmap_pr2_fanout.json`, `sar_stripmap_pr3_*`, `sar_stripmap_pr6_matched_filter.json`, `sar_stripmap_pr7_materialized_image.json`).
- Observed: `GotchaReplaySourceNode.hpp` contains a TODO stating that fixture-based replay should eventually be replaced with a direct AFRL Gotcha reader after raw layout, calibration metadata, and redistribution constraints are confirmed.
- Inferred: PR-era configs and alias headers likely exist as compatibility/testing artifacts rather than as a single canonical runtime surface.

## 7. Complexity Hotspots

- Observed: `SarBackprojectionTransformAccelNode` mixes SAR token preservation, simulated kernel metadata, native Metal capability binding, Metal kernel descriptor creation, inline Metal source generation, and fallback launch behavior in one node.
- Observed: Resolver behavior is split across JSON config, `GraphConfigParser`, `NodeResolutionRegistry`, `ResolvingNodeProvider`, plugin availability, and `GraphBuilder`.
- Observed: The SAR test target compiles a broad set of architecture, fixture, external-baseline, JSON, runtime, benchmark, and image-comparison tests into one executable.
- Observed: External baseline paths in the SAR test CMake target point to `plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md` and `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json`, but `plan/reviews` is currently empty.
- Observed: `sar_benchmark.cpp` is a large performance and reporting executable with trace JSON, metrics, external/reference language, and runtime execution paths.
- Observed: GOTCHA support currently spans scenarios, fixtures, replay source, local runner scripts, gotcha-back adapter, image comparator, and SarPy metadata harness.
- Inferred: Current complexity is concentrated around transitional architecture evidence and local-only validation tooling.

## 8. Blockers For `AccelControlToken<SarSidecar>`

- Observed: `AccelControlToken<SarSidecar>` already exists and is used by the current SAR nodes.
- Observed: The active definitive SAR config declares `edge_contract: "accel-token"` and SAR token resolver mappings.
- Observed: Parser guardrails reject legacy SAR payload contracts under `edge_contract: "accel-token"`.
- Observed: Config-facing aliases and default generic resolver contracts still leave room for ambiguity between generic H2D/D2H node names and SAR-token H2D/D2H aliases.
- Observed: `plan/reviews` is empty while tests reference external baseline policy/registry files under that directory; this can block the full SAR unit test suite independent of the accel token implementation.
- Observed: Direct GOTCHA MATLAB ingestion does not exist in the current repo; current GOTCHA path is normalized JSON replay.
- Unknown: Whether native Metal capability binding is available in all target CI environments; the code has native and simulated paths.

## 9. Existing External Comparison/Baseline Hooks

- Observed: `examples/SAR/tools/rrp1_local_runner.py` scaffolds a local-only GOTCHA reproduction layout, writes GraphX config artifacts, and prepares a gotcha-back reference invocation script.
- Observed: `examples/SAR/tools/rrp3_gotcha_back_adapter.py` creates pinned gotcha-back invocation specs and can normalize raw gotcha-back output into comparison metadata.
- Observed: `examples/SAR/tools/rrp4_image_comparator.py` exists for image comparison and has C++ tests around comparator behavior.
- Observed: `examples/SAR/tools/rrp7_sarpy_harness.py` is a local-only SarPy metadata harness; its own notes say the comparison scope is product metadata validation only and does not prove GraphX phase-history image-formation correctness.
- Observed: `examples/SAR/scenarios/scenario_001.json` and `examples/SAR/fixtures/scenario_001/*` provide deterministic scenario/fixture artifacts.
- Observed: `examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json` provides a CI-safe tiny GOTCHA fixture path for replay-style tests.
- Observed: External baseline policy/registry tests exist, but their expected policy/registry files are absent from current `plan/reviews`.
- Inferred: External SAR packages are currently integrated as comparison/scaffolding artifacts, not as core GraphX runtime dependencies.

## Current-State Summary

- Observed: The repo has an explicit `AccelControlToken<SarSidecar>` SAR token model and the active SAR topology uses an `accel-token` JSON contract.
- Observed: SAR identity is documented and mostly implemented through sidecar fields, with opaque `host_ptr`/`ready_event` transport semantics.
- Observed: Transitional artifacts remain: compatibility aliases, deprecated configs, PR-era configs, SAR-local H2D/D2H wrappers, and generic libgpu H2D/D2H nodes.
- Observed: `examples/SAR/main.cpp` is tested through `test_sar_main_executable.cpp`, and it reports runtime status plus limited diagnostics counters.
- Observed: Detailed performance reporting exists in `sar_benchmark`, not in `main.cpp`.
- Observed: Direct MATLAB/GOTCHA ingestion and CRSD conversion are not present in the current repo; current GOTCHA support is normalized JSON replay plus external local-only harnesses.
- Observed: `plan/reviews` is currently empty despite test compile definitions pointing at baseline policy and registry artifacts there.

Stop point: current-state inspection only.
