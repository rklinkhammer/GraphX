# DSP CPU vs Metal Performance PR5 Implementer Report

## PR

PR5 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: Truth-In-Labeling Performance Documentation Audit.

## Files Changed

- `libgraph/test/unit/test_dsp_gpu_truth_in_labeling.cpp`
  - Added guardrails that active DSP performance docs require host/config-qualified measurement wording.
  - Added guardrails against unqualified Metal/GPU speed claims.
  - Added guardrails ensuring `MetalSpectrumDftNode<256>` is not documented as GPU FFT.
  - Added guardrails proving default CMake/CTest paths do not enable `GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`.
  - Added guardrails proving performance reports name `GraphExecutor::Execute()` and `run_elapsed_time_ms` as the timing source/basis.

## Files Deleted

- None.

## Tests Added

- `DspGpuTruthInLabelingTest.PerformanceDocsRequireMeasuredOnHostQualifier`
- `DspGpuTruthInLabelingTest.PerformanceDocsDoNotClaimGeneralGpuSuperiority`
- `DspGpuTruthInLabelingTest.MetalDftIsNeverDocumentedAsGpuFft`
- `DspGpuTruthInLabelingTest.DefaultCiDoesNotRequireSpeedup`
- `DspGpuTruthInLabelingTest.PerformanceReportsUseExecuteResultTiming`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspGpuTruthInLabelingTest.*' \
  --gtest_brief=1
```

Result: passed, 10 tests.

```bash
./build-ninja/ninja-debug-metal-native-strict/examples/DSP/test/test_dsp_example_unit --gtest_brief=1
```

Result: passed, 11 tests.

Search guardrail:

```bash
rg 'Metal is faster|GPU is faster|Metal outperforms|GPU outperforms|guaranteed speedup|general GPU superiority|is a GPU FFT|implements a GPU FFT' \
  README.md docs/dsp examples/DSP libgraph/test/unit/test_dsp_gpu_truth_in_labeling.cpp -n
```

Result: only the new guardrail test assertions contained those forbidden phrases.

## Scope Guardrails

- No algorithm behavior was changed.
- No benchmark feature was added.
- No true Metal FFT was implemented.
- Default CI remains free of native Metal speedup requirements.

## Remaining Follow-Up Work

- None for this PR.
