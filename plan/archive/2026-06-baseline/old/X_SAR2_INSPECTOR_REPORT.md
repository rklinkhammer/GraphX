# X SAR2 Inspector Report

Scope: inspected current repository only under `/Users/rklinkhammer/workspace/GraphX`. No redesign, implementation, or tests were performed.

## 1. Current Type Model

Observed:
- Canonical SAR runtime payload is `sar::SarAccelControlToken = AccelControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- `SarSidecar` carries SAR identity and metadata: sequence, batch, aperture, pulse range, stream, tile, backend, marker, payload bytes, queues, merge counts, byte counters, timings, watermark, and merge completion.
- Generic accel transport types live in `libgpu/include/gpu/accel/types/AccelTypes.hpp`: `DeviceBufferView`, `HostPinnedBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`.
- Legacy message names such as `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` appear only as parser/test rejection guardrails, not as active SAR structs.

Inferred:
- The repo has moved to sidecar-first SAR identity, while generic accel views/tickets remain as transport and observability fields.

Unknown:
- Whether `AccelControlToken` is intended to stay SAR-local or eventually move into `libgpu`.

## 2. Current Node Model

Observed:
- Main SAR path in `examples/SAR/config/sar_stripmap_definitive.json`:
  `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- All SAR example nodes in that path use `SarAccelControlToken`.
- `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` are wrapper aliases to accel implementations in `examples/SAR/include/sar/H2DAsyncNode.hpp`, `examples/SAR/include/sar/D2HAsyncNode.hpp`, and `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`.
- Native Metal backprojection is delegated through `DeviceKernelNodeMetal` when capabilities bind in `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`.

Inferred:
- The old class names remain as compatibility/naming aliases, not separate active message paths.

## 3. Current Token/Data Flow

Observed:
- Source nodes populate `SarSidecar` and `host_view`; host pointers are opaque sentinels such as `0x1` in `SyntheticApertureIqSourceNode.cpp` and `GotchaReplaySourceNode.cpp`.
- `H2DAsyncAccelNode` validates `host_view`, synthesizes `device_view`, records lease and transfer ticket, and updates sidecar H2D queue/timing.
- Backprojection validates `device_view`, either delegates native Metal or synthesizes output, records kernel ticket, and updates sidecar kernel queue/timing.
- `D2HAsyncAccelNode` validates `device_view`, synthesizes `host_view`, records transfer ticket, and updates sidecar D2H queue/timing.
- `ImageTileMergeNode` aggregates merge/timing state into sidecar and also manufactures aggregate transfer/kernel ticket fields for output status.
- Tests assert sidecar preservation through split/H2D/backprojection/D2H/merge in `examples/SAR/test/test_sar_accel_nodes.cpp`.

Inferred:
- SAR identity is intended to live in `SarSidecar`; `host_ptr`, `device_ptr`, `ready_event`, and completion events are transport/trace artifacts.

## 4. Resolver Substitution Flow

Observed:
- Parser accepts `execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, `edge_contract`, and `resolver_mappings` in `libgraph/src/graph/GraphConfigParser.cpp`.
- Only `edge_contract: "accel-token"` is accepted when set.
- Legacy SAR payload contracts are rejected under accel-token mode.
- Default resolver registry maps generic GPU intents like `H2DAsyncNode`, `D2HAsyncNode`, `DeviceKernelNode`, etc. in `libgraph/src/graph/NodeResolutionRegistry.cpp`.
- `ResolvingNodeProvider` chooses concrete node type by backend preference: auto order is `metal`, `sycl`, `stub`, `cuda`.
- `GraphBuilder` loads resolver config, merges JSON mappings into the default registry, wraps the node provider, and records diagnostics.

Inferred:
- Resolver substitution is backend/type substitution only; it does not transform edge payloads.

## 5. Violations of Accel-Token Architecture

Observed:
- `host_ptr` is still required for `HostPinnedBufferView` validity and is filled with opaque sentinel values.
- `ready_event` remains part of `DeviceBufferView`; simulated H2D/backprojection set it to `0`, while native paths/tests may carry nonzero values.
- `ImageTileMergeNode` writes transfer/kernel ticket fields while performing SAR merge/reporting behavior.
- Benchmark trace still emits transport handles/signals: `host_view_handle`, `device_view_handle`, `ready_signal_id`, and completion signal IDs in `examples/SAR/src/sar_benchmark.cpp`.

Inferred:
- These are not currently identity channels, but they are still visible enough to be mistaken for identity-bearing fields.

## 6. Obsolete Abstractions

Observed:
- Legacy SAR message names survive as negative-validation literals.
- Old node names survive as alias wrappers.
- Deprecated SAR configs remain: `sar_stripmap_definitive_nonmetal.json` and `sar_stripmap_definitive_metal.json`.
- Default resolver contracts for H2D/D2H use generic token labels (`HostPinnedBufferView`, `DeviceBufferView`), while SAR configs/tests sometimes use `SarAccelControlToken`.

Inferred:
- Most obsolete pieces are compatibility/naming surfaces and test guardrails, not active alternate runtime payloads.

## 7. Complexity Hotspots

Observed:
- Duplicate `ElapsedUs` helpers exist in many SAR source files: split, merge, H2D, D2H, range window, diagnostics, range compression, and backprojection.
- Duplicate opaque pointer/event helper patterns exist across source, split, H2D, D2H, merge, Gotcha replay, and backprojection.
- `examples/SAR/src/sar_benchmark.cpp` is a large multi-concern surface: graph execution, baseline execution, parity, telemetry, trace schema, policy reporting, and device-reduce evaluation.
- Diagnostics sink resolution is duplicated in `main.cpp`, benchmark, and tests.

## 8. Blockers for `AccelControlToken<SarSidecar>`

Observed:
- The canonical token exists and is widely used.
- Guardrail tests enforce legacy payload rejection and token alias contracts.
- Remaining blockers are not absence of token type, but residual transport handle/event visibility, duplicate helper logic, and mixed generic/SAR resolver token naming.

Unknown:
- Whether current `host_ptr` and `ready_event` visibility is acceptable as transport-only state long term.

## 9. Existing External Comparison/Baseline Hooks

Observed:
- Deterministic baseline/parity work exists in `examples/SAR/src/sar_benchmark.cpp` and `examples/SAR/test/test_sar_baseline_compare.cpp`.
- External baseline policy/registry files exist under `plan/reviews`.
- Gotcha fixture conversion/replay exists with external data gated by `GRAPHX_SAR_ALLOW_EXTERNAL_DATA` and `allow_external_fixture`.
- RRP tooling exists under `examples/SAR/tools`, including local runner, Gotcha adapter, image comparator, and benchmark comparison script.
- `examples/SAR/main.cpp` is built as `sar_example`, but CTest does not directly run `sar_example`; automated SAR tests run `test_sar_example_unit`. `main.cpp` reports completion and queue diagnostics, while detailed performance reporting lives in `sar_benchmark`.

Inferred:
- External comparison is mostly kept in example/test/tooling layers, not core `libgraph`/`libgpu` contracts.
