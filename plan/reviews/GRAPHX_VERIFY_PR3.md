# GRAPHX PR3 Verifier Report

PR: Remove FHSS Correlator-Bank Canonical Surface

## 1. Verdict

Fail.

The core correlator-bank node/config/plugin/test surface was removed and the
channelized graph remains green, but PR3 is not fully accepted because the DSP
FHSS demo still exposes a user-visible reference-correlator option that points
to the deleted config.

## 2. Scope compliance findings

Pass for the main source-tree deletion scope:

- `libdsp/config/fhss_cpsm_fixture_500msps.json` is absent.
- `FHSSCorrelatorBankDetector.hpp` is absent.
- `FHSSCorrelatorBankDetectorNode.hpp` is absent.
- `FHSSCorrelatorBankDetectorNode.cpp` is absent.
- `fhss_correlator_bank_detector_node_plugin.cpp` is absent.
- `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp` is absent.
- `libdsp/plugins/CMakeLists.txt` no longer registers a
  `fhss_correlator_bank_detector_node` plugin target.

Fail for the examples risk called out by PR3:

- `examples/DSP/src/fhss_demo.cpp` still defines
  `DSP_FHSS_REFERENCE_CONFIG_PATH` as
  `libdsp/config/fhss_cpsm_fixture_500msps.json`.
- `examples/DSP/src/fhss_demo.cpp` still advertises and parses
  `--reference-correlator-graph`.
- `examples/DSP/CMakeLists.txt` still defines
  `DSP_FHSS_REFERENCE_CONFIG_PATH` to the deleted config.
- Running the demo with `--reference-correlator-graph` fails because the config
  was deleted.

This leaves a compatibility/reference UI path for the removed topology rather
than fully eliminating it from active support.

## 3. Acceptance criteria findings

Partial.

- Channelized graph remains green.
- Active source/config no longer contains a correlator-bank GraphX node or
  plugin target.
- Guardrails prove the deleted correlator-bank source/config/test files stay
  absent.
- Active docs/configs checked by guardrails do not label the correlator-bank
  graph as canonical or production-like.
- However, the demo still labels a user option as
  `--reference-correlator-graph` and points it at the deleted graph config.
  This violates the PR3 goal to eliminate the noncanonical receiver topology
  from active support and the PR3 risk item about examples defaulting or
  routing to the older config.

## 4. Tests/build commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=*FHSS*:*CPSM*'
```

Result: passed, 102 tests.

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit \
  '--gtest_filter=DspFhssDemoExecutableTest.*:DspFhssBaselineGuardrailTest.*'
```

Result: passed, 5 tests.

Additional manual check:

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-fhss-demo \
  --reference-correlator-graph \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --executor-timeout-s 1
```

Result: failed with:

```text
FHSS demo failed: failed to open JSON file: /Users/rklinkhammer/workspace/GraphX/libdsp/config/fhss_cpsm_fixture_500msps.json
```

## 5. Files inspected

- `plan/roadmap/GRAPHX_PR_ROADMAP.md`
- `plan/agents/GRAPHX_PR_AGENTS.md`
- `plan/reviews/GRAPHX_IMPL_PR3.md`
- `README.md`
- `plan/BASELINE.md`
- `examples/DSP/src/fhss_demo.cpp`
- `examples/DSP/CMakeLists.txt`
- `examples/DSP/test/test_dsp_fhss_demo.cpp`
- `examples/DSP/test/test_dsp_fhss_baseline_guardrails.cpp`
- `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json`
- `libdsp/plugins/CMakeLists.txt`
- `libdsp/include/dsp/fhss/FHSSGraphXConfig.hpp`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- `libgraph/test/unit/test_fhss_graphx_executor.cpp`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`
- `libgraph/test/unit/test_fhss_cpsm_decoder.cpp`
- `libgraph/test/unit/test_fhss_pulse_word_decoder.cpp`

## 6. Compatibility-shim or dual-canonical-path check

Fail.

No alternate canonical graph config remains, but the demo still preserves a
compatibility/reference command-line route named
`--reference-correlator-graph`. Because the referenced config is deleted, this
is now a broken compatibility surface rather than a supported graph. PR3 should
remove the option and the `DSP_FHSS_REFERENCE_CONFIG_PATH` definitions, or
replace them with an explicit error path that does not preserve a
correlator-bank option as active documentation/API.

## 7. Truth-in-labeling check

Pass aside from the stale reference option.

The channelized graph remains documented/configured as a deterministic CPU
fixture, not production RF. No production RF, production channelizer,
GPU/Metal FHSS, Doppler/noise, overlap-aware separation, or external waveform
claim was added.

## 8. Regression or deletion-risk findings

- The deleted config filename remains compiled into the demo as
  `DSP_FHSS_REFERENCE_CONFIG_PATH`.
- The demo help text still advertises `--reference-correlator-graph`, but the
  path now fails at runtime.
- Existing demo tests cover the default channelized path but do not cover
  absence/rejection of the removed reference-correlator option.
- A previously built local plugin artifact may still exist under the build
  directory, but the source plugin target is removed and clean builds should
  not recreate it. This is not a source-tree acceptance failure.

## 9. Required fixes before acceptance

1. Remove the `--reference-correlator-graph` option from
   `examples/DSP/src/fhss_demo.cpp`, including help text, parser state, and
   `DSP_FHSS_REFERENCE_CONFIG_PATH` fallback.
2. Remove `DSP_FHSS_REFERENCE_CONFIG_PATH` from `examples/DSP/CMakeLists.txt`.
3. Add or update a guardrail/demo test proving the demo no longer exposes the
   removed reference-correlator option or deleted config path.
4. Re-run the affected build, FHSS/CPSM tests, and DSP FHSS demo tests.
