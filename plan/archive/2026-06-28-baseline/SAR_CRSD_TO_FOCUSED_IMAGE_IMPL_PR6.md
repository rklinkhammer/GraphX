# SAR CRSD To Focused Image IMPLEMENTER Report - PR6

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR6 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Deterministic focused-image output sink and artifact contract

## Scope Implemented

Implemented PR6 deterministic focused-image sink artifacts with schema/config/test coverage.

Completed:

- Added `CrsdFocusedImageSinkNode`.
  - Input/output contract: `FocusedImageResult -> FocusedImageResult`.
  - Deterministically persists artifact triplet:
    - binary focused-image payload (`.bin`)
    - JSON artifact metadata (`.json`)
    - convenience image (`.pgm`)
- Added plugin registration and plugin CMake wiring for `CrsdFocusedImageSinkNode`.
- Added artifact schema file for focused-image sink JSON contract:
  - `examples/SAR/tools/sar_focused_image_artifact_schema.json`
- Added focused-image sink topology config:
  - `examples/SAR/config/sar_crsd_tiny_fixture_with_sink.json`
- Extended focused-image transform result metadata (CPU + Metal outputs) so sink can persist required lineage/hash fields:
  - ordered CRSD segment list
  - per-segment input hashes
  - complete-aperture lineage flag
- Added PR6 tests for deterministic binary/JSON/PGM persistence and JSON/binary consistency checks.
- Added CMake test wiring and compile definitions for new sink tests and schema path.

## Files Changed

- examples/SAR/include/sar/CrsdFocusedImageSinkNode.hpp (new)
- examples/SAR/src/CrsdFocusedImageSinkNode.cpp (new)
- examples/SAR/plugins/crsd_focused_image_sink_node_plugin.cpp (new)
- examples/SAR/tools/sar_focused_image_artifact_schema.json (new)
- examples/SAR/config/sar_crsd_tiny_fixture_with_sink.json (new)
- examples/SAR/test/test_crsd_focused_image_sink.cpp (new)
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/test/CMakeLists.txt
- examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp
- examples/SAR/src/CrsdFocusedImageTransformNode.cpp
- examples/SAR/src/CrsdFocusedImageTransformMetal.cpp

## Files Deleted

- None

## Tests Added

- `CrsdFocusedImageSinkNodeTest.WritesDeterministicBinaryJsonAndPgmArtifacts`
- `CrsdFocusedImageSinkNodeTest.ArtifactJsonContainsSchemaContractFields`
- `CrsdFocusedImageSinkNodeTest.JsonAndBinaryArtifactsAreConsistent`

## Tests Removed

- None

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit crsd_focused_image_sink_node

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageSinkNodeTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageTransformNodeTest.*:CrsdFocusedImageSinkNodeTest.*:CrsdFocusedImageMetalTest.*'
```

## Validation Results

- `CrsdFocusedImageSinkNodeTest.*`: 3/3 passed
- `CrsdFocusedImageTransformNodeTest.*`: 16/16 passed
- `CrsdFocusedImageMetalTest.*`: 8/8 passed

## PR6 Required Checks Mapping

1. Sink writes deterministic binary and JSON focused-image artifacts.
- PASS
- Covered by deterministic rerun test comparing `.bin` and `.json` byte/text equality.

2. Sink writes convenience PNG or PGM output.
- PASS
- Implemented deterministic `.pgm` output.

3. Artifact schema preserves shape, spacing, assumptions, hashes, provenance, CPU/Metal lane, ordered CRSD segment list, per-segment input hashes, ordered-set hash, output hash, and complete-aperture lineage.
- PASS
- Implemented schema + JSON emission including required contract fields.
- CPU/Metal execution lane recorded from control sidecar backend metadata.

4. Tests prove deterministic serialization and JSON/binary consistency.
- PASS
- Deterministic serialization validated by repeated artifact equality checks.
- JSON/binary consistency validated via binary payload hash cross-check in JSON.

5. No SarPy reference generation, comparison lane, local real-data workflow, MATLAB dependency, or unrelated algorithm redesign added.
- PASS
- PR6 changes are limited to sink/plugin/schema/config/tests and metadata plumbing from existing transform outputs.

## Constraints Compliance

- No SarPy runtime/reference generation was added.
- No comparison lane work was added.
- No local real-data workflow changes were added.
- No MATLAB dependency was added.
- Focused-image math path was not changed; only output metadata plumbing was added to connect sink contract fields.

## Remaining Follow-Up Work

- PR6 verifier pass against this implementation.
