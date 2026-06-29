# SAR CRSD To Focused Image VERIFIER Report - PR2

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR2 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Ordered CRSD set reader/source-node interface and tiny fixture strategy

## Verdict

PR2 verification status: PASS

## Required Check Results

1. OrderedCrsdSetInputSourceNode accepts ordered CRSD paths, CRSD directory, and manifest via node_config.
- Status: PASS
- Evidence:
  - Node config contract includes `crsd_paths`, `crsd_directory`, and `manifest_path`.
  - Configure() enforces exactly one mode and maps each mode into reader options.
  - Config examples exist for all 3 modes:
    - examples/SAR/config/sar_crsd_tiny_fixture_set_input.json
    - examples/SAR/config/sar_crsd_tiny_fixture_set_input_directory.json
    - examples/SAR/config/sar_crsd_tiny_fixture_set_input_manifest.json
  - Tests: `OrderedCrsdSetInputSourceNodeTest.AcceptsDirectoryAndManifestNodeConfigModes`, `OrderedCrsdSetInputSourceNodeTest.JsonTopologySmokeRunsForPathsDirectoryAndManifestModes`.

2. CrsdReader reads CRSD metadata, signal, and required PVP subset from product.crsd.
- Status: PASS
- Evidence:
  - `CrsdReader` parses `product.crsd` schema, segment index, carrier/sample-rate metadata, vector timing (`rcv_time_s`), geometry (`platform_position_m`, `platform_velocity_mps`), and complex signal samples.
  - Reader materializes first/last vector records, per-segment hashes, and ordered-set hash.
  - Test: `CrsdReaderTest.OrderedPathsModeParsesSignalPvpGeometryAndHashes` validates parsed metadata/signal/geometry and hash fields.

3. Tests prove deterministic segment order, per-segment shape, total vector count, payload checksums, first/last PVP, and first/last geometry metadata.
- Status: PASS
- Evidence:
  - Deterministic order and total vector count covered in `CrsdReaderTest.OrderedPathsModeParsesSignalPvpGeometryAndHashes` and `CrsdReaderTest.DirectoryAndManifestModesResolveOrderedSetDeterministically`.
  - Per-segment shape (`samples_per_vector`, vector counts) and total vectors (`6`) asserted.
  - Payload checksum/hash fields (`payload_hash`, ordered-set hash) asserted nonzero and deterministic across modes.
  - First/last vector timing and geometry fields explicitly asserted.

4. Tests prove sidecar JSON files are optional and not authoritative for signal/PVP.
- Status: PASS
- Evidence:
  - Reader only consumes `product.crsd` and does not require sidecars.
  - Test `CrsdReaderTest.SidecarJsonFilesRemainOptionalAndNonAuthoritative` verifies successful ingest with missing sidecars and preserved vector/hash results.

5. Duplicate, missing, out-of-order, and unsupported-field cases produce deterministic diagnostics.
- Status: PASS
- Evidence:
  - Deterministic diagnostics emitted by reader for:
    - `duplicate_segment_index:<n>`
    - `missing_segment_index:<n>`
    - `out_of_order_segment_index:<n>`
    - `unsupported_crsd:*` (including geometry)
  - Test `CrsdReaderTest.DuplicateMissingOutOfOrderAndUnsupportedDiagnosticsAreDeterministic` asserts exact/prefix diagnostics.

6. Node emits one ordered aperture-set stream, not one focused image per segment.
- Status: PASS
- Evidence:
  - Source node emits `SarAccelControlToken` data frames per CRSD segment followed by one EOS marker.
  - No focused-image transform or sink-image generation is implemented in PR2 node code.
  - Test `OrderedCrsdSetInputSourceNodeTest.EmitsOneOrderedApertureSetStreamThenEos` verifies 3 data tokens + EOS and ordered aperture-set sidecar accounting.

7. Plugin load and JSON topology smoke tests exist.
- Status: PASS
- Evidence:
  - Plugin implementation exists: `examples/SAR/plugins/crsd_input_source_node_plugin.cpp`.
  - Plugin CMake wiring exists in `examples/SAR/plugins/CMakeLists.txt`.
  - Tests:
    - `OrderedCrsdSetInputSourceNodeTest.DynamicPluginLoadAndInstantiationSmoke`
    - `OrderedCrsdSetInputSourceNodeTest.JsonTopologySmokeRunsForPathsDirectoryAndManifestModes`

8. No aperture assembly, focused image transform, Metal, sink, SarPy reference lane, real-data workflow, MATLAB, or dependency work was added.
- Status: PASS
- Evidence:
  - Changed files are scoped to CRSD reader/source node, plugin wiring, config examples, fixtures, and unit tests.
  - No new aperture assembly node, focused-image transform node, Metal focused-image path, SarPy reference lane, MATLAB dependency, or real-data workflow files were introduced.
  - Existing sink node is reused only in smoke topology/tests; no sink implementation or sink contract redesign was added.
  - No new dependency package additions were introduced in PR2 scope.

## Validation Commands Executed

- `build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'`
- Result: PASS (8/8 tests).

## Summary

PR2 meets all required verifier checks. The implementation introduces a bounded ordered-CRSD ingest contract (reader + source node), deterministic tiny-fixture coverage, and plugin/topology integration, while staying within PR2 scope and preserving the one-ordered-aperture-set semantics.
