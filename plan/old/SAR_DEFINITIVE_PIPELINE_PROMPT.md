# Prompt: Definitive SAR Main Pipeline + Metal Performance Path

Use this prompt to audit and evolve the SAR example while preserving GraphX runtime contracts.

## Objective

Examine the SAR example and produce a definitive SAR pipeline pair for `examples/SAR/src/main.cpp`:

1. A strict accel-token non-METAL baseline JSON config.
2. A strict accel-token METAL-optimized JSON config with SAR-specific performance lanes.

## Required Constraints

1. Keep `GraphExecutorBuilder` plus JSON config as the canonical execution path.
2. Enforce `edge_contract: "accel-token"` in every maintained SAR runtime config.
3. Require resolver metadata fields in each definitive config:
- `execution_backend`
- `backend_fallback_policy`
- `resolver_diagnostics`
- `edge_contract`
4. Keep direct/non-graph paths limited to reference/parity and benchmark attribution only.

## Required Outputs

1. `examples/SAR/config/sar_stripmap_definitive_nonmetal.json`
- Uses `execution_backend: "auto"` and `backend_fallback_policy: "strict"`.
- Includes source, range stage(s), split, H2D, SAR backprojection, D2H, merge, sink.
- Uses accel-token edge model end-to-end.

2. `examples/SAR/config/sar_stripmap_definitive_metal.json`
- Uses `execution_backend: "metal"` and `backend_fallback_policy: "strict"`.
- Uses SAR-specific METAL performance topology:
  - Graph-visible fanout (`SarPulseFanoutNode`)
  - Per-tile split/H2D/backprojection/D2H lanes
  - Merge + diagnostics sink
- Keeps accel-token edge model end-to-end.

3. `examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh`
- Runs `sar_example` (main.cpp executable) against both definitive configs.
- Measures wall-clock run time over multiple iterations.
- Reports per-run ms + avg/min/max for both non-METAL and METAL configs.

4. README updates in `examples/SAR/README.md`
- Document every SAR node parameter currently exposed via `IParameterized::Fields()`.
- Add explicit run commands for definitive non-METAL and METAL configs through `sar_example`.
- Add benchmark usage instructions for `benchmark_main_metal_vs_nonmetal.sh`.

## Validation Checklist

1. Both definitive configs execute successfully through `sar_example`.
2. `sar_example` diagnostics sink reports completion signal in both configs.
3. METAL config resolves expected concrete backend nodes without breaking contracts.
4. Benchmark script runs both configs and prints timing summary.
5. Unit/integration tests remain green in SAR lane.

## Reviewer Evidence Expectations

1. File references for both definitive configs.
2. README parameter tables tied to node headers.
3. Benchmark sample output from main.cpp-based runs.
4. Explicit statement that accel-token contract remains strict in both definitive configs.
