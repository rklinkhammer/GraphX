# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

This update reruns **Phase 7 only** on macOS.

Constraints held:
- GraphExecutor + plugin-loaded nodes only for benchmark-family behavior.
- No Phase 8 deletion work.
- No legacy `libgpu` surface changes.
- No compatibility adapters/wrappers/aliases/shims.

## Artifacts

- Benchmark schema: `verification/accelgraph/phase-7/benchmark_result.schema.json`
- macOS benchmark run: `verification/accelgraph/phase-7/macos-metal-20260709T043226Z.json`
- Jetson lane import status: `verification/accelgraph/phase-7/jetson-cuda-20260709T043519Z.json`
- Jetson import source (not identity-matching in this invocation):
  `verification/accelgraph/phase-7/jetson-cuda-latest.json`
- Jetson import runbook: `verification/accelgraph/phase-7/JETSON_IMPORT_RUNBOOK.md`

## Results (Current Invocation)

From `verification/accelgraph/phase-7/macos-metal-20260709T043226Z.json`:

1. CPU-only (local, backend=cpu)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80469.979000
- steady_state_elapsed_time_ms: 60164.208916
- frames_per_second: 0.0498635327
- samples_per_second: 12.7650643769
- latency_ms: 20054.736305
- transfer_inclusive_gpu_time_ms: 0.0
- compute_only_gpu_time_ms: not available
- graph_overhead_ms: 48.736305
- correctness_parity_status: pass:cpu-benchmark-family-baseline

2. macOS Metal (local, backend=metal)
- packet_size: 256
- frame_count: 3 (warmup: 1)
- total_elapsed_time_ms: 80309.223292
- steady_state_elapsed_time_ms: 60210.911126
- frames_per_second: 0.0498248564
- samples_per_second: 12.7551632360
- latency_ms: 20070.303709
- transfer_inclusive_gpu_time_ms: 20009.666667
- compute_only_gpu_time_ms: not available
- graph_overhead_ms: 60.637042
- cpu_gpu_speed_ratio: 0.9992243564
- correctness_parity_status: pass

3. Jetson CUDA (import attempt on macOS)
- host_class: jetson-cuda
- measurement_origin.imported: true
- correctness_parity_status: pending:phase7-import-identity-mismatch
- transfer_inclusive_gpu_time_ms: pending identity-matched import
- compute_only_gpu_time_ms: pending identity-matched import
- allocation_count/bytes: pending identity-matched import
- diagnostic: imported phase-7 artifact commit (`3b2e544...`) does not match current commit (`f3b2c611...`)

## Replacement Recommendation

Decision: **NOT READY**

Rationale:
- macOS CPU and Metal parity is passing through GraphExecutor and plugin-loaded nodes.
- CPU and Metal throughput remain effectively equal (`cpu_gpu_speed_ratio ~ 1.0`), with no credible replacement-value speedup.
- CUDA imported evidence cannot be accepted in this invocation due branch/commit identity mismatch.
- Compute-only GPU timing and allocation telemetry remain unavailable (`null`).

Required before replacement approval:
- Produce a Jetson phase-7 artifact with matching `branch`, `commit_sha`, and `diff_identity`.
- Preserve CUDA benchmark-family parity pass in that matching artifact.
- Add compute-only and allocation telemetry values when backend telemetry supports them.
- Demonstrate credible replacement value for at least one legacy surface with parity maintained.
