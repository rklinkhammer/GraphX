# GRAPHX PR3b Implementer Report

PR: Remove FHSS Correlator-Bank Canonical Surface

Follow-up input:

- `plan/reviews/GRAPHX_IMPL_PR3.md`
- `plan/reviews/GRAPHX_VERIFY_PR3.md`

## 1. Files changed

- `examples/DSP/CMakeLists.txt`
- `examples/DSP/src/fhss_demo.cpp`
- `examples/DSP/test/test_dsp_fhss_baseline_guardrails.cpp`
- `examples/DSP/test/test_dsp_fhss_demo.cpp`

## 2. Files deleted

- None in this PR3b follow-up.

The original PR3 deletion surface remains deleted:

- `libdsp/config/fhss_cpsm_fixture_500msps.json`
- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetector.hpp`
- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetectorNode.hpp`
- `libdsp/plugins/fhss_correlator_bank_detector_node_plugin.cpp`
- `libdsp/src/dsp/FHSSCorrelatorBankDetectorNode.cpp`
- `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp`

## 3. Tests added or updated

- Updated `DspFhssDemoExecutableTest.PrintsHelp` to assert the demo help no
  longer exposes `--reference-correlator-graph`.
- Added `DspFhssBaselineGuardrailTest.DemoDoesNotExposeDeletedReferenceCorrelatorSurface`
  to assert the demo source and CMake do not mention:
  - `--reference-correlator-graph`
  - `DSP_FHSS_REFERENCE_CONFIG_PATH`
  - `fhss_cpsm_fixture_500msps.json`

## 4. Tests deleted

- None.

## 5. Build/test commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=*FHSS*:*CPSM*'

./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit \
  '--gtest_filter=DspFhssDemoExecutableTest.*:DspFhssBaselineGuardrailTest.*'
```

All listed commands passed.

## 6. Acceptance criteria status

- Closed the PR3 verifier failure by removing the stale demo
  `--reference-correlator-graph` option.
- Removed `DSP_FHSS_REFERENCE_CONFIG_PATH` from the demo source and CMake
  target definitions.
- The demo now exposes only the channelized FHSS graph path by default unless
  the user explicitly passes another graph config with `--graph-config`.
- Channelized FHSS graph tests remain green.

## 7. Truth-in-labeling status

- FHSS remains a deterministic CPU fixture.
- No production RF, production channelizer, GPU/Metal FHSS, Doppler/noise,
  overlap-aware separation, external waveform, or correlator-bank
  compatibility claim was added.

## 8. Remaining follow-up work

- None for the PR3 verifier finding.

## 9. Scope intentionally not touched

- Did not implement PR4 repeated-port helper work.
- Did not change FHSS protocol behavior.
- Did not change GraphX executor APIs.
- Did not remove negative guardrail references that intentionally assert the
  correlator-bank surface stays absent.
