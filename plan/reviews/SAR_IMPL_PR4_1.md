# SAR Implementation Report - PR4

## 1. Decision made
- PR4 implemented by removing the global token-id keyed payload store from the primary SAR path.
- Materialization behavior is preserved through an explicit token-carried contract:
  - `SarMaterializedImageSinkNode` now derives deterministic materialized image output directly from token sidecar fields (`sequence_id`, `tile_id`, `payload_byte_count`) plus kernel-ticket validity.
- No compatibility shims were introduced.

## 2. Files changed
- `examples/SAR/src/SarMaterializedImageSinkNode.cpp`
- `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
- `examples/SAR/CMakeLists.txt`
- `examples/SAR/plugins/CMakeLists.txt`
- `examples/SAR/test/CMakeLists.txt`
- `examples/SAR/test/test_sar_materialized_image_sink_node.cpp`
- `examples/SAR/README.md`

## 3. Files deleted
- `examples/SAR/include/sar/SarAccelTokenImagePayloadStore.hpp`
- `examples/SAR/src/SarAccelTokenImagePayloadStore.cpp`

## 4. Tests added
- Updated/added explicit token-contract materialization validation in:
  - `examples/SAR/test/test_sar_materialized_image_sink_node.cpp`
  - `CapturesDeterministicImageFromTokenContract`

## 5. Tests removed or replaced
- Replaced obsolete store-dependent behavior in:
  - `examples/SAR/test/test_sar_materialized_image_sink_node.cpp`
  - `MissingPayloadDoesNotCaptureImage` -> reframed as `MissingKernelTicketDoesNotCaptureImage`
  - `ConsumesStoredPayloadWhenAvailable` -> replaced with token-contract deterministic capture test
- Removed linkage to deleted payload-store runtime in:
  - `examples/SAR/test/CMakeLists.txt`
  - `examples/SAR/plugins/CMakeLists.txt`
  - `examples/SAR/CMakeLists.txt`

## 6. Build commands run
1. `cmake --build build-ninja/ninja-debug-metal-native -j8`
2. `cmake --build build-ninja/ninja-debug-metal-native -j8 && ctest --test-dir build-ninja/ninja-debug-metal-native --output-on-failure`

## 7. Test commands run
1. `ctest --test-dir build-ninja/ninja-debug-metal-native --output-on-failure`

## 8. Test outcome
- Full configured lane green: 5/5 passed.
- Passed suites:
  - `libgraph_unit`
  - `libgraph_integration`
  - `libgpu_stub_unit`
  - `libgpu_metal_runtime`
  - `sar_example_unit`

## 9. Acceptance criteria check
- No primary-path reliance on global sidecar/payload store: **Satisfied**.
  - Payload store files removed.
  - No remaining `SarAccelTokenImagePayloadStore` references in `examples/SAR/**`.
- Materialization behavior preserved through explicit contracts: **Satisfied**.
  - Materialized sink continues deterministic capture using token-carried fields and kernel-ticket contract.
  - PR7 materialization/parity tests remain green under full suite.

## 10. Remaining follow-up items
- None required for PR4 scope.
- Optional: refresh any higher-level summary docs that still describe the removed shared payload registry as current behavior.
