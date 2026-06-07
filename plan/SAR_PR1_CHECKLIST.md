# SAR PR1 Implementation Tracking Checklist

Source plan: `plan/SAR.md`

## Status

- [x] PR1 started
- [ ] PR1 implementation complete
- [ ] PR1 ready for review
- [ ] PR1 merged

GitHub status snapshot:

- [x] Issue #1 closed
- [x] Issue #2 merged via PR #12
- [x] Issue #3 merged via PR #14
- [x] Phase 3.1 active on issue #4

## Scope Guardrails (Must Stay True)

- [ ] Keep all SAR-specific implementation under `examples/SAR`
- [ ] Keep PR1 to no more than 4 new SAR nodes
- [ ] JSON-loaded topology is the primary demo path
- [ ] Deterministic synthetic data path only (fixed seed + fixed counts)
- [ ] No framework-wide rewrites
- [ ] No mandatory native GPU requirement for CI
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
- [ ] Add header/source for `SarBackprojectionTransformNode`
- [ ] Implement deterministic simulated backprojection semantics
- [ ] Preserve generic device-transform style metadata

### 3.4 ImageTileMergeNode
- [ ] Add header/source for `ImageTileMergeNode`
- [ ] Validate expected tile count
- [ ] Detect duplicates
- [ ] Detect missing tiles
- [ ] Handle out-of-order completion
- [ ] Handle EOS/watermark correctly

Exit criteria:
- [ ] Exactly 4 new SAR nodes introduced (or fewer)
- [ ] No SAR-specific libgpu framework expansion in PR1

## Phase 4 - Reused GPU Async Path Wiring

- [ ] Reuse existing H2D async transfer pattern
- [ ] Reuse existing device transform stage pattern
- [ ] Reuse existing D2H async transfer pattern
- [ ] Keep byte movement explicit at transfer boundaries
- [ ] Keep control/data-plane separation explicit in messages and docs

Exit criteria:
- [ ] End-to-end run works on CI-safe simulated backend path
- [ ] H2D/D2H bytes and dispatch counters are emitted

## Phase 5 - JSON Topology + Runtime Entry

- [ ] Implement SAR topology in `sar_stripmap_pr1.json`
- [ ] Add executable entrypoint `examples/SAR/src/main.cpp`
- [ ] Ensure plugin/provider bootstrap path is used
- [ ] Avoid direct plugin-loader coupling in graph construction code

Exit criteria:
- [ ] JSON topology loads and executes from example app
- [ ] Topology shows explicit fan-out/fan-in stages

## Phase 6 - Diagnostics and Metrics Contract

- [ ] Emit `pulses_processed`
- [ ] Emit `tiles_processed`
- [ ] Emit `bytes_h2d`
- [ ] Emit `bytes_d2h`
- [ ] Emit `kernel_dispatches`
- [ ] Emit `fanin_wait_ms`
- [ ] Emit `e2e_latency_ms`
- [ ] Emit duplicate/missing tile counts
- [ ] Emit queue/backpressure data where already available

Exit criteria:
- [ ] Metrics are deterministic enough for CI validation
- [ ] No brittle hard performance thresholds required in CI

## Phase 7 - Tests

### Unit
- [ ] Add `examples/SAR/test/test_image_tile_merge.cpp`
- [ ] Add `examples/SAR/test/test_sar_diagnostics_contract.cpp`
- [ ] Add node-level determinism checks for source/split

### Integration
- [ ] Add `examples/SAR/test/test_sar_json_pipeline.cpp`
- [ ] Add `examples/SAR/test/test_sar_baseline_compare.cpp`
- [ ] Validate simulated backend CI-safe execution

### Merge correctness matrix
- [ ] Happy path: all tiles exactly once
- [ ] Duplicate tile case
- [ ] Missing tile case
- [ ] Out-of-order completion case
- [ ] EOS/watermark completion behavior

Exit criteria:
- [ ] New SAR unit/integration tests pass
- [ ] Graph and baseline outputs match within declared tolerance

## Phase 8 - Benchmark and Overhead Attribution

- [ ] Implement non-graph baseline path for same synthetic dataset
- [ ] Add warm-up and repeated measurement runs
- [ ] Report median/min/max (+stddev when feasible)
- [ ] Add CI-safe small profile
- [ ] Add larger local profile

Overhead attribution categories:
- [ ] Graph scheduling
- [ ] Message allocation/copy
- [ ] Queue wait/backpressure
- [ ] Provider/plugin lookup
- [ ] Diagnostics collection
- [ ] Backend synchronization

Exit criteria:
- [ ] Benchmark report includes graph vs baseline comparison
- [ ] Correctness is CI-gated; performance thresholds remain conservative

## Phase 9 - Documentation and PR Packaging

- [ ] Update SAR README with architecture diagram and run instructions
- [ ] Document deterministic configuration knobs (seed/counts/tile size)
- [ ] Document simulated backend behavior and native backend follow-up path
- [ ] Document PR1 non-goals and deferred PR2/PR3 work
- [ ] Add short decision-log summary in PR description

Exit criteria:
- [ ] Reviewer can build, run, and validate SAR example from docs alone

## Final PR1 Release Gate

- [ ] All scope guardrails are still true
- [ ] All new tests pass in CI-safe profile
- [ ] JSON path is primary demonstrated path
- [ ] Diagnostics contract implemented and validated
- [ ] Graph vs baseline benchmark summary included
- [ ] Deferred work clearly listed (PR2/PR3+)

## Deferred Backlog (Do Not Pull Into PR1)

- [ ] Higher-fidelity SAR math (motion compensation, autofocus, radiometric calibration)
- [ ] Native backend specialization and tuning (CUDA/SYCL/Metal)
- [ ] DeviceReduce-centric accumulation showcase (if not included in PR1)
- [ ] Dynamic load balancing/work stealing
- [ ] Multi-device heterogeneous routing
- [ ] Full execution tracing framework expansion
