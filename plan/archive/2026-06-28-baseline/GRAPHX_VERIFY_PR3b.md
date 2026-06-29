# GRAPHX PR3b Verifier Report

PR: Remove FHSS Correlator-Bank Canonical Surface

## 1. Verdict

Pass.

The PR3 implementation plus PR3b follow-up satisfies the PR3 scope. The
correlator-bank graph, node, plugin, config, and active demo surface have been
removed from active support, and the channelized FHSS graph remains green.

## 2. Scope compliance findings

- The correlator-bank graph config is no longer present under `libdsp/config`.
- The `FHSSCorrelatorBankDetector` helper/header and
  `FHSSCorrelatorBankDetectorNode` header/source are absent from active
  `libdsp` source/include directories.
- The correlator-bank plugin source and CMake plugin target are absent.
- The correlator-bank-only unit test is absent.
- The DSP FHSS demo no longer defines `DSP_FHSS_REFERENCE_CONFIG_PATH`.
- The DSP FHSS demo no longer advertises or parses
  `--reference-correlator-graph`.
- The follow-up stayed within PR3 scope and did not implement PR4 repeated-port
  helper work or other future roadmap items.

## 3. Acceptance criteria findings

- Channelized graph remains green.
- Active docs/configs do not label the correlator-bank graph as canonical or
  production-like.
- The previous verifier failure is closed: the demo no longer preserves a
  user-visible reference-correlator compatibility path.
- Guardrail tests now prove the demo source/CMake and demo help do not expose
  the deleted reference-correlator surface.

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

Result: passed, 6 tests.

Manual removed-option check:

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-fhss-demo \
  --reference-correlator-graph
```

Result: rejected as an unknown argument.

## 5. Files inspected

- `plan/roadmap/GRAPHX_PR_ROADMAP.md`
- `plan/reviews/GRAPHX_IMPL_PR3b.md`
- `examples/DSP/src/fhss_demo.cpp`
- `examples/DSP/CMakeLists.txt`
- `examples/DSP/test/test_dsp_fhss_demo.cpp`
- `examples/DSP/test/test_dsp_fhss_baseline_guardrails.cpp`
- `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- `libgraph/test/unit/test_fhss_graphx_executor.cpp`
- `README.md`
- `plan/BASELINE.md`

## 6. Compatibility-shim or dual-canonical-path check

Pass.

No active correlator-bank compatibility shim or dual canonical FHSS graph path
was found. The only remaining references to correlator-bank identifiers in
active source searches are negative guardrail assertions proving the old
surface is absent.

## 7. Truth-in-labeling check

Pass.

FHSS remains represented as a deterministic CPU fixture. No production RF,
production channelizer, GPU/Metal FHSS, Doppler/noise/multipath,
overlap-aware separation, external waveform compatibility, or canonical PDW
claim was added.

## 8. Regression or deletion-risk findings

- No source-tree regression found.
- A stale local build artifact could still exist in an old build directory, but
  the source plugin target has been removed and clean builds do not recreate
  the correlator-bank plugin.
- `--graph-config` remains available as a generic demo option. This is not a
  correlator-bank compatibility shim; it is the existing generic GraphX config
  input path.

## 9. Required fixes before acceptance

None.
