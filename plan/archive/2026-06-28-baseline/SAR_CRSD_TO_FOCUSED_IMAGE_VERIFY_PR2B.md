# SAR CRSD To Focused Image VERIFIER Report - PR2b

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: corrective PR2b from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Binary CRSD product reader support for OrderedCrsdSetInputSourceNode

## Verdict

PR2b verification status: PASS

## Required Check Results

1. CrsdReader detects binary CRSD product files and no longer tries to parse product.crsd as JSON.
- Status: PASS
- Evidence:
  - ParseOneCrsd probes file bytes and dispatches CRSD signature prefix to binary parser.
  - Binary parser validates CRSD header prefix and parses XML/PVP/signal blocks.
  - Non-CRSD files produce deterministic unsupported_non_crsd_file diagnostics.

2. CI tests use actual generated binary CRSD product files as the principal CRSD reader fixture.
- Status: PASS
- Evidence:
  - Focused tests and compile definitions point to examples/SAR/test/fixtures/crsd_binary_tiny_multisegment and examples/SAR/test/fixtures/crsd_binary_tiny_multisegment_directory.
  - Principal fixture entries are product.crsd files verified as binary data by file inspection.

3. Any retained JSON tiny fixture support is explicitly labeled as a test helper and is not described as CRSD binary support.
- Status: PASS
- Evidence:
  - Reader constant uses explicit helper schema name graphx.sar.crsd.tiny.v1.
  - Successful helper parse diagnostic is labeled ok:test_helper_json.
  - Binary parse path is distinct and labeled ok:binary_crsd.

4. CrsdReader tests prove binary product.crsd metadata, signal dimensions, required PVP subset, first/last vectors, geometry metadata, and stable signal checksums are read from product.crsd.
- Status: PASS
- Evidence:
  - CrsdReaderTest.BinaryPathsModeParsesMetadataSignalPvpGeometryAndHashes validates vector count, samples per vector, first/last vector fields, geometry fields, and payload/vector hashes.
  - CrsdReaderTest.DirectoryAndManifestModesResolveBinaryOrderedSetDeterministically validates deterministic totals and ordered-set hash equality across modes.

5. OrderedCrsdSetInputSourceNode path, manifest, and directory modes work with binary product.crsd files.
- Status: PASS
- Evidence:
  - CrsdReader mode tests pass for paths, directory, and manifest.
  - OrderedCrsdSetInputSourceNodeTest.AcceptsBinaryDirectoryAndManifestNodeConfigModes passes.
  - OrderedCrsdSetInputSourceNodeTest.JsonTopologySmokeRunsForBinaryPathsDirectoryAndManifestModes passes.

6. Directory mode discovers product.crsd files deterministically and ignores sidecars as authoritative signal/PVP sources.
- Status: PASS
- Evidence:
  - Directory discovery filters to filename product.crsd and sorts paths lexically.
  - Sidecar non-authoritative behavior is validated by CrsdReaderTest.SidecarJsonFilesRemainOptionalAndNonAuthoritativeForBinaryCrsd.

7. Node emits one ordered aperture-set stream for all selected CRSD segments, not one image or independent product per segment.
- Status: PASS
- Evidence:
  - OrderedCrsdSetInputSourceNodeTest.EmitsOneOrderedApertureSetStreamThenEos validates 3 data tokens plus 1 EOS for the 3-segment fixture and proper aggregate sequencing.
  - No focused image transform behavior was introduced in this PR2b scope.

8. Tests prove duplicate, missing, malformed, unsupported, missing required metadata/PVP, and unexpected ordering cases produce deterministic diagnostics.
- Status: PASS
- Evidence:
  - CrsdReaderTest.DeterministicDiagnosticsCoverInvalidAndOrderingCases verifies duplicate_segment_index, missing_segment_index, out_of_order_segment_index, unsupported_non_crsd_file prefix, malformed_crsd prefix, missing_required_pvp or missing_required_metadata prefix, and missing_product_crsd.

9. Sidecar JSON files are optional evidence only; tests fail if metadata.json, pvp.json, chunk_index.json, provenance.json, or SarPy validation JSON are used as substitutes for CRSD signal/PVP.
- Status: PASS
- Evidence:
  - Reader binary path consumes product.crsd signal/PVP directly.
  - Sidecar optionality is explicitly tested with absent sidecars and successful ingest.

10. Optional local data/crsd smoke is gated and documented; CI does not require real GOTCHA data or SarPy.
- Status: PASS
- Evidence:
  - OrderedCrsdSetInputSourceNodeTest.OptionalLocalDataCrsdDirectorySmokeIsGated requires GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 and skips otherwise.
  - Focused CI-safe test run skips this test by default.
  - Gated local run passed when enabled.

11. No aperture assembly, focused image transform, Metal lane, sink, SarPy reference generation, image comparison, MATLAB dependency, real GOTCHA data check-in, large generated CRSD check-in, or GraphX runtime contract change was added.
- Status: PASS
- Evidence:
  - Changed implementation scope is limited to CRSD reader/source-node, test/config wiring, and PR2b reports.
  - No new aperture assembly, focused image, Metal lane, sink contract, SarPy reference lane, or MATLAB/dependency additions were introduced.

## Verification Commands Executed

1. Suggested build command as written:
- cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit graphx_sar_example
- Result: FAIL in this workspace (unknown target graphx_sar_example)

2. Corrected build command for this workspace:
- cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit sar_example
- Result: PASS

3. Suggested focused test command:
- ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter=*CrsdReader*:*OrderedCrsdSetInputSourceNode*
- Result: PASS (8 passed, 1 skipped gated local smoke)

4. Optional local command as written:
- ./build-ninja/ninja-debug-metal-native/examples/SAR/graphx_sar_example --config examples/SAR/config/sar_crsd_real_directory_input_smoke.json
- Result: FAIL in this workspace (binary name/CLI differs)

5. Corrected optional local verification in this workspace:
- GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 ./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter=*OptionalLocalDataCrsdDirectorySmokeIsGated
- ./build-ninja/ninja-debug-metal-native/examples/SAR/sar_example examples/SAR/config/sar_crsd_real_directory_input_smoke.json
- Result: PASS

## Notes

- The verifier checks are satisfied.
- The two optional/suggested command strings in the PR agent prompt are stale for this workspace target/executable naming and CLI style; corrected equivalents are documented above.
