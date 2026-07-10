# AccelGraph Phase 7 Benchmarking and Replacement Decision

## Scope

This update is Phase 7 only.

Constraints held:
- GraphExecutor and plugin-loaded nodes only for benchmark-family runs.
- No Phase 8 deletion or integration work.
- No legacy libgpu API surface modifications.

## Artifacts Updated In This Run

- Schema reference: verification/accelgraph/phase-7/benchmark_result.schema.json
- Jetson local CPU+CUDA benchmark-family artifact: verification/accelgraph/phase-7/jetson-cuda-20260709T044523Z.json
- Published import source artifact: verification/accelgraph/phase-7/jetson-cuda-latest.json
- Jetson import validation artifact: verification/accelgraph/phase-7/jetson-import-check-20260709T044948Z.json
- macOS reference artifact available in this workspace: verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json

## Identity Status

Jetson artifact identity from verification/accelgraph/phase-7/jetson-cuda-20260709T044523Z.json:
- branch: HEAD
- commit_sha: f3b2c611bec624dd721e5f43f36dafd6ea478043
- diff_identity: {"type":"working_tree","value":"clean","working_tree_dirty":false}

Interpretation:
- Commit identity matches the target f3b2c611 rerun.
- Branch identity does not match codex/gpu-clean-restart because this run was executed in detached HEAD.
- Import guard passes when producer and consumer are both on this detached identity (confirmed below).

## Jetson Benchmark-Family Results (Local)

From verification/accelgraph/phase-7/jetson-cuda-20260709T044523Z.json:

1. CPU row
- graph_configuration_name: accelgraph_phase7_spectrum_cpu_macos
- correctness_parity_status: pass:cpu-benchmark-family-baseline
- latency_ms: 21003.326207
- transfer_inclusive_gpu_time_ms: 0.0

2. CUDA row
- graph_configuration_name: accelgraph_phase7_spectrum_cuda_jetson
- correctness_parity_status: pass
- latency_ms: 21003.428421
- transfer_inclusive_gpu_time_ms: 20005.0
- cpu_gpu_speed_ratio: 0.9999951334611686

## Jetson Import Validation

From verification/accelgraph/phase-7/jetson-import-check-20260709T044948Z.json:
- imported CUDA row status: pass
- measurement_origin.imported: true
- source_artifact: verification/accelgraph/phase-7/jetson-cuda-latest.json

## macOS Re-import Constraint

This host cannot execute native macOS Metal benchmark-family runs.

- The available macOS evidence in this workspace is verification/accelgraph/phase-7/macos-metal-20260709T040049Z.json.
- The macOS artifact for f3b2c611 referenced earlier (macos-metal-20260709T043226Z.json) is not present in the current workspace state, so direct cross-host import verification against that exact file cannot be completed here.

## Replacement Recommendation

Decision: NOT READY

Reasoning:
- Parity passes for local Jetson CPU and CUDA rows.
- Imported CUDA row parity also passes in the current identity context.
- Observed acceleration value is effectively neutral on this benchmark-family run (cpu_gpu_speed_ratio approximately 1.0).
- Compute-only and allocation telemetry fields remain null in this lane.
- Branch identity remains HEAD due detached execution, so strict branch+commit identity matching to codex/gpu-clean-restart cannot be claimed from this Jetson artifact.

READY criteria not met:
- Re-run Jetson benchmark while checked out on branch codex/gpu-clean-restart at commit f3b2c611 to produce branch+commit identity match without detached HEAD.
- Re-run macOS import step against that exact Jetson artifact to confirm imported CUDA lane parity under the same branch+commit identity.
- Demonstrate non-trivial replacement-value advantage (not parity-only, ratio approximately 1.0).
