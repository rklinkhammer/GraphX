# SAR CRSD To Focused Image IMPLEMENTER Report - PR5c

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: Corrective PR5c from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Metal Node Truth-In-Labeling Guardrails

## Scope Implemented

Implemented PR5c guardrails to enforce honest classification and labeling of Metal-named nodes before PR6.

Completed:

- Added repository-level inventory and truth-in-labeling policy:
  - docs/sar/metal_node_truth_in_labeling.md
- Classified every active Metal-named node as memory, transfer, sync/control, sink/source, kernel primitive, domain algorithm, or unsupported.
- Recorded per-node truth fields in the inventory:
  - capability binding
  - native API usage mode (capability-mediated)
  - kernel launch behavior
  - whether kernel is expected
  - behavior label (real/simulated/fallback/unsupported)
- Added focused guardrails for CrsdFocusedImageTransformMetalNode:
  - node now reports explicit incomplete/experimental algorithm status
  - native success diagnostic now explicitly marks algorithm as incomplete
  - parameter surface includes:
    - algorithm_status
    - claims_complete_native_algorithm=false
  - plugin descriptor/info updated to include "experimental, algorithm incomplete"
- Added focused guardrails for CollectiveReduceNodeMetal:
  - tests enforce runtime unsupported behavior with default collective capability
  - tests enforce plugin descriptor includes runtime unsupported labeling
- Added guardrail tests distinguishing acceptable non-kernel Metal node classes from algorithm claims.
- Inventory explicitly marks PR6 gate as blocked pending truthful algorithm status.

## Files Changed

- docs/sar/metal_node_truth_in_labeling.md (new)
- examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp
- examples/SAR/src/CrsdFocusedImageTransformMetal.cpp
- examples/SAR/plugins/crsd_focused_image_transform_metal_node_plugin.cpp
- examples/SAR/test/test_metal_truth_in_labeling_guardrails.cpp (new)
- examples/SAR/test/CMakeLists.txt
- libgpu/test/unit/test_metal_truth_in_labeling_guardrails.cpp (new)
- libgpu/test/CMakeLists.txt

## Files Deleted

- None

## Tests Added

SAR:

- MetalTruthInLabelingGuardrailTest.InventoryDocumentClassifiesAllActiveMetalNodesAndBlocksPr6
- MetalTruthInLabelingGuardrailTest.FocusedImageMetalNodeReportsExperimentalIncompleteStatus
- MetalTruthInLabelingGuardrailTest.FocusedImageMetalPluginDescriptorDeclaresExperimentalIncomplete

libgpu:

- MetalTruthInLabelingGuardrailGpuTest.CollectiveReduceIsUnsupportedWithDefaultMetalCapability
- MetalTruthInLabelingGuardrailGpuTest.CollectiveReducePluginInfoDeclaresRuntimeUnsupported
- MetalTruthInLabelingGuardrailGpuTest.InventoryStatesNonKernelNodeClassesAreValidMetalNodes

## Tests Removed

- None

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgpu_stub_unit test_sar_example_unit

./build-ninja/ninja-debug-metal-native/libgpu/test/test_libgpu_stub_unit \
  --gtest_filter='MetalTruthInLabelingGuardrailGpuTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='MetalTruthInLabelingGuardrailTest.*:CrsdFocusedImageMetalTest.*'
```

## Validation Results

- MetalTruthInLabelingGuardrailGpuTest.*: 3/3 passed
- MetalTruthInLabelingGuardrailTest.*: 3/3 passed
- CrsdFocusedImageMetalTest.* regression in same run: 8/8 passed

## Constraints Compliance

- No real focused-image native Metal algorithm was implemented in PR5c.
- No PR6 artifact persistence work was started.
- No node renames were performed.
- Transfer/memory/sync node validity without kernels was preserved and tested.
- No real GOTCHA dependency, MATLAB dependency, SarPy runtime dependency, or dataset dependency added.

## Remaining Follow-Up Work

- PR5c verifier pass.
- Keep PR6 blocked until any experimental/unsupported Metal algorithm claims are explicitly upgraded with real implementation or retained as downgraded status.
