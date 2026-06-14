# SAR Implementation Report: PR12

Date: 2026-06-12
PR: PR12
Title: CLI Skeleton For graphx-crsd-lite
Scope: Add `graphx-gotcha-to-crsd` CLI, wire MAT import + validation + chunking + graphx-crsd-lite writing + report emission, and add focused CLI tests.

## 1) Files Changed

- `examples/SAR/src/graphx_gotcha_to_crsd.cpp`
  - Added CLI executable implementation for `graphx-gotcha-to-crsd`.
  - Supports required args:
    - `--input-dir`
    - `--output-dir`
    - `--collection-id`
    - `--max-output-size-mb`
    - `--sort`
    - `--manifest`
    - `--mode`
    - `--validate`
    - `--emit-index`
  - Wires:
    - deterministic input ordering (`GotchaInputOrdering`)
    - MAT support gate (`GotchaMatInspector::HasHdf5Signature`)
    - normalized import (`GotchaMatReader`)
    - optional validation (`SarProductValidator`)
    - chunk planning (`SarProductChunker`)
    - lite output writing (`GraphxCrsdLiteWriter`)
    - index/report/warnings emission (`SarIoUtilities`)
  - Implements deterministic failures for invalid/missing inputs and unsupported modes/formats.
  - `--mode crsd` fails clearly with `crsd_mode_not_implemented`.

- `examples/SAR/CMakeLists.txt`
  - Added executable target `graphx_gotcha_to_crsd`.
  - Set output name to `graphx-gotcha-to-crsd`.
  - Added include/link/compile feature wiring.
  - Added install rule for new executable.

- `examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp`
  - Added focused PR12 CLI tests for help, deterministic failure modes, lite end-to-end tiny fixture success, and CRSD mode rejection.

- `examples/SAR/test/CMakeLists.txt`
  - Registered `test_graphx_gotcha_to_crsd_cli.cpp` in `test_sar_example_unit`.
  - Added `SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH` compile definition.
  - Added dependency on `graphx_gotcha_to_crsd` for test target.

## 2) Files Deleted

- None.

## 3) Tests Added

- `examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp`
  - `GraphxGotchaToCrsdCliTest.HelpDocumentsRequiredOptions`
  - `GraphxGotchaToCrsdCliTest.GraphxCrsdLiteModeWorksOnTinyFixture`
  - `GraphxGotchaToCrsdCliTest.InvalidInputAndEmptyInputAndMalformedManifestFailDeterministically`
  - `GraphxGotchaToCrsdCliTest.UnsupportedMatAndCrsdModeFailClearly`

## 4) Tests Removed or Replaced

- None.

## 5) Build Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run

- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='GraphxGotchaToCrsdCliTest.*'`
- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='GraphxCrsdLiteIoTest.*:SarProductChunkerTest.*:SarIoUtilitiesTest.*'`

## 7) Remaining Follow-Up Items

- PR12 intentionally does not implement full CRSD writing; `--mode crsd` is a deterministic clear failure path until PR15.
- No Python/SarPy tooling was added in this PR.
