# GRAPHX IMPLEMENTER REPORT PR2

PR: Remove FHSS Pulse Merge Duplicate Node

## 1. Files changed

- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
  - Removed `FHSSPulseMergeInteriorNode` from the expected public FHSS node list.
  - Added `DuplicatePulseMergeInteriorNodeWasDeleted` guardrail.
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`
  - Removed direct include and type assertions for `FHSSPulseMergeInteriorNode`.
  - Removed duplicate comparison tests that only checked parity between the old duplicate node and the canonical node.
  - Retargeted consume/queue and configured per-channel batch tests to `FHSSPulseMergeNode`.

## 2. Files deleted

- `libdsp/include/dsp/fhss/FHSSPulseMergeInteriorNode.hpp`
- `libdsp/src/dsp/FHSSPulseMergeInteriorNode.cpp`

## 3. Tests added or updated

- Added `FHSSGraphXGuardrailTest.DuplicatePulseMergeInteriorNodeWasDeleted`.
- Updated FHSS GraphX node contract tests to cover only canonical `FHSSPulseMergeNode`.
- Updated the detected-pulse consume/queue test to use `FHSSPulseMergeNode`.
- Updated the configured per-channel batch consume test to use `FHSSPulseMergeNode`.

## 4. Tests deleted

- Removed direct duplicate-node parity tests:
  - `PulseMergeInteriorNodeMatchesDetectedPulseMergeBehavior`
  - `PulseMergeInteriorNodeMatchesPerChannelMergeBehavior`

The canonical behavior remains covered by:

- `FHSSGraphXNodeTest.PulseMergeNodeConsumeQueuesDetectedPulseOutput`
- `FHSSGraphXNodeTest.PerChannelPulseDetectorUsesSingleChannelMetadataAndMerges`
- `FHSSGraphXNodeTest.PulseMergeNodeConsumeAccumulatesConfiguredPerChannelBatch`
- `FHSSPulseMergeTest.*`

## 5. Build/test commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit
```

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars:FHSSGraphXNodeTest.PulseMergeNodeConsumeQueuesDetectedPulseOutput:FHSSGraphXNodeTest.PulseMergeNodeConsumeAccumulatesConfiguredPerChannelBatch:FHSSGraphXNodeTest.PerChannelPulseDetectorUsesSingleChannelMetadataAndMerges:FHSSPulseMergeTest.*'
```

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit '--gtest_filter=DspFhssBaselineGuardrailTest.*'
```

```bash
rg "FHSSPulseMergeInteriorNode|fhss_pulse_merge_interior|PulseMergeInterior" -n libdsp libgraph examples
```

All build and focused test commands passed. The remaining `rg` matches are only the deletion guardrail.

## 6. Acceptance criteria status

- Only one public FHSS pulse merge node remains in active code: `FHSSPulseMergeNode`.
- The duplicate public `FHSSPulseMergeInteriorNode` header and source are deleted.
- Direct tests of the duplicate node are removed.
- Canonical merge behavior remains covered through GraphX node API tests and kernel-level pulse merge tests.
- Guardrails now fail if the duplicate public interior node header/source returns.

## 7. Truth-in-labeling status

- No compatibility alias or shim was added for the deleted node.
- No runtime, RF, channelizer, GPU, Doppler/noise, overlap, or production claim changed.
- The canonical FHSS model remains real GraphX nodes with token-wrapped packet contracts.

## 8. Remaining follow-up work

- PR3 still owns removing or finalizing the reference-only FHSS correlator-bank surface.
- PR4/PR5 still own any deeper repeated-port helper or channelizer implementation simplification.

## 9. Scope intentionally not touched

- Did not remove `FHSSCorrelatorBankDetectorNode`.
- Did not alter FHSS graph JSON.
- Did not change plugin/provider behavior.
- Did not change `FHSSPulseMergeNode` runtime semantics.
- Did not add compatibility shims for `FHSSPulseMergeInteriorNode`.
