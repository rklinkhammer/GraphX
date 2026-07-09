# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

Phase 7 rerun executed on macOS with identity-guarded Jetson import handling.

Constraints applied in this rerun:
- GraphExecutor-only benchmark/correctness behavior.
- No Phase 8 deletion work.
- No legacy `libgpu` surface changes.
- No compatibility adapters/wrappers/aliases/shims.

## Artifacts

- Benchmark schema: `verification/accelgraph/phase-7/benchmark_result.schema.json`
- macOS benchmark run: `verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json`
- Jetson lane import status: `verification/accelgraph/phase-7/jetson-cuda-20260709T040049Z.json`
- Jetson import runbook: `verification/accelgraph/phase-7/JETSON_IMPORT_RUNBOOK.md`

## Benchmark Configurations Added

- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cpu_macos.json`
- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_metal_macos.json`
- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json`

## Benchmark Runner Added

- `libaccelgraph/test/bench/accelgraph_phase7_benchmark.cpp`
- CMake target: `accelgraph_phase7_benchmark`

Rerun updates:
- benchmark parity now uses GraphExecutor output from the CPU benchmark-family row as the baseline;
- graph/lifecycle overhead estimate now uses GraphExecutor phase timings (`latency_ms - mean(run_elapsed_time_ms)`), not direct node execution;
- phase-7 schema now includes `branch`, `commit_sha`, and `diff_identity`;
- imported phase-7 CUDA rows are accepted only when branch/commit identity matches.

Importer behavior (updated):
- If `imported_artifact` points to a valid phase-7 schema artifact with a matching CUDA row, the row is validated and merged directly (timing/throughput/allocation fields included as available).
- If phase-7 artifact is missing or lacks a matching CUDA row, output remains pending for CUDA lane.
- If a phase-6b verifier artifact is provided instead, correctness-only import fallback is used.

## Results (Current Invocation)

From `verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json`:

1. CPU-only (local, backend=cpu)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80512.114708
- steady_state_elapsed_time_ms: 60171.598375
- frames_per_second: 0.0498574092
- samples_per_second: 12.7634967450
- latency_ms: 20057.199458
- graph_overhead_ms: 47.199458
- transfer_inclusive_gpu_time_ms: 0.0
- compute_only_gpu_time_ms: not available
- correctness_parity_status: pass:cpu-benchmark-family-baseline

2. macOS Metal (local, backend=metal)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80280.191541
- steady_state_elapsed_time_ms: 60181.575291
- frames_per_second: 0.0498491438
- samples_per_second: 12.7613808094
- latency_ms: 20060.525097
- transfer_inclusive_gpu_time_ms: 20010.333333
- compute_only_gpu_time_ms: not available
- graph_overhead_ms: 50.191764
- cpu_gpu_speed_ratio: 0.9998342198
- correctness_parity_status: pass

3. Jetson CUDA (imported attempt)
- host_class: jetson-cuda
- measurement_origin.imported: true
- correctness_parity_status: pending:phase7-import-identity-mismatch
- transfer_inclusive_gpu_time_ms: pending identity-matched import
- compute_only_gpu_time_ms: pending identity-matched import
- allocation_count/bytes: pending identity-matched import
- diagnostic: imported phase-7 artifact lacks matching branch/commit identity

## Replacement Recommendation

Decision: NOT READY to replace libgpu pieces yet.

Rationale:
- macOS CPU and Metal benchmark-family parity is passing through GraphExecutor and plugin-loaded nodes.
- CPU and Metal throughput remain effectively equal for this benchmark family (`cpu_gpu_speed_ratio ~ 1.0`), with no demonstrated replacement-value speedup.
- Jetson CUDA lane cannot be accepted as imported evidence yet because branch/commit identity does not match or is missing in the provided phase-7 artifact.
- Compute-only GPU timing and allocation telemetry are still unavailable in the benchmark output.

Required before replacement approval:
- Produce a Jetson phase-7 artifact with matching `branch`, `commit_sha`, and `diff_identity` and pass CUDA parity in the same benchmark family.
- Add compute-only and allocation telemetry fields when backend telemetry supports them.
- Demonstrate credible replacement value for at least one legacy surface with non-trivial CPU/GPU advantage and parity maintained.
