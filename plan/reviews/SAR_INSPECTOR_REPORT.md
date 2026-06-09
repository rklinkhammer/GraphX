# SAR Inspector Report (Current State)

Scope: repository inspection only. No redesign. No implementation.

## Classification Key

- Observed: directly verified in inspected files.
- Inferred: reasoned from observed evidence.
- Unknown: not verified from inspected scope.

## 1. Current Type Model

1. Observed: SAR defines a mixed message model with SAR-specific envelopes and message structs in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
2. Observed: GPU contract types are graph accel views/tickets embedded in `SarGpuMetadata` (`HostPinnedBufferView`, `DeviceBufferView`, `TransferTicket`, `KernelTicket`) in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
3. Observed: Public node names `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` are aliases to accel implementations in [examples/SAR/include/sar/H2DAsyncNode.hpp](examples/SAR/include/sar/H2DAsyncNode.hpp), [examples/SAR/include/sar/D2HAsyncNode.hpp](examples/SAR/include/sar/D2HAsyncNode.hpp), and [examples/SAR/include/sar/SarBackprojectionTransformNode.hpp](examples/SAR/include/sar/SarBackprojectionTransformNode.hpp).
4. Observed: No explicit `AccelControlToken` symbol exists in inspected SAR/libgpu/libgraph code (search returned no matches).
5. Inferred: Current model is hybrid (SAR messages + accel views), not a single explicit control-token type.
6. Unknown: Whether any equivalent token abstraction exists outside inspected directories.

## 2. Current Node Model

1. Observed: Definitive topology node chain is:
`SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode` in [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json).
2. Observed: `RangeWindowNode` and `RangeCompressionNode` are host/SAR-message stages (`SarPulseBlockMessage` in/out) in [examples/SAR/include/sar/RangeWindowNode.hpp](examples/SAR/include/sar/RangeWindowNode.hpp) and [examples/SAR/include/sar/RangeCompressionNode.hpp](examples/SAR/include/sar/RangeCompressionNode.hpp).
3. Observed: `AzimuthTileSplitNode` is the conversion boundary to accel views and emits `HostPinnedBufferView` with encoded token bits in `host_ptr` in [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp).
4. Observed: `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode` operate on accel views and update a SAR sidecar store in [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp), [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp), and [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp).
5. Observed: `ImageTileMergeNode` decodes token bits and overlays sidecar-store data to build SAR status in [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp).

## 3. Current Token/Data Flow

1. Observed: Before split, data remains `SarPulseBlockMessage` through window/compression stages.
2. Observed: Split encodes marker/tile/sequence/byte/stream into a packed integer token and writes it into `host_ptr` in [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp).
3. Observed: H2D sets `DeviceBufferView.ready_event` from `host_ptr` and updates sidecar metadata in [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp).
4. Observed: Backprojection uses `ready_event` as token identity and updates sidecar kernel metadata in [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp).
5. Observed: D2H reconstructs `host_ptr` from `ready_event` and updates sidecar D2H metadata in [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp).
6. Observed: Merge decodes token bits from `host_ptr`, then prefers sidecar-store fields where available in [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp).
7. Inferred: Runtime identity propagation depends on encoded pointer/event channels plus out-of-band sidecar map state.

## 4. Resolver Substitution Flow

1. Observed: libgraph default resolver registry is generic and backend-based (`H2DAsyncNode`, `D2HAsyncNode`, `DeviceTransformNode`, `DeviceKernelNode`, etc.) in [libgraph/src/graph/NodeResolutionRegistry.cpp](libgraph/src/graph/NodeResolutionRegistry.cpp).
2. Observed: Backend selection logic uses requested backend + fallback policy ordering in [libgraph/src/graph/ResolvingNodeProvider.cpp](libgraph/src/graph/ResolvingNodeProvider.cpp).
3. Observed: Definitive SAR JSON uses portable intent node types and provides SAR-owned resolver mapping for `SarBackprojectionTransformNode` only in [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json).
4. Observed: Parser validation rejects legacy SAR payload contracts when `edge_contract` is `accel-token` in [libgraph/src/graph/GraphConfigParser.cpp](libgraph/src/graph/GraphConfigParser.cpp), with corresponding test in [examples/SAR/test/test_sar_accel_token_guardrails.cpp](examples/SAR/test/test_sar_accel_token_guardrails.cpp).
5. Inferred: SAR-specific substitutions are dynamic and SAR-owned via resolver mappings; libgraph defaults remain generic.

