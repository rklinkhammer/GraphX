# SAR Verifier PERF PR1-A1

Date: 2026-06-09
Scope: PR1-A1 - Canonical SAR Stage Timing Spans
Verdict: PASS

## Pass/fail

PASS

## Blocking issues

- None.

## Non-blocking issues

- `range_window_time_us` is allowed to be zero in the trace schema assertion, while the definitive runtime test expects the other stage timing fields to be strictly positive. This is defensible because the benchmark trace may run either the window or compression path, but the asymmetry should be documented.
- There is a non-blocking compiler warning in `examples/SAR/src/ImageTileMergeNode.cpp` for an unused helper parameter. It does not affect correctness or test outcomes.

## Suggested fixes

1. Make the schema expectation for `range_window_time_us` explicitly conditional on `profile.range_stage == "window"`, and document zero for compression-mode traces.
2. Remove the unused helper parameter warning in `examples/SAR/src/ImageTileMergeNode.cpp`.
3. Optionally add a grouped benchmark-trace contract assertion for the stage timing block, rather than only per-field checks.

## Acceptance Criteria Verification

- Definitive topology emits stage timing fields in diagnostics and trace: PASS.
  - Runtime diagnostics assertions are present in `examples/SAR/test/test_sar_json_runtime.cpp` and verify:
    - `range_window_time_us`
    - `range_compression_time_us`
    - `split_time_us`
    - `h2d_stage_time_us`
    - `backprojection_stage_time_us`
    - `d2h_stage_time_us`
    - `merge_stage_time_us`
    - `diagnostics_sink_time_us`
  - Benchmark trace exports these fields in `examples/SAR/src/sar_benchmark.cpp`.

- Benchmark trace schema covers those fields: PASS.
  - Schema assertions are present in `examples/SAR/test/test_sar_trace_schema.cpp` for all required stage timing keys.

- Full CTest lane remains green: PASS.
  - Fresh CTest result: 5/5 passed.
  - Passed suites:
    - `libgraph_unit`
    - `libgraph_integration`
    - `libgpu_stub_unit`
    - `libgpu_metal_runtime`
    - `sar_example_unit`

## Final Verifier Conclusion

PR1-A1 satisfies its stated acceptance criteria in the current repository state.
