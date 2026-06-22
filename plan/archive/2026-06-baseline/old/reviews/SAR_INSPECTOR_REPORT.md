# SAR Inspector Report (Current State)

Scope: repository analysis only. No redesign. No implementation.

## Classification Key

- Observed: directly verified in repository files.
- Inferred: reasoned from observed evidence.
- Unknown: not verified from inspected scope.

## 1. Current Type Model

- Observed: Canonical SAR accel token type is `SarAccelControlToken = AccelControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` is the explicit SAR identity carrier (sequence, aperture, pulse/tile ranges, backend markers, stage timings) in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `AccelControlToken` carries sidecar plus accel views/tickets/lease fields and presence flags in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: Legacy payload-contract names are retained as guardrail rejection vocabulary (not canonical runtime contracts) in `libgraph/src/graph/GraphConfigParser.cpp`.
- Inferred: Type contract intent is single canonical accel-token payload after split; host pulse message remains pre-split carrier.

## 2. Current Node Model

- Observed: Runtime path uses host pulse message before split, then canonical accel token through GPU stages:
  - Source/range stage host path in `examples/SAR/src/sar_benchmark.cpp`.
  - Split emits token in `examples/SAR/src/AzimuthTileSplitNode.cpp`.
  - H2D/BP/D2H operate on token in `examples/SAR/src/H2DAsyncAccelNode.cpp`, `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`, `examples/SAR/src/D2HAsyncAccelNode.cpp`.
  - Merge/diagnostics consume token-derived status in `examples/SAR/src/ImageTileMergeNode.cpp` and `examples/SAR/src/SarDiagnosticsSinkNode.cpp`.
- Observed: Benchmark run result captures resolved backend, diagnostics, token lifecycle evidence, and native snapshots in `examples/SAR/src/sar_benchmark.cpp`.

## 3. Current Token/Data Flow

- Observed: Canonical flow in active benchmark/preset execution path is host pulse -> split -> tokenized H2D -> tokenized backprojection -> tokenized D2H -> merge -> diagnostics.
- Observed: Sidecar stage timings are propagated and aggregated into diagnostics and trace output in `examples/SAR/src/sar_benchmark.cpp` and `examples/SAR/src/SarDiagnosticsSinkNode.cpp`.
- Inferred: Identity continuity in runtime is sidecar-first; pointer/event channels are opaque transport fields.

## 4. Resolver Substitution Flow

- Observed: Resolver backend ordering/fallback behavior is explicit in `libgraph/src/graph/ResolvingNodeProvider.cpp`.
- Observed: Resolver diagnostics capture intent type, concrete type, selected backend, fallback metadata in `libgraph/src/graph/ResolvingNodeProvider.cpp`.
- Observed: SAR JSON presets consistently declare `execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, and `edge_contract: "accel-token"` under `examples/SAR/config/`.
- Inferred: Generic resolver path remains SAR-unaware while SAR adapter nodes hold SAR semantics.

## 5. Violations of Accel-Token Architecture

- Observed: No direct evidence in current runtime path that SAR identity is reconstructed from `host_ptr` or `ready_event`.
- Observed: Guardrail tests explicitly cover rejection of legacy payload contracts in `examples/SAR/test/test_sar_accel_token_guardrails.cpp`.
- Unknown: Whether any non-benchmark, non-definitive niche topology outside inspected presets reintroduces non-canonical identity usage.

## 6. Obsolete Abstractions Present

- Observed: Legacy names (`SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, `SarTransferTicketMessage`) remain in parser/test guardrails and planning docs, not as active runtime payload types:
  - Parser rejection list in `libgraph/src/graph/GraphConfigParser.cpp`.
  - Guardrail tests in `examples/SAR/test/test_sar_accel_token_guardrails.cpp`.
  - Parser unit tests in `libgraph/test/unit/test_graph_config_parser.cpp`.
- Inferred: These names currently function as negative-validation artifacts and historical compatibility vocabulary.

## 7. Complexity Hotspots

- Observed: Native Metal runtime capability file is large and multi-concerned (context, memory, transfer, kernel, telemetry) in `libgpu/src/gpu/metal/native/NativeMetalCapabilities.cpp`.
- Observed: Benchmark trace assembly is broad and includes many policy/instrumentation sections in `examples/SAR/src/sar_benchmark.cpp`.
- Observed: Mixed transport, diagnostics, and parity evidence checks are concentrated in `examples/SAR/test/test_sar_trace_schema.cpp`.
- Inferred: Inspection and maintenance cost is concentrated in these files due to cross-cutting instrumentation logic.

## 8. Blockers for `AccelControlToken<SarSidecar>`

- Observed: No hard blocker found in current repository state for continuing on canonical token contract.
- Observed: PR1-A2 and PR1-A3 instrumentation surfaces are present:
  - Telemetry snapshot API in `libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp`.
  - Memory pool snapshot API in `libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp`.
  - Trace export for native telemetry and memory metrics in `examples/SAR/src/sar_benchmark.cpp`.
- Unknown: Whether future preset additions could drift without additional guardrails beyond current schema/parser tests.
