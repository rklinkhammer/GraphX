# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

This update continues **Phase 7 only** and does not perform any Phase 8 deletion/integration work.

Constraints held:
- GraphExecutor + plugin-loaded nodes only for benchmark-family runs.
- No legacy `libgpu` surface changes.
- No compatibility adapters/wrappers/aliases/shims.

## Identity Alignment

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

Command family used (CUDA-enabled build):
- benchmark target: `accelgraph_phase7_benchmark`
- configs:
	- CPU family: `accelgraph_phase7_spectrum_cpu_macos.json`
	- Jetson CUDA family (local): `accelgraph_phase7_spectrum_cuda_jetson` (local config variant)

From `jetson-cuda-20260709T041603Z.json`:

1. CPU row (local)
- correctness_parity_status: `pass`
- latency_ms: `21003.5866725`
- transfer_inclusive_gpu_time_ms: `0.0`

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

## Replacement Recommendation

Decision: **NOT READY**

Reasoning:
- Parity is maintained across benchmark-family CPU/CUDA runs on Jetson.
- Jetson CUDA shows only a small improvement over CPU in this run (`cpu_gpu_speed_ratio ~ 1.012`), which is not yet strong enough as credible replacement value for a legacy surface.
- macOS CPU/Metal evidence still shows no meaningful acceleration advantage in prior rerun (`~1.0` ratio).
- Compute-only and allocation telemetry remain unavailable (`null`), reducing confidence in native compute-vs-transfer decomposition.

READY criterion was not met in this phase update because no candidate legacy surface demonstrated clear, credible replacement value with maintained parity.
