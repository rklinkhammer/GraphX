# SAR Simplifier Report

Source input: plan/reviews/SAR_INSPECTOR_REPORT.md

Scope: target architecture only. No implementation and no PR plan.

## 1. Target Type Model

- Keep `SarSidecar` as the only SAR identity/diagnostics carrier.
- Keep `AccelControlToken<SidecarT>` as the canonical transport contract template.
- Keep `SarAccelControlToken = AccelControlToken<SarSidecar>` as the only SAR GPU-edge payload.
- Keep generic accel transport primitives in `libgpu` (`DeviceBufferView`, `HostPinnedBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`, `TensorLayout`).
- Keep `host_ptr` and `ready_event` strictly transport-opaque and non-identity.
- Keep SAR-specific semantic fields in `SarSidecar`; keep transport state in accel views/tickets.
- Keep `SarDiagnosticsSnapshot` as sink-facing aggregate output, sourced from sidecar + graph metrics.
- Remove reliance on any legacy payload-contract names under accel-token mode.

## 2. Target Node Model

Canonical runtime path:

`Synthetic/Replay SAR source` -> `SAR DSP/prep` -> `SarAccelControlToken` -> `GPU transfer/kernel stage (token-preserving)` -> `SarAccelControlToken` -> `SAR merge/diagnostics/sinks`

Concrete target for maintained definitive topology:

- Keep source/prep nodes: `SyntheticApertureIqSourceNode`, `RangeWindowNode`, `RangeCompressionNode`, `AzimuthTileSplitNode`.
- Keep GPU-edge intent nodes in SAR config as explicit token contracts: `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, `D2HAsyncAccelNode`.
- Keep completion/diagnostics nodes: `ImageTileMergeNode`, `SarDiagnosticsSinkNode`.
- Keep optional/example-local nodes as non-definitive: `GotchaReplaySourceNode`, `SarPulseFanoutNode`, `SarVisualizationSinkNode`, `SarMaterializedImageSinkNode`.
- Keep backend realization in resolver/capability layers; keep definitive JSON portable and intent-based.

## 3. Deletion List

- Delete compatibility or legacy payload-contract references from maintained SAR runtime/topology surfaces (if still present outside negative-validation tests).
- Delete non-definitive SAR configs that are no longer required for maintained workflows once equivalent focused tests exist.
- Delete external-manual topology scaffolds from maintained runtime surface if they are not actively used (`sar_gotcha_external_manual.json` class of artifacts).
- Delete obsolete tests whose only value is preserving legacy alias names or deprecated compatibility behavior.
- Delete duplicate architecture-policy checks if they are enforced in multiple places with no additional signal.

## 4. Replacement List

- Replace any remaining generic/legacy runtime intent usage in maintained SAR configs with explicit SAR token intents and resolver mappings.
- Replace mixed identity interpretation opportunities with strict sidecar-only identity checks in tests and diagnostics.
- Replace ad-hoc scenario coverage with one definitive topology plus focused behavior tests (token preservation, resolver contract, diagnostics propagation).
- Replace dual-purpose runtime artifacts with explicit classification:
  - maintained definitive runtime path
  - optional local-only/baseline/comparison harness path
- Replace implicit external-baseline assumptions with explicit policy/registry contract assertions only.

## 5. Architecture Invariants

- Exactly one SAR GPU-edge contract in runtime: `AccelControlToken<SarSidecar>`.
- SAR identity is sidecar-only.
- `host_ptr`, `device_ptr`, `ready_event`, transfer IDs, kernel IDs, queue IDs are transport metadata, never SAR identity.
- Resolver contract for maintained SAR topology is explicit (`edge_contract: "accel-token"`) with SAR token type mappings.
- Generic GPU nodes and accel transport types remain SAR-unaware.
- SAR semantics remain in SAR nodes and sidecar fields.
- Definitive topology remains portable intent names; backend-specific realization stays in resolver/capability layers.
- `examples/SAR/main.cpp` remains covered by executable test and emits runtime diagnostics.
- External baselines (SarPy/gotcha-back/etc.) remain local-only comparison infrastructure and must not define GraphX core runtime contracts.

## 6. Open Questions That Block Planning

- Which non-definitive SAR configs are truly maintained versus historical test scaffolds?
- Should `SarBackprojectionTransformAccelNode` stay as a dual simulated/native node, or be split into clearer adaptor + backend execution responsibilities?
- What is the minimal retained set of baseline/comparison tests that preserves policy guarantees without duplicate coverage?
- Which optional SAR nodes (`SarPulseFanoutNode`, visualization/materialized sinks, replay sources) are required in maintained CI lanes versus local-only workflows?
- Should external-manual topology artifacts remain in-tree as examples or move to docs/local scripts only?
- Is current `main.cpp` diagnostics output considered sufficient performance reporting, or must benchmark-only metrics be promoted into the runtime executable output contract?