## 5. Violations of Accel-Token Architecture

1. Observed: SAR identity is encoded into `host_ptr` token bits at split and decoded at merge in [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp) and [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp).
2. Observed: `ready_event` is used as identity transport through accel stages in [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp), [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp), and [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp).
3. Observed: Sidecar state is kept in a process-global map keyed by token in [examples/SAR/src/SarAccelTokenSidecarStore.cpp](examples/SAR/src/SarAccelTokenSidecarStore.cpp).
4. Inferred: Configuration-level accel-token guardrails exist, but runtime path still relies on pointer/event encoding mechanics rather than explicit token object carriage.

## 6. Obsolete Abstractions

1. Observed: Legacy SAR payload/message structs remain defined in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp):
   - `SarRangeTileMessage`
   - `SarDeviceLeaseMessage`
   - `SarTransferTicketMessage`
   - `SarImageTileMessage`
2. Inferred: Not all legacy structs are canonical for definitive flow; some appear retained for compatibility/test/diagnostics contexts.
3. Unknown: Complete runtime reachability for each legacy struct across all presets/plugins not fully inspected.

## 7. Complexity Hotspots

1. Observed: Identity handling is distributed across token packing/unpacking, pointer casts, event fields, and sidecar map updates in:
   - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp)
   - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp)
   - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp)
   - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp)
   - [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp)
2. Observed: Sidecar lifecycle uses global mutable map + mutex with no explicit expiry/cleanup policy in [examples/SAR/src/SarAccelTokenSidecarStore.cpp](examples/SAR/src/SarAccelTokenSidecarStore.cpp).
3. Observed: Boundary mismatch exists between pre-split SAR-message stages and post-split accel-view stages in [examples/SAR/include/sar/RangeWindowNode.hpp](examples/SAR/include/sar/RangeWindowNode.hpp), [examples/SAR/include/sar/RangeCompressionNode.hpp](examples/SAR/include/sar/RangeCompressionNode.hpp), and [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json).
4. Inferred: This seam increases substitution complexity for generic `DeviceTransformNodeMetal`/`DeviceKernelNodeMetal` at window/compression stages.

## 8. Blockers for `AccelControlToken<SarSidecar>`

1. Observed: No `AccelControlToken` type exists in inspected code.
2. Observed: `RangeWindowNode`/`RangeCompressionNode` pre-split contracts are `SarPulseBlockMessage` in/out, not accel token/control types in [examples/SAR/include/sar/RangeWindowNode.hpp](examples/SAR/include/sar/RangeWindowNode.hpp) and [examples/SAR/include/sar/RangeCompressionNode.hpp](examples/SAR/include/sar/RangeCompressionNode.hpp).
3. Observed: Runtime identity and markers depend on encoded `host_ptr`/`ready_event` channels and sidecar map correlation in the split/H2D/kernel/D2H/merge chain.
4. Observed: Tests currently capture substitution blockers for forcing RangeWindow->DeviceTransform and RangeCompression->DeviceKernel in [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp).
5. Inferred: Introducing `AccelControlToken<SarSidecar>` as sole canonical carrier is blocked by current identity transport assumptions and mixed stage boundary contracts.
6. Unknown: Full migration blast radius across all non-definitive SAR presets, plugins, and tools without broader file-level trace.

## Repository State Note

1. Observed: At inspection time, git status showed untracked planning files (`plan/agents/`, `plan/sar_fix.md`) and no additional tracked-code deltas in this inspection pass.
