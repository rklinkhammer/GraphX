# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

Phase 7 rerun executed on macOS with identity-guarded Jetson import handling.

Constraints applied in this rerun:
- GraphExecutor-only benchmark/correctness behavior.
- No Phase 8 deletion work.
This update continues **Phase 7 only** and does not perform any Phase 8 deletion/integration work.

Constraints held:
- GraphExecutor + plugin-loaded nodes only for benchmark-family runs.
>>>>>>> t1
- No legacy `libgpu` surface changes.
- No compatibility adapters/wrappers/aliases/shims.

## Identity Alignment

- Benchmark schema: `verification/accelgraph/phase-7/benchmark_result.schema.json`
- macOS benchmark run: `verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json`
- Jetson lane import status: `verification/accelgraph/phase-7/jetson-cuda-20260709T040049Z.json`
- Jetson import runbook: `verification/accelgraph/phase-7/JETSON_IMPORT_RUNBOOK.md`

macOS rerun identity to match:
- branch: `codex/gpu-clean-restart`
- commit_sha: `3b2e544cd199e11623cf35c04618477ef7f5ced3`
- diff_identity: `{"type":"working_tree","value":"uncommitted_changes_present","working_tree_dirty":true}`

Jetson Phase 7 schema artifact now includes and matches this identity.

## Updated Artifacts

- Schema: `verification/accelgraph/phase-7/benchmark_result.schema.json`
- Matching-identity Jetson local benchmark artifact:
	- `verification/accelgraph/phase-7/jetson-cuda-20260709T041603Z.json`
- Updated import source path:
	- `verification/accelgraph/phase-7/jetson-cuda-latest.json`
- Import validation artifact using matching Jetson schema artifact:
	- `verification/accelgraph/phase-7/jetson-import-check-20260709T042050Z.json`
- Existing macOS rerun artifact (external host evidence):
	- `verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json`

## Jetson Benchmark-Family Re-run (Local)

Rerun updates:
- benchmark parity now uses GraphExecutor output from the CPU benchmark-family row as the baseline;
- graph/lifecycle overhead estimate now uses GraphExecutor phase timings (`latency_ms - mean(run_elapsed_time_ms)`), not direct node execution;
- phase-7 schema now includes `branch`, `commit_sha`, and `diff_identity`;
- imported phase-7 CUDA rows are accepted only when branch/commit identity matches.
Command family used (CUDA-enabled build):
- benchmark target: `accelgraph_phase7_benchmark`
- configs:
	- CPU family: `accelgraph_phase7_spectrum_cpu_macos.json`
	- Jetson CUDA family (local): `accelgraph_phase7_spectrum_cuda_jetson` (local config variant)
>>>>>>> t1

From `jetson-cuda-20260709T041603Z.json`:

1. CPU row (local)
- correctness_parity_status: `pass`
- latency_ms: `21003.5866725`
- transfer_inclusive_gpu_time_ms: `0.0`

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

2. CUDA row (local)
- correctness_parity_status: `pass`
- latency_ms: `20753.29325675`
- transfer_inclusive_gpu_time_ms: `20753.29325675`
- cpu_gpu_speed_ratio: `1.0120604191659361`

## Jetson Import Validation

From `jetson-import-check-20260709T042050Z.json`:
- imported CUDA row status: `pass`
- origin: `imported=true`
- source_artifact: `verification/accelgraph/phase-7/jetson-cuda-latest.json`

## Telemetry Fields (Compute/Allocation)

Phase 7 required fields are present in the Jetson schema artifact.

- `compute_only_gpu_time_ms`: present, currently `null`
- `allocation_count`: present, currently `null`
- `allocation_bytes`: present, currently `null`

Interpretation:
- these fields are emitted and ready;
- backend telemetry support in the current runtime path does not yet provide non-null values.

## macOS CPU/Metal Re-run Requirement

This invocation runs on Jetson only. Native macOS Metal benchmark-family execution is not available on this host.

- Explicit Jetson attempt to run the macOS Metal benchmark-family config failed with expected host diagnostic:
	- `Metal support not compiled (ACCELGRAPH_ENABLE_METAL=OFF).`
- macOS CPU/Metal rerun evidence remains the external artifact:
	- `verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json`
>>>>>>> t1

## Replacement Recommendation

Decision: **NOT READY**

Rationale:
- macOS CPU and Metal benchmark-family parity is passing through GraphExecutor and plugin-loaded nodes.
- CPU and Metal throughput remain effectively equal for this benchmark family (`cpu_gpu_speed_ratio ~ 1.0`), with no demonstrated replacement-value speedup.
- Jetson CUDA lane cannot be accepted as imported evidence yet because branch/commit identity does not match or is missing in the provided phase-7 artifact.
- Compute-only GPU timing and allocation telemetry are still unavailable in the benchmark output.

Required before replacement approval:
- Produce a Jetson phase-7 artifact with matching `branch`, `commit_sha`, and `diff_identity` and pass CUDA parity in the same benchmark family.
- Add compute-only and allocation telemetry fields when backend telemetry supports them.
- Demonstrate credible replacement value for at least one legacy surface with non-trivial CPU/GPU advantage and parity maintained.

Reasoning:
- Parity is maintained across benchmark-family CPU/CUDA runs on Jetson.
- Jetson CUDA shows only a small improvement over CPU in this run (`cpu_gpu_speed_ratio ~ 1.012`), which is not yet strong enough as credible replacement value for a legacy surface.
- macOS CPU/Metal evidence still shows no meaningful acceleration advantage in prior rerun (`~1.0` ratio).
- Compute-only and allocation telemetry remain unavailable (`null`), reducing confidence in native compute-vs-transfer decomposition.

READY criterion was not met in this phase update because no candidate legacy surface demonstrated clear, credible replacement value with maintained parity.
