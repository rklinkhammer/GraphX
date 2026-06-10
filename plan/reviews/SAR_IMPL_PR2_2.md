# SAR Implementation Report - PR2 (Verifier Fixes)

## 1. Verifier findings addressed
- Converted canonical pre-GPU SAR path to token contracts end-to-end:
  - Source now emits `SarAccelControlToken`.
  - Range window/compression now consume and emit `SarAccelControlToken`.
  - Split now consumes and emits `SarAccelControlToken`.
- Updated adjacent pre-GPU compatibility paths (no shims) so full lane remains green:
  - Gotcha replay source now emits `SarAccelControlToken`.
  - Sar pulse fanout now uses `SarAccelControlToken` ports.
- Updated benchmark/test call sites to reflect token contracts.
- Added explicit type-level PR2 assertions for source/window/compression/split token signatures.

## 2. Files changed
- `examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp`
- `examples/SAR/src/SyntheticApertureIqSourceNode.cpp`
- `examples/SAR/include/sar/RangeWindowNode.hpp`
- `examples/SAR/src/RangeWindowNode.cpp`
- `examples/SAR/include/sar/RangeCompressionNode.hpp`
- `examples/SAR/src/RangeCompressionNode.cpp`
- `examples/SAR/include/sar/AzimuthTileSplitNode.hpp`
- `examples/SAR/src/AzimuthTileSplitNode.cpp`
- `examples/SAR/include/sar/GotchaReplaySourceNode.hpp`
- `examples/SAR/src/GotchaReplaySourceNode.cpp`
- `examples/SAR/include/sar/SarPulseFanoutNode.hpp`
- `examples/SAR/src/sar_benchmark.cpp`
- `examples/SAR/test/test_sar_pr2_token_contract.cpp`
- `examples/SAR/test/test_range_window_node.cpp`
- `examples/SAR/test/test_range_compression_node.cpp`
- `examples/SAR/test/test_synthetic_aperture_iq_source_node.cpp`
- `examples/SAR/test/test_azimuth_tile_split_node.cpp`
- `examples/SAR/test/test_sar_accel_nodes.cpp`
- `examples/SAR/test/test_gotcha_dataset_adapter.cpp`
- `examples/SAR/test/test_sar_projectile_scenario.cpp`

## 3. Files deleted
- None.

## 4. Tests added
- Added PR2 token contract coverage in:
  - `examples/SAR/test/test_sar_pr2_token_contract.cpp`
- Added explicit compile-time/type-level token signature assertions in:
  - `examples/SAR/test/test_sar_pr2_token_contract.cpp`

## 5. Tests removed or replaced
- Replaced obsolete message/IQ-behavior assertions with token-contract assertions in:
  - `examples/SAR/test/test_range_window_node.cpp`
  - `examples/SAR/test/test_range_compression_node.cpp`
  - `examples/SAR/test/test_synthetic_aperture_iq_source_node.cpp`
  - `examples/SAR/test/test_azimuth_tile_split_node.cpp`
  - `examples/SAR/test/test_gotcha_dataset_adapter.cpp`
  - `examples/SAR/test/test_sar_projectile_scenario.cpp`

## 6. Build commands run
- `cmake --build build-ninja/ninja-debug-metal-native -j8`
- `cmake --build build-ninja/ninja-debug-metal-native -j8`

## 7. Test commands run
- `RunCtest_CMakeTools` (full configured lane)
- Final result: 5/5 tests passed
  - `libgraph_unit` passed
  - `libgraph_integration` passed
  - `libgpu_stub_unit` passed
  - `libgpu_metal_runtime` passed
  - `sar_example_unit` passed

## 8. Remaining follow-up items
- Optional follow-up: if DSP numerical behavior parity should be preserved under token contract (instead of timing/contract-only behavior pre-GPU), handle that as a separate PR with explicit payload representation requirements.
