# SAR Simplifier Report

Source input: `plan/reviews/SAR_INSPECTOR_REPORT.md`

Scope: target architecture only. No implementation or PR plan.

## 1. Target Type Model

- `SarSidecar` remains the only SAR identity and diagnostics carrier.
- `AccelControlToken<SarSidecar>` / `SarAccelControlToken` remains the only SAR GPU-edge payload type.
- Generic GPU transport types remain in `libgpu`: `DeviceBufferView`, `HostPinnedBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`, `TensorLayout`.
- `host_ptr` and `ready_event` remain transport-only fields and must never encode SAR identity.
- SAR source/DSP/merge types may use SAR concepts, but GPU transfer/kernel mechanics must use the generic accel transport model.
- GOTCHA replay fixture types stay example-local unless direct dataset ingestion becomes an approved feature.
- No legacy SAR payload message types, aliases, or compatibility type names remain in the target model.

## 2. Target Node Model

Canonical flow:

```text
SAR source / DSP nodes
  -> SarAccelControlToken
  -> generic sidecar-preserving GPU adapter over libgpu transfer/kernel nodes
  -> SarAccelControlToken
  -> SAR merge / diagnostics / materialization nodes
```

Target SAR nodes that stay example-local:

- `SyntheticApertureIqSourceNode`
- `GotchaReplaySourceNode`, fixture-only for now
- `RangeWindowNode`
- `RangeCompressionNode`
- `AzimuthTileSplitNode`
- `SarPulseFanoutNode`, only if the canonical config needs fanout
- `ImageTileMergeNode`
- `SarDiagnosticsSinkNode`
- `SarMaterializedImageSinkNode`
- `SarVisualizationSinkNode`, if still useful for example output

Target GPU boundary:

- SAR no longer owns separate H2D/D2H transfer implementations.
- SAR uses a small sidecar-preserving wrapper/adaptor pattern around generic libgpu nodes.
- Backprojection should be represented as SAR-side configuration plus generic `DeviceKernelNode`/Metal kernel execution, not as a mixed SAR/Metal/inline-kernel mega-node.

## 3. Deletion List

- Delete compatibility alias headers:
  - `examples/SAR/include/sar/H2DAsyncNode.hpp`
  - `examples/SAR/include/sar/D2HAsyncNode.hpp`
  - `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`
- Delete SAR-local duplicate transfer implementations once replaced:
  - `H2DAsyncAccelNode`
  - `D2HAsyncAccelNode`
- Delete config-facing compatibility plugin names for SAR H2D/D2H/backprojection aliases.
- Delete deprecated definitive split configs:
  - `sar_stripmap_definitive_metal.json`
  - `sar_stripmap_definitive_nonmetal.json`
- Delete PR-era topology configs once their coverage is folded into focused tests:
  - `sar_stripmap_pr1.json`
  - `sar_stripmap_pr2_fanout.json`
  - `sar_stripmap_pr3_*`
  - `sar_stripmap_pr6_matched_filter.json`
  - `sar_stripmap_pr7_materialized_image.json`
- Delete tests whose only purpose is preserving old aliases, deprecated configs, or legacy resolver behavior.
- Delete references to absent `plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md` and `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json` from normal SAR tests unless those files are restored as real source artifacts.
- Delete any remaining legacy SAR payload contract allowances under `edge_contract: "accel-token"`.

## 4. Replacement List

- Replace `H2DAsyncNode` config usage with an explicit sidecar-preserving SAR GPU ingress node name, or with a generic token adapter whose only job is:
  - unwrap host view from `SarAccelControlToken`
  - call generic GPU H2D behavior
  - reattach unchanged `SarSidecar`
- Replace `D2HAsyncNode` config usage with the symmetric sidecar-preserving GPU egress adapter.
- Replace `SarBackprojectionTransformNode` alias with one explicit canonical name.
- Replace mixed simulated/native logic inside `SarBackprojectionTransformAccelNode` with a simpler split:
  - SAR backprojection descriptor/config builder
  - generic device kernel execution
  - sidecar preservation
- Replace many PR-era JSON configs with one canonical config plus small targeted fixtures/tests.
- Replace external baseline policy tests that depend on missing `plan/reviews` files with either checked-in policy artifacts or tests scoped to existing scripts/fixtures.
- Replace normalized GOTCHA replay naming with clearer fixture wording until direct MATLAB/GOTCHA ingestion exists.

## 5. Architecture Invariants

- Exactly one SAR GPU-edge contract exists: `AccelControlToken<SarSidecar>`.
- SAR identity is sidecar-only.
- `host_ptr`, `device_ptr`, `ready_event`, transfer IDs, kernel IDs, and queue IDs are never identity.
- Generic GPU nodes stay SAR-unaware.
- SAR nodes may interpret `SarSidecar`; libgpu nodes may not.
- Resolver mappings must not make generic node names mean different payload contracts in different contexts.
- No compatibility aliases survive merely because old configs or tests reference them.
- `examples/SAR/main.cpp` remains tested.
- Performance reporting must be explicit: either `main.cpp` reports required metrics or benchmark-only metrics are not claimed as main runtime reporting.
- External SAR tools remain comparison/harness artifacts only; they do not shape GraphX core contracts.
- GOTCHA replay fixtures are not treated as direct GOTCHA dataset ingestion.

## 6. Open Questions That Block Planning

- Should the canonical sidecar-preserving GPU boundary be implemented as generic templated token adapters in `libgpu`, or as SAR-local adapters that call libgpu nodes?
- Is `SarPulseFanoutNode` required in the single canonical topology, or should the definitive config remain linear?
- Should `examples/SAR/main.cpp` itself report stage/performance metrics, or is `sar_benchmark` the accepted performance surface?
- Are the missing external baseline policy/registry files intentionally removed, or should those tests be deleted?
- Which current SAR configs are still user-facing, if any, besides `sar_stripmap_definitive.json`?
- Should direct GOTCHA MATLAB ingestion be considered in this cleanup sequence, or kept out until after architecture cleanup?
