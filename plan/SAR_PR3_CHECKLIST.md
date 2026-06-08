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
- Current SAR graph-processing direction uses accel-token edge contracts (`HostPinnedBufferView`, `DeviceBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`) with SAR metadata sidecars.
- Historical SAR payload messages are compatibility/wrapper vocabulary only and should not be used as native PR3 transfer/kernel edge contracts.
- Added general `DeviceKernelNodeMetal` as the preferred libgpu boundary for one device input tile -> one device output tile SAR kernels. Remaining SAR work is kernel descriptor/source plus SAR sidecar adapter/parity tests.
- `SarBackprojectionTransformNode` now binds GPU capabilities in native-device mode, delegates to `DeviceKernelNodeMetal`, and preserves SAR sidecar identity for downstream merge.

## Validation Matrix

- [x] Build: `sar_benchmark` and `test_sar_example_unit`
- [x] Test: full SAR test suite (`test_sar_example_unit`)
- [x] Test: new `RangeCompressionNode` plugin/unit coverage
- [x] Run: `sar_benchmark --profile=ci --range-stage=compression --native-backend --trace-out ...`

## Remaining PR3 Follow-Up

- [ ] Replace simulated native path with backend-specific kernel execution (Metal/CUDA/SYCL) where runtime is available.
- [x] Add FFT-backed range compression into JSON scenario presets beyond benchmark-generated topologies.
- [x] Add Metal-focused SAR JSON presets for window and compression pipeline execution.
- [x] Convert SAR Metal/native presets from backend-tagged SAR payload semantics to generic-intent accel-token topology semantics.
- [x] Complete SAR vs Metal node capability and contract gap analysis in `plan/SAR.md`.
- [x] Decide SAR<->Metal edge contract strategy (token edges; generic intent nodes resolved to backend-specific implementations).
- [x] Add framework-level parsing/validation for resolver controls (`execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, `edge_contract`).
- [x] Implement generic-intent to backend-variant resolver policy for boundary nodes (H2D/D2H/transform/reduce where applicable).
- [x] Add graph-build diagnostics that emit resolved concrete node types per generic intent.
- [x] Add intent-conformance tests across stub/CUDA/SYCL/Metal lanes for substituted node families.
- [x] Decide Metal kernel packaging strategy for SAR kernels (inline-source descriptors for PR3; `.metallib` deferred until SAR kernel ABI stabilizes).
- [x] Decide ownership boundary for SAR-metal adapters (`examples/SAR` owns SAR graph contracts; `libgpu` owns reusable Metal runtime nodes).
- [x] Define PR3 acceptance gate for native runtime requirement (`GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME` policy).
- [x] Implement explicit transfer/kernel overlap scheduling policy and quantify overlap utilization policy.
- [x] Add backend-specific performance threshold policy for local representative profile.

## Accel-Token Conversion Audit

Missed conversions identified during documentation review:

- [x] Update PR3 SAR JSON presets so transfer/kernel/reduce boundaries express generic node intents with accel-token edge contracts, not legacy SAR payload-message edges or `backend=2` metadata as a stand-in for backend resolution.
- [x] Verify SAR PR3 presets are consumed through the typed `GraphConfig` resolver contract.
- [x] Add topology/schema validation that rejects native PR3 transfer/kernel edges carrying legacy `SarRangeTileMessage`/`SarImageTileMessage` payload-envelope contracts unless an explicit compatibility adapter is declared.
- [x] Ensure `SyntheticApertureIqSourceNode`, range window/compression, tile split, H2D, transform, D2H, merge, and diagnostics stages preserve SAR metadata as sidecars over accel tokens.
- [x] Ensure resolver substitution preserves sidecars when mapping `H2DAsyncNode`, `D2HAsyncNode`, transform, and reduce intents to stub/Metal/CUDA/SYCL variants.
- [x] Add graph-build diagnostics for each resolved node: intent type, concrete type, selected backend, fallback reason, input token type, output token type.
- [x] Extend trace output with token ids, lease ids, transfer ticket ids, kernel ticket ids, backend kind, device id, queue id, and sidecar identity fields.
- [x] Audit benchmark overhead attribution so message allocation/copy metrics do not count accel-token edge passing as raw SAR payload movement.

## Required PR3 Tests

- [x] Topology validation test: native/resolved SAR presets accept accel-token edge contracts and fail on legacy SAR payload-envelope transfer/kernel edges.
- [x] Resolver conformance test: generic transfer/kernel intents resolve to compatible concrete variants across available stub/Metal/CUDA/SYCL lanes.
- [x] Sidecar propagation test: `batch_id`, `aperture_id`, `pulse_range_start`, `pulse_range_count`, `tile_id`, `tile_count`, EOS/watermark, backend/device/queue ids survive source -> split -> H2D -> transform -> D2H -> merge.
- [x] Trace schema test: PR3 trace includes resolved concrete node type, token/lease/ticket identifiers, backend lane, queue id, transfer timing, kernel timing, and graph-vs-baseline overhead fields.
- [x] Negative compatibility test: backend-native PR3 presets reject `SarRangeTileMessage`/`SarImageTileMessage` style payload edges unless a named compatibility adapter is present.
- [x] Benchmark smoke test: `sar_benchmark --profile=ci --range-stage=compression --native-backend --trace-out ...` verifies token-edge counters and diagnostics parity against the non-graph baseline.
