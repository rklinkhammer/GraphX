# SAR CRSD To Focused Image IMPLEMENTER Report - PR2

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR2 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Ordered CRSD set reader/source-node interface and tiny fixture strategy

## Scope Implemented

Completed exactly PR2 with a narrow ordered-CRSD ingest interface, source-node contract, plugin/CMake wiring, config examples, and deterministic CI-safe fixture tests:

- Added `OrderedCrsdSetInputSourceNode` with node_config modes:
  - `crsd_paths` (explicit ordered list)
  - `crsd_directory` (directory discovery)
  - `manifest_path` (manifest order)
- Added narrow C++ CRSD reader interface/implementation (`ICrsdReader` / `CrsdReader`) for ordered-set ingest.
- Implemented deterministic diagnostics for:
  - duplicate segment index
  - missing segment index
  - out-of-order segment index
  - unsupported CRSD content/fields
- Enforced that `product.crsd` is authoritative for signal/PVP/geometry ingest in this PR2 fixture contract.
- Kept JSON sidecars as optional/non-authoritative (sanity/provenance only) by not requiring any sidecar file for ingest success.
- Added plugin registration and CMake wiring for the new source node.
- Added JSON config examples for all three input modes (paths, directory, manifest).
- Added deterministic tiny multi-segment CRSD fixture strategy and focused input-node contract tests.
- Added plugin-load and JSON-topology smoke tests for the new source node.

Out-of-scope items were not implemented (no aperture assembly, no focused-image transform, no Metal focused-image lane, no sink/artifact redesign, no SarPy reference generation path, no local real-data workflow, no MATLAB dependency).

## Files Changed

- examples/SAR/include/sar/io/CrsdReader.hpp
- examples/SAR/src/io/CrsdReader.cpp
- examples/SAR/include/sar/OrderedCrsdSetInputSourceNode.hpp
- examples/SAR/src/OrderedCrsdSetInputSourceNode.cpp
- examples/SAR/plugins/crsd_input_source_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/test/CMakeLists.txt
- examples/SAR/test/test_crsd_input_source_node.cpp
- examples/SAR/config/sar_crsd_tiny_fixture_set_input.json
- examples/SAR/config/sar_crsd_tiny_fixture_set_input_directory.json
- examples/SAR/config/sar_crsd_tiny_fixture_set_input_manifest.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/manifest.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_000/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_001/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_001/metadata.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_001/pvp.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_001/chunk_index.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_001/provenance.json
- examples/SAR/test/fixtures/crsd_tiny_multisegment/segment_002/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/bad/duplicate_a/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/bad/duplicate_b/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/bad/missing_segment_2/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/bad/out_of_order_first/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment/bad/unsupported_missing_geometry/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment_directory/segment_000/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment_directory/segment_001/product.crsd
- examples/SAR/test/fixtures/crsd_tiny_multisegment_directory/segment_002/product.crsd

## Files Deleted

- None

## Tests Added

- `CrsdReaderTest.OrderedPathsModeParsesSignalPvpGeometryAndHashes`
- `CrsdReaderTest.DirectoryAndManifestModesResolveOrderedSetDeterministically`
- `CrsdReaderTest.SidecarJsonFilesRemainOptionalAndNonAuthoritative`
- `CrsdReaderTest.DuplicateMissingOutOfOrderAndUnsupportedDiagnosticsAreDeterministic`
- `OrderedCrsdSetInputSourceNodeTest.EmitsOneOrderedApertureSetStreamThenEos`
- `OrderedCrsdSetInputSourceNodeTest.AcceptsDirectoryAndManifestNodeConfigModes`
- `OrderedCrsdSetInputSourceNodeTest.DynamicPluginLoadAndInstantiationSmoke`
- `OrderedCrsdSetInputSourceNodeTest.JsonTopologySmokeRunsForPathsDirectoryAndManifestModes`

## Tests Removed

- None

## Build/Test Command

- Build:
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit -j8`
- Focused PR2 test run:
  - `build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'`

Result: PASS (8 tests passed).

## Remaining Follow-Up Work

- PR3: add CRSD aperture assembly adapter and explicit SAR phase-history contract over ordered CRSD-set output.
- PR3+: preserve and verify payload/ordering/checksum continuity across adapter/split/merge boundaries.
- PR4+: add true focused-image transform path and its proof matrix.
