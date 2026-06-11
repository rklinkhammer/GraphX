# SAR Implementation Report - PR2-F1

## 1. Decision made
- Decision: token-only.
- Pre-GPU `RangeWindowNode` and `RangeCompressionNode` are currently token/timing placeholder stages, not numerically meaningful DSP transform stages.
- Dual runtime stage modes exist as separate presets:
  - Window mode: `examples/SAR/config/sar_stripmap_pr3_metal_window.json`
  - Compression mode: `examples/SAR/config/sar_stripmap_pr3_metal_compression.json`

## 2. Files changed
- `examples/SAR/README.md`
- `examples/SAR/test/test_range_window_node.cpp`
- `examples/SAR/test/test_range_compression_node.cpp`
- `examples/SAR/test/test_sar_pr3_metal_json.cpp`

## 3. Tests added or updated
- Added explicit deferred-numerical semantics test:
  - Range window token/timing-only behavior in `examples/SAR/test/test_range_window_node.cpp`
- Added explicit deferred-numerical semantics test:
  - Range compression token/timing-only behavior in `examples/SAR/test/test_range_compression_node.cpp`
- Added one integration test validating downstream diagnostics across both runtime range-stage modes:
  - `examples/SAR/test/test_sar_pr3_metal_json.cpp`
- Documented semantics decision and deferral in repo docs:
  - `examples/SAR/README.md`

## 4. Build/test commands run
1. `cmake --build build-ninja/ninja-debug-metal-native -j8`
2. `RunCtest_CMakeTools`

## 5. Test outcome
- Full lane green: 5/5 passed.
- PR2-F1 acceptance achieved:
  - Semantic decision is explicit in repository documentation.
  - Tests reflect token-only/timing semantics.
  - Both runtime range-stage modes are integration-tested for downstream diagnostics.
  - Numerical DSP deferral is explicit.

## 6. Deferred numerical DSP work (future PR)
- Deferred: reintroducing numerically meaningful host-side range window/compression transforms.
- Future PR should define payload representation expectations and add explicit numerical parity/fidelity tests for those stages.
