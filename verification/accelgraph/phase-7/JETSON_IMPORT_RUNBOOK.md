# Phase 7 Jetson Import Runbook

This runbook captures the exact steps to collect a Jetson CUDA phase-7 benchmark artifact and import it into the macOS phase-7 matrix report.

## 1) On Jetson: run phase-7 benchmark and emit schema report

Prerequisites:
- Build tree with CUDA lane enabled.
- `accelgraph_phase7_benchmark` target built.

Example:

```bash
cmake --build build-ninja/ninja-debug-linux-host --target accelgraph_phase7_benchmark -- -j$(nproc)

build-ninja/ninja-debug-linux-host/libaccelgraph/test/accelgraph_phase7_benchmark \
  --config=libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json \
  --frames=24 \
  --warmup=4 \
  --output=verification/accelgraph/phase-7/jetson-cuda-latest.json
```

Expected output schema:
- `schema = graphx.accelgraph.phase7.spectrum.benchmark.v1`
- `phase = 7`
- at least one `results[]` row with `backend = cuda`.

## 2) Move artifact to macOS workspace

Copy Jetson artifact into:

- `verification/accelgraph/phase-7/jetson-cuda-latest.json`

This path is already referenced by:

- `libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json`

## 3) On macOS: regenerate matrix with auto-import

```bash
build-ninja/ninja-metal-phase3/libaccelgraph/test/accelgraph_phase7_benchmark \
  --all-default-configs \
  --frames=24 \
  --warmup=4 \
  --output=verification/accelgraph/phase-7/macos-jetson-matrix-latest.json
```

The importer validates the Jetson artifact shape and merges the CUDA row into the same output report with:
- `measurement_origin.imported = true`
- `measurement_origin.source_artifact = verification/accelgraph/phase-7/jetson-cuda-latest.json`

## 4) Fallback behavior

If `jetson-cuda-latest.json` is missing or does not include a matching CUDA result row:
- CUDA row remains in pending import state.
- CPU/Metal local rows are still emitted.

If you need correctness-only fallback, point `imported_artifact` to a phase-6b verifier artifact in an alternate config and rerun.
