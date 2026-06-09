# SAR PR1 Implementation Tracking Checklist

Source plan: `plan/SAR.md`

## Status

- [x] PR1 started
- [x] PR1 implementation complete
- [x] PR1 ready for review
- [ ] PR1 merged

GitHub status snapshot (2026-06-07):

- [x] PR #22 opened for SAR PR1 follow-up commits
- [x] SAR Phase 6 diagnostics and queue/backpressure metrics committed and pushed
- [x] Strict `node_config` schema follow-up committed and pushed

## Scope Guardrails (Must Stay True)

- [x] Keep all SAR-specific implementation under `examples/SAR`
- [x] Keep PR1 to no more than 4 new SAR nodes
- [x] JSON-loaded topology is the primary demo path
- [x] Deterministic synthetic data path only (fixed seed + fixed counts)
- [x] No framework-wide rewrites
- [x] No mandatory native GPU requirement for CI
- [x] Every SAR node has a direct unit test
- [x] Every SAR node is dynamically loadable as a plugin
- [x] Every SAR node has a dynamic-load test that validates node behavior

## Phase 1 - Example Scaffolding

- [x] Create `examples/SAR/CMakeLists.txt`
- [x] Create `examples/SAR/README.md`
- [x] Create `examples/SAR/config/sar_stripmap_pr1.json`
- [x] Add optional build flag (for example `GRAPHX_BUILD_EXAMPLES_SAR`)
- [x] Wire example target(s) into root build

Exit criteria:

- [x] Example targets configure and build cleanly
- [x] SAR example can be toggled on/off via CMake option

## Phase 2 - Message Contracts

- [x] Add `examples/SAR/include/sar/SarMessages.hpp`
- [x] Define `SarPulseBlockMessage`
- [x] Define `SarRangeTileMessage`
- [x] Define `SarDeviceLeaseMessage`
- [x] Define `SarTransferTicketMessage`
- [x] Define `SarImageTileMessage`
- [x] Define `SarMergeStatusMessage`
- [x] Define `SarDiagnosticsMessage`
- [x] Include EOS/watermark semantics
- [x] Include required sequence/tile/backend metadata fields

Exit criteria:

- [x] Message contracts compile
- [x] Metadata fields cover sequence/tile IDs and device/backend diagnostics

## Phase 3 - New Nodes (PR1 Cap: 4)

### 3.1 SyntheticApertureIqSourceNode

- [x] Add header/source for `SyntheticApertureIqSourceNode`
- [x] Emit deterministic pulse blocks
- [x] Emit explicit EOS

### 3.2 AzimuthTileSplitNode

- [x] Add header/source for `AzimuthTileSplitNode`
- [x] Implement explicit tile fan-out with stable tile IDs

### 3.3 SarBackprojectionTransformNode

- [x] Add header/source for `SarBackprojectionTransformNode`
- [x] Implement deterministic simulated backprojection semantics
- [x] Preserve generic device-transform style metadata

### 3.4 ImageTileMergeNode

- [x] Add header/source for `ImageTileMergeNode`
- [x] Validate expected tile count
- [x] Detect duplicates
- [x] Detect missing tiles
- [x] Handle out-of-order completion
- [x] Handle EOS/watermark correctly

Exit criteria:

- [x] Exactly 4 new SAR nodes introduced (or fewer)
- [x] No SAR-specific libgpu framework expansion in PR1

## Phase 4 - Reused GPU Async Path Wiring

- [x] Reuse existing H2D async transfer pattern
- [x] Reuse existing device transform stage pattern
- [x] Reuse existing D2H async transfer pattern
- [x] Keep byte movement explicit at transfer boundaries
- [x] Keep control/data-plane separation explicit in messages and docs

Exit criteria:

- [x] End-to-end run works on CI-safe simulated backend path
- [x] H2D/D2H bytes and dispatch counters are emitted

## Phase 5 - JSON Topology + Runtime Entry

