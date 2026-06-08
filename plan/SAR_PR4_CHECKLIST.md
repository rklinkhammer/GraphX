# SAR PR4 Checklist: CPU Reference Correctness Harness

## Objective

Add a deterministic SAR correctness foundation before expanding native GPU/Metal work.

## Scope

- [x] Add deterministic point-target CPU reference utilities under `examples/SAR`.
- [x] Add scalar nearest-range backprojection reference for a tiny scene.
- [x] Add image metrics:
  - [x] peak location/value
  - [x] L-infinity error
  - [x] RMS error
  - [x] relative L2 error
  - [x] deterministic golden image hash
- [x] Add CI-safe unit tests for the point-target reference and metric contract.
- [x] Add benchmark attribution buckets for:
  - [x] algorithm baseline cost
  - [x] DSP/range stage
  - [x] transfer payload bytes
  - [x] kernel dispatches
  - [x] graph overhead
  - [x] diagnostics contract
- [ ] Add CPU-vs-Metal parity once native Metal output values are exposed through a testable data path.

## Non-Goals

- No large external datasets.
- No new production GPU kernels.
- No SAR-specific abstractions in `libgraph`.
- No raw SAR payload contracts across transfer/kernel graph edges.

## Acceptance Criteria

- [x] `test_sar_example_unit` passes.
- [x] `sar_benchmark --profile=ci --range-stage=compression --trace-out <file>` emits PR4 cost buckets.
- [x] Accel-token topology validation still rejects legacy payload contracts.
- [x] Benchmark output separates GraphX overhead from SAR algorithm/baseline cost.

## Follow-Up

- Replace the nearest-range scalar reference with a matched-filter-aware reference once range compression has a physically meaningful chirp/metadata model.
- Add a tiny legal Gotcha-derived fixture after raw-to-normalized conversion is defined and documented.
