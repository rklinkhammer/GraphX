# SAR Simplifier Report

Input basis: [plan/reviews/SAR_INSPECTOR_REPORT.md](plan/reviews/SAR_INSPECTOR_REPORT.md)
Mode: SIMPLIFIER
Constraints applied: backward compatibility not required, complexity is a defect, prefer deletion over compatibility.

## 1. Target type model

1. Introduce one canonical edge payload type: `AccelControlToken<SarSidecar>`.
2. `SarSidecar` owns all SAR identity/control metadata currently split across envelope/token bits:
   - `sequence_id`, `batch_id`, `aperture_id`, `pulse_range_start`, `pulse_range_count`,
   - `stream_id`, `tile_id`, `tile_count`, `marker`,
   - `backend_id`, queue metadata,
   - `synthetic`.
3. Device readiness/lease/transfer/kernel evidence remains explicit fields on `AccelControlToken`, never encoded via pointer/event channels.
4. SAR sample/image payload structs are not edge contracts; they are node-local working data only.
5. Generic accel runtime structs (buffer views/tickets) stay as backend primitives but are carried through token fields, not out-of-band side channels.
6. Global sidecar lookup maps are not part of target correctness.
7. Resolver contract boundaries become token-in/token-out for SAR GPU path stages.

## 2. Target node model

1. Exactly one canonical SAR GPU flow:
   `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
2. Every node in that flow consumes/emits `AccelControlToken<SarSidecar>` (single edge contract family).
3. `RangeWindowNode` and `RangeCompressionNode` remain SAR-owned in `examples/SAR` but migrate from `SarPulseBlockMessage` contracts to token contracts.
4. `H2DAsyncNode` and `D2HAsyncNode` remain generic-intent resolver nodes with backend substitution from libgraph defaults.
5. `SarBackprojectionTransformNode` remains SAR adapter intent with SAR-owned resolver mapping and delegates compute to generic libgpu device primitives.
6. `ImageTileMergeNode` and `SarDiagnosticsSinkNode` read sidecar directly from token; no pointer decode path.

## 3. Deletion list

1. Delete encoded token packing/unpacking via `host_ptr` and decode helpers in [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp) and [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp).
2. Delete `ready_event`-as-identity transport behavior in [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp), [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp), and [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp).
3. Delete sidecar global-store API/impl in [examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp](examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp) and [examples/SAR/src/SarAccelTokenSidecarStore.cpp](examples/SAR/src/SarAccelTokenSidecarStore.cpp).
4. Delete legacy SAR edge-message types from [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp):
   - `SarRangeTileMessage`
   - `SarDeviceLeaseMessage`
   - `SarTransferTicketMessage`
   - `SarImageTileMessage`
5. Delete alias-layer indirection files if redundant after token unification:
   - [examples/SAR/include/sar/H2DAsyncNode.hpp](examples/SAR/include/sar/H2DAsyncNode.hpp)
   - [examples/SAR/include/sar/D2HAsyncNode.hpp](examples/SAR/include/sar/D2HAsyncNode.hpp)
   - [examples/SAR/include/sar/SarBackprojectionTransformNode.hpp](examples/SAR/include/sar/SarBackprojectionTransformNode.hpp)
6. Delete compatibility tests asserting pointer/event encoded identity semantics once token contract is canonical.

## 4. Replacement list

1. Replace `SarPulseBlockMessage` edge contracts in [examples/SAR/include/sar/RangeWindowNode.hpp](examples/SAR/include/sar/RangeWindowNode.hpp) and [examples/SAR/include/sar/RangeCompressionNode.hpp](examples/SAR/include/sar/RangeCompressionNode.hpp) with `AccelControlToken<SarSidecar>`.
2. Replace split token-bit encoding with explicit token construction from sidecar fields.
3. Replace merge pointer decoding with direct sidecar reads from token.
4. Replace sidecar map correlation with in-token sidecar propagation rules.
5. Replace definitive JSON resolver token-type strings in [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json) to canonical token contract vocabulary.
6. Replace guardrail tests to assert:
   - no `host_ptr`/`ready_event` identity dependence,
   - explicit end-to-end token sidecar preservation.
7. Replace benchmark/report field derivations tied to decode helpers with token-native fields while preserving attribution schema intent.

## 5. Architecture invariants

1. Exactly one SAR GPU edge contract family is allowed: `AccelControlToken<SarSidecar>`.
2. No SAR identity/marker/routing semantics may be encoded in pointer values or event integers.
3. No global mutable sidecar registry participates in correctness.
4. libgraph default resolver registry remains generic and SAR-agnostic.
5. SAR-specific resolver mappings remain SAR-owned/dynamic in topology config, not libgraph defaults.
6. Definitive topology keeps portable intent node types; concrete backend types come from resolver/provider only.
7. Generic libgpu nodes remain SAR-unaware; SAR semantics stay in `examples/SAR` adapters and sidecar definitions.
8. `edge_contract` remains `accel-token`; parser guardrails continue rejecting legacy payload contracts.
9. No backward-compatibility requirement for deleted legacy SAR message-edge paths.

## 6. Open questions that block planning

1. Canonical placement for `AccelControlToken<SarSidecar>` type: SAR-only vs shared libgraph/libgpu namespace.
2. Sidecar mutation model across stages: immutable copy-forward vs controlled mutable updates.
3. Minimal token fields required for SAR correctness while keeping generic GPU nodes SAR-unaware.
4. Whether `RangeWindow`/`RangeCompression` run on host buffers wrapped in token or shift to device-buffer token stage pre-split.
5. Final JSON resolver token-type naming convention for input/output contract strings.
6. Scope cut for migration: definitive topology only vs all maintained SAR presets in same cleanup wave.
7. Test baseline reset boundary: which existing tests are deleted as obsolete vs rewritten as token-invariant tests.
