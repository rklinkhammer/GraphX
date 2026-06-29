# GRAPHX VERIFIER REPORT PR2

PR: Remove FHSS Pulse Merge Duplicate Node

## 1. Verdict

Pass.

The implementation satisfies PR2 scope and acceptance criteria.

## 2. Scope compliance findings

- The implementation deletes the duplicate public `FHSSPulseMergeInteriorNode`.
- The canonical `FHSSPulseMergeNode` remains the only public FHSS pulse merge node in active code.
- Direct tests of the duplicate node were removed or retargeted to `FHSSPulseMergeNode`.
- The change does not modify FHSS graph JSON, plugin/provider behavior, channelizer behavior, detector behavior, decoder behavior, or runtime semantics.
- No future PR work was included.

## 3. Acceptance criteria findings

- `libdsp/include/dsp/fhss/FHSSPulseMergeInteriorNode.hpp` is deleted.
- `libdsp/src/dsp/FHSSPulseMergeInteriorNode.cpp` is deleted.
- `FHSSGraphXGuardrailTest.DuplicatePulseMergeInteriorNodeWasDeleted` now fails if the deleted header/source return.
- `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars` still validates canonical `FHSSPulseMergeNode` token-wrapped port contracts.
- Runtime merge behavior remains covered by:
  - `FHSSGraphXNodeTest.PulseMergeNodeConsumeQueuesDetectedPulseOutput`
  - `FHSSGraphXNodeTest.PerChannelPulseDetectorUsesSingleChannelMetadataAndMerges`
  - `FHSSGraphXNodeTest.PulseMergeNodeConsumeAccumulatesConfiguredPerChannelBatch`
  - `FHSSPulseMergeTest.*`
- Duplicate rejection, unsupported overlap/collision reporting, global-time ordering, slot indexing, and complex-evidence preservation remain covered by `FHSSPulseMergeTest.*`.

Acceptance criteria are satisfied.

## 4. Tests/build commands run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit test_dsp_example_unit
```

Result: pass, no work needed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars:FHSSGraphXNodeTest.PulseMergeNodeConsumeQueuesDetectedPulseOutput:FHSSGraphXNodeTest.PulseMergeNodeConsumeAccumulatesConfiguredPerChannelBatch:FHSSGraphXNodeTest.PerChannelPulseDetectorUsesSingleChannelMetadataAndMerges:FHSSPulseMergeTest.*'
```

Result: pass, 30 tests.

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit '--gtest_filter=DspFhssBaselineGuardrailTest.*'
```

Result: pass, 2 tests.

```bash
find libdsp/include/dsp/fhss libdsp/src/dsp -name 'FHSSPulseMergeInteriorNode*' -print
```

Result: no output; deleted files are absent.

```bash
rg "FHSSPulseMergeInteriorNode|fhss_pulse_merge_interior|PulseMergeInterior" -n libdsp libgraph examples
```

Result: only the deletion guardrail references the old name.

## 5. Files inspected

- `plan/roadmap/GRAPHX_PR_ROADMAP.md`
- `plan/reviews/GRAPHX_IMPL_PR2.md`
- `libdsp/include/dsp/fhss/FHSSPulseMergeInteriorNode.hpp`
- `libdsp/src/dsp/FHSSPulseMergeInteriorNode.cpp`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- `libgraph/test/unit/test_fhss_graphx_nodes.cpp`

## 6. Compatibility-shim or dual-canonical-path check

- No compatibility shim was added.
- No alias, wrapper, replacement typedef, plugin registration, or retained public class preserves the old `FHSSPulseMergeInteriorNode` API.
- The duplicate pulse merge path was removed.
- `FHSSPulseMergeNode` remains the sole canonical public FHSS pulse merge node.

## 7. Truth-in-labeling check

- No truth-in-labeling claims changed.
- The PR does not claim production RF, production channelizer behavior, FHSS GPU execution, Doppler/noise support, or overlap-aware separation.
- The canonical FHSS model remains real GraphX nodes with token-wrapped packet contracts.

## 8. Regression or deletion-risk findings

- No blocking regression found.
- Deleting the public header is intentionally breaking and matches the roadmap rule that backward compatibility is not required.
- Existing PR3 scope remains untouched: the correlator-bank reference/canonical cleanup was not performed here.

## 9. Required fixes before acceptance

None.
