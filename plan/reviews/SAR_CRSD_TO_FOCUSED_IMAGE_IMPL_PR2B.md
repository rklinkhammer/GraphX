# SAR CRSD To Focused Image IMPLEMENTER Report - PR2b

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: corrective PR2b from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Binary CRSD product reader support for OrderedCrsdSetInputSourceNode

## Scope Implemented

Implemented corrective PR2b to make the ordered CRSD reader/source-node path consume binary `product.crsd` files used by generated and local real data workflows.

Completed:

- Upgraded `CrsdReader` binary path to parse CRSD header/XML/PVP/signal blocks from binary `product.crsd`.
- Kept tiny JSON fixture parsing only as explicit helper fallback (`graphx.sar.crsd.tiny.v1`) and made non-JSON/non-CRSD helper files fail deterministically instead of throwing JSON parse exceptions.
- Added robust signal sample width handling using block-size-derived bytes-per-sample with supported decode paths for 4/8/16 byte complex representations.
- Preserved authoritative-source rule: signal/PVP/geometry come from binary `product.crsd`; sidecars are optional and non-authoritative.
- Updated focused CRSD reader/source-node tests to use binary fixtures as principal inputs.
- Updated tiny fixture config examples to point to binary fixture products.
- Added local optional real-directory smoke config for `data/crsd`.
- Demonstrated end-to-end runtime success with local `data/crsd` through `sar_example`.

Out of scope remained unchanged: no aperture assembly, no focused-image transform, no Metal-focused lane redesign, no sink redesign, no SarPy CI dependency, no MATLAB.

## Files Changed

- examples/SAR/src/io/CrsdReader.cpp
- examples/SAR/test/test_crsd_input_source_node.cpp
- examples/SAR/test/CMakeLists.txt
- examples/SAR/config/sar_crsd_tiny_fixture_set_input.json
- examples/SAR/config/sar_crsd_tiny_fixture_set_input_directory.json
- examples/SAR/config/sar_crsd_tiny_fixture_set_input_manifest.json
- examples/SAR/config/sar_crsd_real_directory_input_smoke.json

## Tests and Validation

Commands executed:

1. Build (corrected target name):
   - `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit sar_example`
   - Result: PASS

2. Focused PR2b tests:
   - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='*CrsdReader*:*OrderedCrsdSetInputSourceNode*'`
   - Result: PASS (8 passed, 1 skipped gated local smoke)

3. Gated local smoke test using real `data/crsd`:
   - `GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='*OptionalLocalDataCrsdDirectorySmokeIsGated'`
   - Result: PASS (1 passed)

4. End-to-end local runtime smoke:
   - `./build-ninja/ninja-debug-metal-native/examples/SAR/sar_example examples/SAR/config/sar_crsd_real_directory_input_smoke.json`
   - Result: PASS (`Execution completed successfully.`)

## Implementation Notes

- Initial PR2b test run exposed `malformed_crsd:signal_block_too_small` for binary fixtures.
- Root cause: incorrect fixed mapping for `SignalArrayFormat` sample widths.
- Fix: infer bytes-per-complex-sample from `SIGNAL_BLOCK_SIZE/(NumVectors*NumSamples)` when available, with format fallback and explicit decode branches.
- Also fixed ParseOne fallback behavior so unsupported non-CRSD files return deterministic diagnostics rather than propagating JSON parse exceptions.

## Remaining Follow-Up Work

- PR2b verification pass/report (VERIFIER role).
- Optional: broaden binary parser coverage to additional CRSD signal encodings as future datasets require.
