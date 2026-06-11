# SAR Simplifier Report

Authoritative input used: `plan/reviews/SAR_INSPECTOR_REPORT.md`.
No fresh redesign implementation performed.

## 1. Target Type Model

1. One canonical runtime edge payload type for SAR GPU flow:
   `AccelControlToken<SarSidecar>`.
2. `SarSidecar` owns SAR identity/control metadata only:
   sequence, batch, aperture, pulse range, stream, tile, marker, backend, queue IDs, stage timings, payload byte count.
3. Pointer/event fields (`host_ptr`, `device_ptr`, `ready_event`, completion events) are transport/runtime handles only, never SAR identity carriers.
4. Legacy SAR payload contracts (`SarPulseBlockMessage`, `SarMergeStatusMessage`, `SarDiagnosticsMessage`) are non-canonical for core GPU flow.

## 2. Target Node Model

1. Exactly one canonical SAR GPU flow:
   `SAR DSP/source -> AccelControlToken<SarSidecar> -> H2D -> backprojection kernel -> D2H -> merge/diagnostics`.
2. Every inter-stage edge in that flow uses `AccelControlToken<SarSidecar>`.
3. Generic GPU transfer/kernel behavior remains in generic accel semantics; SAR-specific identity/metrics stay in sidecar and SAR nodes.
4. Merge/diagnostics consume sidecar-backed token information directly from the canonical token path, not reconstructed side channels.

## 3. Deletion List

1. Delete non-canonical runtime dependence on `SarPulseBlockMessage` in the canonical GPU path stages.
2. Delete global side-channel payload store as primary path (`SarAccelTokenImagePayloadStore` map + keying behavior).
3. Delete residual runtime logic that treats pointer/event channels as identity-like transport semantics.
4. Delete deprecated definitive config variants once canonical definitive config fully covers use cases (`sar_stripmap_definitive_nonmetal.json`, `sar_stripmap_definitive_metal.json`).

## 4. Replacement List

1. Replace message-to-token boundary at split with token-native upstream contract in canonical flow.
2. Replace side-channel materialized-image payload retrieval with explicit token-carried canonical data/metadata handoff contract.
3. Replace mixed boundary status wiring with token-derived diagnostics/merge outputs that do not introduce alternate identity channels.
4. Replace duplicated per-file timing helpers with one shared timing utility for SAR stage timing accumulation.

## 5. Architecture Invariants

1. Only one SAR GPU runtime contract family is allowed on canonical edges: `AccelControlToken<SarSidecar>`.
2. SAR identity is sidecar-only; host/device pointers and ready/completion events are never identity.
3. Resolver contract remains explicit and strict around accel-token mode (`edge_contract = accel-token`) and legacy payload rejection.
4. Generic GPU node contracts remain SAR-unaware; SAR semantics are attached via sidecar at SAR pipeline boundaries.
5. `examples/SAR/src/main.cpp` remains the canonical JSON+GraphExecutor entrypoint.
6. Metal remains first backend, with fallback policy and resolver diagnostics explicit in maintained presets.

## 6. Open Questions That Block Planning

1. Canonical scope decision: should non-GPU upstream SAR stages also be token-native, or is token canonicality limited strictly to GPU flow entry onward?
2. Materialized image contract decision: what exact explicit token-carried artifact replaces the current global payload store path?
3. Boundary decision: should merge/diagnostics remain separate message types for sink ergonomics, or be normalized as token-path consumers with optional projection messages?
4. Rollout decision: which maintained presets become strict canonical references first (`definitive`, `pr7`, gotcha manual), and in what order are deprecated variants removed?
