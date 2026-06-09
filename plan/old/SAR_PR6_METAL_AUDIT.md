# SAR PR6 Metal Auto-Substitution Audit

## Summary

This note captures the PR6 Metal substitution review for the SAR example. The result is intentionally conservative: the transfer-boundary Metal nodes already exist in `libgpu`, while SAR-specific stages that encode SAR semantics, tiling, fusion, or diagnostics remain under `examples/SAR` unless later reuse evidence justifies promotion.

## SAR Stage Matrix

| SAR stage | Closest Metal family | Substitution status | Ownership decision | Notes |
| --- | --- | --- | --- | --- |
| `SyntheticApertureIqSourceNode` | `HostIngressPinnedSourceNodeMetal` | Not a direct replacement | `examples/SAR` | Keep SAR replay/source behavior example-local; raw AFRL ingestion remains out of PR6 scope. |
| `RangeWindowNode` | `DeviceTransformNodeMetal` | Direct replacement is plausible | `libgpu` for reusable transforms; `examples/SAR` for SAR-only wrappers | Windowing is a generic transform if reuse evidence exists. |
| `RangeCompressionNode` | `DeviceKernelNodeMetal` | Partial; gated by CPU-reference parity | `examples/SAR` unless a reusable `libdsp` primitive is justified | PR6 uses runtime matched-filter parity tests; new kernels are deferred. |
| `AzimuthTileSplitNode` | `DeviceShardNodeMetal` | Not a direct replacement | `examples/SAR` | Split is graph routing and tile identity management, not a device kernel. |
| `SarPulseFanoutNode` | `SplitNode4` / `DeviceShardNodeMetal` | No direct Metal replacement needed | `examples/SAR` | Fan-out semantics are graph-level. |
| `H2DAsyncNode` | `H2DAsyncNodeMetal` | Direct replacement viable | `libgpu` preferred; SAR wrapper acceptable if example-scoped | Resolver tests now cover auto-selection, fallback, and diagnostics. |
| `SarBackprojectionTransformNode` | `DeviceKernelNodeMetal` | Direct replacement viable for native-device mode | `examples/SAR` | Keep SAR-specific kernels/adapters example-local unless reuse appears. |
| `D2HAsyncNode` | `D2HAsyncNodeMetal` | Direct replacement viable | `libgpu` preferred; SAR wrapper acceptable if example-scoped | Transfer-boundary behavior remains generic. |
| `ImageTileMergeNode` | `DeviceReduceNodeMetal` / `HostEgressSinkNodeMetal` | No direct replacement for the correctness path | `examples/SAR` | Merge correctness and diagnostics stay host-side in PR6. |
| `SarVisualizationSinkNode` | `HostEgressSinkNodeMetal` | Not a direct replacement | `examples/SAR` | Artifact generation is example-specific. |
| `SarDiagnosticsSinkNode` | `HostEgressSinkNodeMetal` / `QueueSyncNodeMetal` | Not a direct replacement | `examples/SAR` | Trace/metrics contracts remain SAR-owned. |
| `GotchaReplaySourceNode` | `HostIngressPinnedSourceNodeMetal` | Not a direct replacement | `examples/SAR` | Fixture replay is a SAR ingest adapter, not a generic Metal source. |

## Existing Metal Families Reviewed

| Metal family | Review outcome |
| --- | --- |
| `HostIngressPinnedSourceNodeMetal` | Exists and is the closest staging analog for source/replay ingress, but it does not replace SAR replay semantics. |
| `H2DAsyncNodeMetal` | Exists and is a direct resolver target for generic ingress transfer. |
| `DeviceShardNodeMetal` | Exists and is useful as the closest analog to SAR fan-out / sharding, but SAR split remains graph-level routing. |
| `DeviceTransformNodeMetal` | Exists and is the best generic fit for a reusable windowing transform. |
| `DeviceKernelNodeMetal` | Exists and is the right generic slot for SAR-specific or reusable native kernels. |
| `DeviceReduceNodeMetal` | Exists and is the closest generic fit for host-side reduction/merge-like work, but SAR merge semantics remain example-owned. |
| `D2HAsyncNodeMetal` | Exists and is a direct resolver target for generic egress transfer. |
| `HostEgressSinkNodeMetal` | Exists and is the sink analog for diagnostics / visualization / output capture. |
| `QueueSyncNodeMetal` | Exists and is relevant for queue/latency staging, but not a direct SAR semantic replacement. |
| `LeaseReleaseNodeMetal` | Exists and is appropriate for lifetime cleanup, but not a standalone SAR stage replacement. |
| `PeerCopyNodeMetal` | Exists and is a generic peer-transfer primitive; no SAR-specific substitution is needed in PR6. |
| `CollectiveReduceNodeMetal` | Exists but is runtime-unsupported; it is not part of the PR6 substitution path. |

## PR6 Decision

No new production Metal kernels are added in PR6. The audit result is:

1. Keep source, fan-out, merge, diagnostics, and replay behavior under `examples/SAR`.
2. Use the existing `libgpu` Metal families for transfer-boundary and generic kernel slots when resolver substitution is requested.
3. Gate SAR-specific backprojection or compression kernel expansion on explicit CPU-reference parity evidence in a later PR.

## New Metal Development Candidates

PR6 does not require any new Metal node or kernel descriptor beyond the existing `libgpu` families already reviewed above. The only PR6 action is to keep SAR-specific logic example-scoped and defer any kernel expansion until a later PR with accepted parity evidence.