- [x] Implement SAR topology in `sar_stripmap_pr1.json`
- [x] Add executable entrypoint `examples/SAR/src/main.cpp`
- [x] Ensure plugin/provider bootstrap path is used
- [x] Avoid direct plugin-loader coupling in graph construction code
- [x] Initialize SAR nodes via standard `IConfigurable` + `node_config` path

Exit criteria:

- [x] JSON topology loads and executes from example app
- [x] Topology shows explicit fan-out/fan-in stages

## Phase 6 - Diagnostics and Metrics Contract

- [x] Emit `pulses_processed`
- [x] Emit `tiles_processed`
- [x] Emit `bytes_h2d`
- [x] Emit `bytes_d2h`
- [x] Emit `kernel_dispatches`
- [x] Emit `fanin_wait_ms`
- [x] Emit `e2e_latency_ms`
- [x] Emit duplicate/missing tile counts
- [x] Emit queue/backpressure data where already available

Exit criteria:

- [x] Metrics are deterministic enough for CI validation
- [x] No brittle hard performance thresholds required in CI

## Phase 7 - Tests

### Unit

- [x] Add `examples/SAR/test/test_image_tile_merge_node.cpp`
- [x] Add `examples/SAR/test/test_image_tile_merge.cpp`
- [x] Add `examples/SAR/test/test_sar_diagnostics_contract.cpp`
- [x] Add node-level determinism checks for source/split

### Integration

- [x] Add `examples/SAR/test/test_sar_json_pipeline.cpp`
- [x] Add `examples/SAR/test/test_sar_baseline_compare.cpp`
- [x] Validate simulated backend CI-safe execution

### Merge correctness matrix

- [x] Happy path: all tiles exactly once
- [x] Duplicate tile case
- [x] Missing tile case
- [x] Out-of-order completion case
- [x] EOS/watermark completion behavior

Exit criteria:

- [x] New SAR unit/integration tests pass
- [x] Graph and baseline outputs match within declared tolerance

## Phase 8 - Benchmark and Overhead Attribution

- [x] Implement non-graph baseline path for same synthetic dataset
- [x] Add warm-up and repeated measurement runs
- [x] Report median/min/max (+stddev when feasible)
- [x] Add CI-safe small profile
- [x] Add larger local profile

Overhead attribution categories:

- [x] Graph scheduling
- [x] Message allocation/copy
- [x] Queue wait/backpressure
- [x] Provider/plugin lookup
- [x] Diagnostics collection
- [x] Backend synchronization

Exit criteria:

- [x] Benchmark report includes graph vs baseline comparison
- [x] Correctness is CI-gated; performance thresholds remain conservative

## Phase 9 - Documentation and PR Packaging

- [x] Update SAR README with architecture diagram and run instructions
- [x] Document deterministic configuration knobs (seed/counts/tile size)
- [x] Document simulated backend behavior and native backend follow-up path
- [x] Document PR1 non-goals and deferred PR2/PR3 work
- [x] Add short decision-log summary in PR description

Exit criteria:

- [x] Reviewer can build, run, and validate SAR example from docs alone

## Post-PR Follow-up (2026-06-07)

- [x] SAR nodes expose `IConfigurable` and consume JSON `node_config` from topology
- [x] SAR plugin descriptors publish `config_fields` for strict loader validation
- [x] Unknown SAR `node_config` keys are rejected during graph load
- [x] Regressions fixed for `JsonDynamicGraphLoaderExpectedTest` optional-config behavior

## Final PR1 Release Gate

- [x] All scope guardrails are still true
- [x] All new tests pass in CI-safe profile
- [x] JSON path is primary demonstrated path
- [x] Diagnostics contract implemented and validated
- [x] Graph vs baseline benchmark summary included
- [x] Deferred work clearly listed (PR2/PR3+)

## Deferred Backlog (Do Not Pull Into PR1)

- [ ] Higher-fidelity SAR math (motion compensation, autofocus, radiometric calibration)
- [ ] Native backend specialization and tuning (CUDA/SYCL/Metal)
- [ ] DeviceReduce-centric accumulation showcase (if not included in PR1)
- [ ] Dynamic load balancing/work stealing
- [ ] Multi-device heterogeneous routing
- [ ] Full execution tracing framework expansion
