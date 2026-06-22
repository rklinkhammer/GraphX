# SAR CRSD To Focused Image IMPLEMENTER Report - PR3

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR3 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: CRSD aperture assembly to SAR phase-history adapter/model

## Scope Implemented

Implemented PR3 by adding a CRSD aperture assembly adapter and explicit SAR phase-history model contracts, then validating ordering/accounting/diagnostics behavior with focused tests.

Completed:

- Added new SAR phase-history message/model contract types in examples/SAR/include/sar/SarPhaseHistoryModel.hpp, including:
  - ownership mode
  - control marker
  - sample format
  - explicit buffer layout (shape/stride)
  - per-vector CRSD sample/PVP/geometry payload
  - per-segment and full-aperture checksum boundaries
- Added CrsdApertureAssemblyAdapterNode with plugin registration:
  - node reads ordered CRSD set through CrsdReader
  - validates segment ordering and contiguity
  - validates sample/channel/frequency consistency across segments (samples_per_vector, carrier_hz, sample_rate_hz)
  - enforces deterministic diagnostics for gaps/duplicates/unexpected ordering
  - supports optional sidecar pulse-range cross-check (disabled by default)
  - emits one assembled SarPhaseHistoryControlMessage on EOS for full-aperture downstream consumption
- Extended CRSD reader model to include full per-segment vectors so adapter can materialize a full phase-history frame.
- Added focused PR3 tests for:
  - segment ordering and full-aperture accounting
  - metadata/PVP mapping into phase-history model
  - EOS/control-marker propagation
  - payload ownership/layout contracts
  - deterministic split/merge boundary checksums
  - deterministic diagnostics for gaps/duplicates/unexpected ordering
  - optional sidecar pulse-range cross-check behavior

Out-of-scope items were not implemented:

- No focused-image transform
- No Metal lane changes
- No sink changes
- No SarPy reference generation
- No local real-data workflow changes
- No MATLAB dependency changes

## Files Changed

- examples/SAR/include/sar/io/CrsdReader.hpp
- examples/SAR/src/io/CrsdReader.cpp
- examples/SAR/include/sar/SarPhaseHistoryModel.hpp
- examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp
- examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp
- examples/SAR/plugins/crsd_aperture_assembly_adapter_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/test/test_crsd_aperture_assembly_adapter_node.cpp
- examples/SAR/test/CMakeLists.txt

## Files Deleted

- None

## Tests Added

- CrsdApertureAssemblyAdapterNodeTest.AssemblesFullApertureFrameOnEndOfStream
- CrsdApertureAssemblyAdapterNodeTest.DetectsOutOfOrderAndMissingSegmentDiagnostics
- CrsdApertureAssemblyAdapterNodeTest.DetectsDuplicateAndUnexpectedSegments
- CrsdApertureAssemblyAdapterNodeTest.OptionalSidecarPulseRangeCrossCheckIsConfigurable
- CrsdApertureAssemblyAdapterNodeTest.EnforcesSampleAndFrequencyConsistencyAtConfigure
- CrsdApertureAssemblyAdapterNodeTest.DynamicPluginLoadAndInstantiationSmoke

## Tests Removed

- None

## Build/Test Commands

- Build:
  - cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
- Focused validation:
  - ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*:CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'

Result:

- PASS (14 passed, 1 skipped gated local smoke)

## Remaining Follow-Up Work

- PR3 verifier pass/report.
- PR4 implementation to consume SarPhaseHistoryControlMessage in focused-image transform path.
