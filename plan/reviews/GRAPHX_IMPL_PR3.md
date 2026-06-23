# GRAPHX PR3 Implementer Report

PR: Remove FHSS Correlator-Bank Canonical Surface

## 1. Files changed

- `README.md`
- `examples/DSP/src/fhss_demo.cpp`
- `libdsp/include/dsp/fhss/FHSSGraphXConfig.hpp`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_fhss_cpsm_decoder.cpp`
- `libgraph/test/unit/test_fhss_graphx_executor.cpp`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`
- `libgraph/test/unit/test_fhss_pulse_word_decoder.cpp`
- `plan/BASELINE.md`

## 2. Files deleted

- `libdsp/config/fhss_cpsm_fixture_500msps.json`
- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetector.hpp`
- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetectorNode.hpp`
- `libdsp/plugins/fhss_correlator_bank_detector_node_plugin.cpp`
- `libdsp/src/dsp/FHSSCorrelatorBankDetectorNode.cpp`
- `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp`

## 3. Tests added or updated

- Added guardrail coverage proving the correlator-bank detector source,
  plugin, config, and dedicated unit test stay deleted.
- Updated FHSS GraphX node tests to exercise the channelized source ->
  downconverter -> 64-port channelizer -> per-channel detector -> merge path
  instead of the removed correlator-bank detector.
- Updated CPSM decoder and pulse-word decoder tests so they derive evidence
  from generated complex IQ without depending on the deleted detector helper.
- Updated executor tests so the channelized graph is the only active FHSS
  executor topology under test.
- Updated docs/config guardrails so active docs/configs identify the
  channelized graph as the only active FHSS receiver topology.

## 4. Tests deleted

- Deleted the correlator-bank-only unit test:
  `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp`.

## 5. Build/test commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXExecutorTest.*:FHSSGraphXNodeTest.*:FHSSCpsmDecoderTest.KnownGeneratedPulseDecodesToSymbols:FHSSPulseWordDecoderTest.DecodesFromComplexDerivedCpsmDecisionsNotTruthMetadata'

./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit \
  '--gtest_filter=DspFhssDemoExecutableTest.*:DspFhssBaselineGuardrailTest.*'

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=*FHSS*:*CPSM*'
```

All listed commands passed.

## 6. Acceptance criteria status

- Channelized FHSS graph remains green.
- The correlator-bank graph was removed from active configs.
- The correlator-bank GraphX node, plugin target, config, and dedicated tests
  were deleted.
- Active docs/configs no longer retain or label the correlator-bank path as
  canonical, reference, compatibility, or production-like.

## 7. Truth-in-labeling status

- FHSS remains documented and tested as a deterministic CPU fixture.
- No production RF, production channelizer, GPU/Metal FHSS, Doppler/noise,
  overlap-aware separation, or external waveform compatibility claim was added.
- Magnitude-only DFT/FFT output remains documented as noncanonical decoder
  input.

## 8. Remaining follow-up work

- A local build tree created before this PR may still contain a stale
  `libfhss_correlator_bank_detector_node.dylib` artifact until the build
  directory is cleaned. The source target has been removed, and clean builds do
  not recreate it.

## 9. Scope intentionally not touched

- Did not implement PR4 repeated-port helper work.
- Did not change FHSS protocol behavior.
- Did not remove the existing detected-pulse packet contract, since it remains
  a generic merge input contract and not a correlator-bank canonical surface.
- Did not add graph JSON executor changes beyond removing the obsolete
  correlator-bank topology test/config.
