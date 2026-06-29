# GRAPHX IMPLEMENTER REPORT PR1

PR: Baseline Architecture Guardrails

## 1. Files changed

- `README.md`
  - Added active cleanup roadmap and PR prompt references.
  - Added SAR GPU-path truth-in-labeling language.
- `plan/BASELINE.md`
  - Added active cleanup roadmap and PR prompt references.
  - Added the current canonical SAR GPU-path candidate and its experimental/incomplete status.
- `libgraph/test/unit/test_baseline_architecture_guardrails.cpp`
  - Added active-doc and core GraphX invariant guardrails.
- `examples/DSP/test/CMakeLists.txt`
  - Added the FHSS baseline guardrail test to `test_dsp_example_unit`.
- `examples/DSP/test/test_dsp_fhss_baseline_guardrails.cpp`
  - Added canonical FHSS channelized graph-shape guardrails.
- `examples/SAR/test/CMakeLists.txt`
  - Added the SAR baseline guardrail test to `test_sar_example_unit`.
- `examples/SAR/test/test_sar_baseline_guardrails.cpp`
  - Added SAR canonical GPU-path candidate and accel-token config guardrails.

## 2. Files deleted

- None.

## 3. Tests added or updated

- Added `BaselineArchitectureGuardrailTest`.
- Added `DspFhssBaselineGuardrailTest`.
- Added `SarBaselineGuardrailTest`.
- Updated DSP and SAR test target wiring.

## 4. Tests deleted

- None.

## 5. Build/test commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit test_sar_example_unit
```

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=BaselineArchitectureGuardrailTest.*:FHSSGraphXGuardrailTest.FhssCanonicalGraphConfigIsChannelized:FHSSGraphXGuardrailTest.FhssDocsAndConfigsKeepCorrelatorBankReferenceOnly:FHSSGraphXGuardrailTest.FhssNodeClassesInheritGraphXNodeBases'
```

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit '--gtest_filter=DspFhssBaselineGuardrailTest.*'
```

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit '--gtest_filter=SarBaselineGuardrailTest.*'
```

All focused commands passed.

## 6. Acceptance criteria status

- Active docs now name the current baseline, cleanup roadmap, and cleanup PR prompt file.
- Guardrails fail if active docs stop preserving the core GraphX runtime, token, truth-in-labeling, local-only, fixture, and experimental status language.
- FHSS guardrails prove the canonical channelized graph has 64 receiver frequency indices, 64 channel ids, 64 per-channel detector nodes, and 64 channelizer outgoing edges.
- FHSS guardrails prove the canonical graph uses real GraphX node names and excludes `FHSSCorrelatorBankDetectorNode` from the canonical channelized path.
- SAR guardrails name exactly one current SAR GPU-path candidate and require its config to use the accel-token contract.

## 7. Truth-in-labeling status

- Preserved CPU-only, fixture-only, reference-only, local-only, experimental/incomplete, and unsupported-path language.
- Did not promote FHSS to production RF or production channelizer status.
- Did not promote SAR Metal behavior beyond the current experimental/incomplete candidate status.
- Did not imply token readiness means GPU execution.

## 8. Remaining follow-up work

- PR2 still owns deletion of the duplicate FHSS pulse merge node.
- PR3 still owns correlator-bank removal or final reference-only treatment.
- PR7 still owns full SAR config consolidation.

## 9. Scope intentionally not touched

- No compatibility shims were added.
- No GraphX runtime APIs, adaptors, or executor paths were changed.
- No FHSS or SAR runtime behavior was changed.
- No production RF, Doppler/noise, overlap-aware separation, external SAR baseline, or performance instrumentation work was added.
