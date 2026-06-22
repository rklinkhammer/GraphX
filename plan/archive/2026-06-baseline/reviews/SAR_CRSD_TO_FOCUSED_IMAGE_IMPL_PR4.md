# SAR CRSD To Focused Image IMPLEMENTER Report - PR4

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR4 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Focused image formation path in GraphX

## Scope Implemented

Implemented PR4 by adding CrsdFocusedImageTransformNode, plugin, CMake wiring, config, and focused tests that cover the entire proof matrix.

Completed:

- Added `FocusedImageGrid` and `FocusedImageResult` message types in examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp.
- Added `CrsdFocusedImageTransformNode` (interior node: `SarPhaseHistoryControlMessage` → `FocusedImageResult`):
  - Derives 2D near-field backprojection geometry from CRSD phase-history frame fields.
  - Uses existing `SarCpuReference::BackprojectNearestRange` for deterministic CPU computation.
  - Geometry assumptions documented inline and in header.
  - Computes one focused image from the full-aperture phase history (not per segment).
  - Rejects empty/zero-vector payloads (diagnostics-only guardrail).
  - Rejects non-EOS data-marker inputs (quick-look/per-segment-output guardrail).
  - Preserves `SarAccelControlToken` in output.
  - Explicit output: `pixels` (float32 row-major), `output_hash`, `input_ordered_set_hash`, `grid` (shape/spacing/geometry).
- Added plugin: examples/SAR/plugins/crsd_focused_image_transform_node_plugin.cpp.
- Added CMake target `crsd_focused_image_transform_node` in plugins/CMakeLists.txt.
- Wired into test CMakeLists: source file, plugin dependency, new config macro.
- Added config: examples/SAR/config/sar_crsd_focused_image_tiny_fixture.json.

Geometry assumptions (explicit, in header):

- Single channel/polarization.
- Platform trajectory from per-vector `platform_position_m[0]` (X), mean of `[1]` (Y).
- `range_spacing_m = c / (2 * sample_rate_hz)`.
- `range_origin_m = 0.0`.
- `wavelength_m = c / carrier_hz` (default 0.03 m if carrier_hz is zero).
- `pixel_spacing_m = range_spacing_m` (auto-derived, overridable).
- Output dtype: float32 magnitude, row-major, shape [height × width].

Reuse: existing `sar::reference::BackprojectNearestRange` from `SarCpuReference.hpp` — no duplicate algorithm written.

Out-of-scope items were not implemented:

- No Metal execution.
- No sink artifacts.
- No SarPy reference generation.
- No local real-data workflow.
- No MATLAB dependency.

## Files Changed

- examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp (new)
- examples/SAR/src/CrsdFocusedImageTransformNode.cpp (new)
- examples/SAR/plugins/crsd_focused_image_transform_node_plugin.cpp (new)
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/test/test_crsd_focused_image_transform_node.cpp (new)
- examples/SAR/test/CMakeLists.txt
- examples/SAR/config/sar_crsd_focused_image_tiny_fixture.json (new)

## Files Deleted

- None

## Tests Added

16 tests in CrsdFocusedImageTransformNodeTest:

- DefaultConstructionAndConfigureAccepted
- ZeroWidthOrHeightRejectedAtConfigure
- AllZeroInputProducesNearZeroImageNotFakeOutput
- CoherentMultiSegmentProducesFiniteNonzeroPeak
- OutputGridAndPayloadMetadataAreExplicit
- IdenticalInputsProduceDeterministicOutputHash
- OneSamplePerturbationChangesOutputHash
- PlatformPositionPerturbationChangesOutput
- SarAccelControlTokenPreservedInOutput
- NonEosDataMarkerProducesNullopt
- EmptyPayloadFrameProducesNullopt
- OutputDependsOnPvpGeometryNotJustMagnitude
- OneImageProducedFromFullApertureNotPerSegment
- PartitionSchemeHashSurvivesToTransformOutput
- TinyFixturePipelineProducesFiniteNonzeroPeak
- DynamicPluginLoadAndInstantiationSmoke

## Tests Removed

- None

## Build/Test Commands

- Build:
  - cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
- Focused validation:
  - ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdFocusedImageTransformNodeTest.*'
- Regression:
  - ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*:CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'

Results:

- CrsdFocusedImageTransformNodeTest: 16 passed
- Regression: 31 passed, 3 skipped (gated local smoke)

## Remaining Follow-Up Work

- PR4 verifier pass.
- PR5: Metal focused-image execution lane using same SarPhaseHistoryControlMessage contract.
- PR6: CrsdFocusedImageSinkNode consuming FocusedImageResult.
