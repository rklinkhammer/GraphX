# SAR PR3 Checklist

Status:

- [x] PR3 started
- [ ] PR3 implementation complete
- [ ] PR3 ready for review
- [ ] PR3 merged

## Scope (from plan/SAR.md)

- [x] Native backend kernel path (feature-gated benchmark mode).
- [x] FFT-backed range compression path using libdsp in SAR example.
- [x] Improve overlap/transfer-kernel observability by exposing explicit timing counters.
- [x] Add real transfer/kernel timing metrics in diagnostics and trace output.

## Implementation Notes

- Added `RangeCompressionNode` (FFT-backed via `dsp::FFTManager`) as a SAR plugin stage.
- `sar_benchmark` now supports:
  - `--range-stage=window|compression`
  - `--native-backend`
  - existing `--trace-out`, `--evaluate-device-reduce`
- Added measured timing telemetry in SAR messages/diagnostics:
  - `transfer_h2d_time_us`
  - `kernel_exec_time_us`
  - `transfer_d2h_time_us`
- Trace schema now emits range-stage/native flags and timing telemetry fields.

## Validation Matrix

- [x] Build: `sar_benchmark` and `test_sar_example_unit`
- [x] Test: full SAR test suite (`test_sar_example_unit`)
- [x] Test: new `RangeCompressionNode` plugin/unit coverage
- [x] Run: `sar_benchmark --profile=ci --range-stage=compression --native-backend --trace-out ...`

## Remaining PR3 Follow-Up

- [ ] Replace simulated native path with backend-specific kernel execution (Metal/CUDA/SYCL) where runtime is available.
- [x] Add FFT-backed range compression into JSON scenario presets beyond benchmark-generated topologies.
- [x] Add Metal-focused SAR JSON presets for window and compression pipeline execution.
- [ ] Convert SAR Metal presets from backend-tagged SAR nodes to true `*Metal` node families where type contracts permit.
- [x] Complete SAR vs Metal node capability and contract gap analysis in `plan/SAR.md`.
- [x] Decide SAR<->Metal edge contract strategy (token edges; generic intent nodes resolved to backend-specific implementations).
- [ ] Implement generic-intent to backend-variant resolver policy for boundary nodes (H2D/D2H/transform/reduce where applicable).
- [ ] Add graph-build diagnostics that emit resolved concrete node types per generic intent.
- [ ] Add intent-conformance tests across stub/CUDA/SYCL/Metal lanes for substituted node families.
- [ ] Decide Metal kernel packaging strategy for SAR kernels (builtin vs inline source vs metallib).
- [x] Decide ownership boundary for SAR-metal adapters (`examples/SAR` owns SAR graph contracts; `libgpu` owns reusable Metal runtime nodes).
- [ ] Define PR3 acceptance gate for native runtime requirement (`GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME` policy).
- [ ] Implement explicit transfer/kernel overlap scheduling policy and quantify overlap utilization.
- [ ] Add backend-specific performance thresholds for local representative profile.
