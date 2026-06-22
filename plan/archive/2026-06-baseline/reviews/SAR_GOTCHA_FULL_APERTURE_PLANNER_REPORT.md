> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# SAR GOTCHA Full-Aperture Conversion Planner Report

Date: 2026-06-14

Planner role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## Executive Summary

The current GOTCHA ingestion path reads one selected pulse per MAT file (typically `pulse_index=0`), producing a subset of the available phase-history data. The GOTCHA Large Scene Data consists of ten files (`subData01.mat` through `subData10.mat`) intended to concatenate into a single aperture of approximately one-foot azimuth resolution.

This planner report proposes a sequence of small, reviewable PRs that move the system from partial/subset handling to correct full-aperture conversion while preserving existing architecture boundaries and maintaining CI safety.

**Key Facts:**
- Current GotchaMatReader accepts `pulse_index` and reads only one pulse per file
- GOTCHA dataset documentation specifies `Np` (pulse count), `K` (sample count), `deltaF`, `minF`, `AntX/Y/Z`, `R0`, `phdata`
- Full-aperture path requires ingesting all `Np` pulses from all ten files while preserving metadata fidelity
- `graphx-crsd-lite` is the working intermediate format; standards-compliant CRSD writer is deferred to a later phase
- MATLAB is explicitly NOT used and must remain excluded from the build chain
- Backward compatibility is not required

## Current State Assessment

**What works:**
- Sidecar-based GOTCHA MAT ordering (lexical/manifest modes)
- MAT field inventory inspection (detects Np, K, deltaF, minF, etc.)
- Single-pulse-per-file normalization into a normalized SAR product model
- Deterministic graphx-crsd-lite output with reports and checksums
- Local-only real-data validation gated by environment variables
- SarPy reference/comparison harness (local-only, optional)

**What is missing:**
1. Full-pulse ingestion (currently hardcoded to `pulse_index=0`)
2. Automatic detection and concatenation of multi-file apertures
3. Normalized product validation for pulse count consistency across files
4. Explicit geometry metadata mapping for antenna positions and reference range
5. Frequency-axis metadata preservation in CRSD/lite output
6. Comprehensive tests with multi-file multi-pulse synthetic fixtures
7. Documentation linking dataset description to field mapping
8. Standards-compliant CRSD writer (deferred; lite is current target)

**Blocking conditions:**
- None identified for the full-aperture lite conversion path
- CRSD writer is deferred to a later PR phase (not in scope here)

## Planned PR Sequence

### PR1: Extend GOTCHA Field Inventory Validation

**Title:** Add mandatory GOTCHA field validation for full-aperture support

**Purpose:** Establish explicit field inventory requirements and error reporting so MAT readers and converters fail clearly when required metadata is missing.

**Files to touch:**
- `examples/SAR/include/sar/GotchaMatInspector.hpp` (add field validation methods)
- `examples/SAR/src/GotchaMatInspector.cpp` (implement field checks for Np, K, deltaF, minF, AntX, AntY, AntZ, R0, phdata)
- `examples/SAR/src/graphx_gotcha_to_crsd.cpp` (call validator before reader)
- `docs/sar/gotcha_large_scene_data_description.md` (update to cite as authoritative reference)

**Files to delete:**
- None

**Tests to add:**
- `test_gotcha_field_inventory_validation.cpp`: Verify that sidecar JSON with missing fields is rejected with clear diagnostic
- Test fixture data: synthetic sidecar JSON missing Np, missing phdata, etc.

**Tests to delete:**
- None

**Acceptance criteria:**
1. `GotchaMatInspector::ValidateRequiredFields()` checks for: Np, K, deltaF, minF, AntX, AntY, AntZ, R0, phdata
2. Missing field reports include field name and expected type
3. Validator output can be consumed by CLI to fail before attempting MAT read
4. All nine field checks are unit-tested with synthetic JSON fixtures
5. Documentation cites `docs/sar/gotcha_large_scene_data_description.md` as authoritative

**Risks:**
- Rejecting synthetic fixtures that omit optional metadata (mitigated by explicit fixture design)

**Rollback plan:**
- Remove `ValidateRequiredFields()` call from CLI; revert to current permissive sidecar handling

**CI-safe:** Yes (only adds validation, no reader changes)

---

### PR2: Extend GotchaMatReader to Support Full-Pulse Ingestion

**Title:** Enable full-pulse ingestion for multi-pulse-per-file GOTCHA data

**Purpose:** Remove the hardcoded `pulse_index` limitation so every pulse from every MAT file can be read into the normalized product.

**Files to touch:**
- `examples/SAR/include/sar/GotchaMatReader.hpp` (remove pulse_index parameter from constructor/read)
- `examples/SAR/src/GotchaMatReader.cpp` (iterate all pulses; preserve Np ordering)
- `examples/SAR/include/sar/NormalizedSarProduct.hpp` (verify pulse vector storage can handle N pulses)
- `examples/SAR/src/graphx_gotcha_to_crsd.cpp` (remove pulse_index CLI arg if present)

**Files to delete:**
- None (pulse_index was parameter, not a separate file)

**Tests to add:**
- `test_gotcha_full_pulse_reader.cpp`: Read synthetic sidecar with Np=5, verify all 5 pulses appear in output
- Test fixtures: synthetic.json with multiple pulses per file

**Tests to delete:**
- Any test that depends on pulse_index parameter isolation (likely none; it was internal)

**Acceptance criteria:**
1. `GotchaMatReader::Read()` iterates over all Np pulses per file
2. Pulse ordering is preserved (index 0..Np-1 per file)
3. Multi-pulse read produces one PulseVector per pulse, not one per file
4. Total pulse count in normalized product matches sum of Np across all input files
5. Existing single-pulse tests still pass (backward compatible in output shape)

**Risks:**
- Memory overhead for large apertures (mitigated by testing with modest Np values first)
- Shape mismatch in downstream consumption (mitigated by comprehensive test fixtures)

**Rollback plan:**
- Revert to pulse_index-based iteration; restore hardcoded pulse_index=0

**CI-safe:** Yes (internal reader change, not API)

---

### PR3: Add Multi-File Aperture Ordering and Validation

**Title:** Support deterministic multi-file aperture concatenation and sequence validation

**Purpose:** Ensure the ten GOTCHA files are read in correct order and that pulse sequencing across files is validated.

**Files to touch:**
- `examples/SAR/include/sar/GotchaInputOrdering.hpp` (enhance or extend ordering logic)
- `examples/SAR/src/GotchaInputOrdering.cpp` (add aperture-concatenation rules)
- `examples/SAR/src/graphx_gotcha_to_crsd.cpp` (apply ordering before reading)
- `examples/SAR/test/fixtures/` (add multi-file synthetic fixtures)

**Files to delete:**
- None

**Tests to add:**
- `test_multi_file_aperture_ordering.cpp`: Verify subData01..subData10 are ordered lexically or by manifest
- `test_aperture_pulse_sequence_validation.cpp`: Check for gaps/duplicates/out-of-order pulses across files
- Test fixtures: 3-file synthetic aperture with 10 pulses per file

**Tests to delete:**
- None

**Acceptance criteria:**
1. Aperture ordering is deterministic (lexical order for subData01..10 by default)
2. Manifest mode can override lexical order
3. Pulse sequencing validator detects gaps between files (e.g., file 1 ends at pulse 99, file 2 starts at pulse 101)
4. Out-of-order or duplicate pulses within the aperture are reported with file/pulse indices
5. Valid aperture passes validation without warnings

**Risks:**
- Manifest overrides may be misapplied (mitigated by explicit error messages)

**Rollback plan:**
- Remove aperture concatenation logic; process files independently

**CI-safe:** Yes (validation and ordering, no format changes)

---

### PR4: Update Normalized Product for Full-Aperture Pulse Metadata

**Title:** Extend normalized product model to preserve full-aperture pulse and geometry metadata

**Purpose:** Ensure the normalized product can represent all pulses from all files with consistent metadata.

**Files to touch:**
- `examples/SAR/include/sar/NormalizedSarProduct.hpp` (review PerVectorParameters, PulseVector for multi-file support)
- `examples/SAR/include/sar/SarProductValidator.hpp` (add multi-file validators)
- `examples/SAR/src/SarProductValidator.cpp` (implement pulse count, geometry, frequency consistency checks)

**Files to delete:**
- None

**Tests to add:**
- `test_normalized_product_multi_file_validation.cpp`: Verify pulse counts, geometry consistency, frequency metadata across files
- Synthetic fixture: 2-file aperture with differing antenna positions (should be flagged as inconsistent or documented as per-file)

**Tests to delete:**
- None

**Acceptance criteria:**
1. NormalizedSarProduct can store Np_total pulses (sum of all files)
2. SarProductValidator checks that Np matches actual pulse vector count
3. Geometry validator checks that antenna positions (AntX/Y/Z) are consistent across files (or documents per-file differences)
4. Frequency metadata (K, deltaF, minF) is validated for consistency
5. Validator output distinguishes between critical (conversion-blocking) and informational issues

**Risks:**
- Antenna position inconsistency may be legitimate (e.g., platform motion); validator should report, not block

**Rollback plan:**
- Revert to single-file-only validation

**CI-safe:** Yes (validation layer only)

---

### PR5: Map GOTCHA Metadata to CRSD/Lite Fields

**Title:** Implement frequency, antenna, and geometry metadata mapping for full-aperture conversion

**Purpose:** Ensure K, deltaF, minF, AntX/Y/Z, R0 from GOTCHA are correctly mapped into CRSD/lite metadata.

**Files to touch:**
- `examples/SAR/include/sar/io/GotchaToCrsdMetadataMapper.hpp` (new file for metadata mapping)
- `examples/SAR/src/io/GotchaToCrsdMetadataMapper.cpp` (implement mapping for frequency axis, antenna positions, reference range)
- `examples/SAR/include/sar/io/GraphxCrsdLiteWriter.hpp` (accept frequency_axis, antenna_positions, reference_range)
- `examples/SAR/src/io/GraphxCrsdLiteWriter.cpp` (write frequency and geometry metadata)
- `examples/SAR/test/fixtures/gotcha_mapper_metadata/` (test fixtures with frequency/geometry data)

**Files to delete:**
- None

**Tests to add:**
- `test_gotcha_to_crsd_metadata_mapper.cpp`: Map K, deltaF, minF -> frequency_axis, carrier_hz, bandwidth; map AntX/Y/Z, R0 -> antenna_positions, reference_range
- Synthetic fixture: antenna positions with known values, verify they appear in lite output JSON

**Tests to delete:**
- None

**Acceptance criteria:**
1. Carrier frequency is derived as minF + (K-1) * deltaF / 2
2. Bandwidth is computed as K * deltaF
3. Frequency axis is populated with K sample frequencies: minF + i*deltaF for i=0..K-1
4. Antenna phase-center position [AntX, AntY, AntZ] is written to lite metadata
5. Reference range R0 is written to lite metadata
6. Lite output JSON includes frequency_axis, carrier_hz, bandwidth, antenna_xyz, reference_range_m
7. Round-trip test verifies metadata is preserved in lite output

**Risks:**
- Frequency axis derivation assumes evenly-spaced samples (documented assumption)
- Antenna positions are in local Cartesian frame, not geodetic (documented as "local frame")

**Rollback plan:**
- Skip frequency/geometry metadata in lite output; revert to prior simpler format

**CI-safe:** Yes (metadata mapping, no runtime changes)

---

### PR6: Add Synthetic Multi-File Multi-Pulse Fixtures and Tests

**Title:** Create comprehensive test fixtures for multi-file full-aperture conversion

**Purpose:** Establish CI-safe deterministic test coverage for the full-aperture conversion path without requiring real GOTCHA data.

**Files to touch:**
- `examples/SAR/test/fixtures/gotcha_full_aperture_synthetic/` (new directory)
  - `2file_10pulse_each.json` (synthetic aperture: 2 files, 10 pulses per file)
  - `10file_5pulse_each.json` (synthetic aperture: 10 files simulating real dataset, 5 pulses per file)
  - `manifest.json` (ordering manifest for the 10-file fixture)
  - `checksums.sha256` (deterministic checksums)
- `examples/SAR/test/test_gotcha_full_aperture_integration.cpp` (new test using fixtures)

**Files to delete:**
- None

**Tests to add:**
- `test_gotcha_full_aperture_integration.cpp` test cases:
  - `TwoFileFullApertureReadAndConvertToLite`: read 2-file fixture, verify Np total, convert to lite
  - `TenFileFiveEachApertureReadAndConvertToLite`: read 10-file fixture, verify aperture pulse count and ordering
  - `MetadataPreservationAcrossAperture`: verify frequency/antenna metadata is consistent
- Test helpers: fixture generator for variable Np, variable file count

**Tests to delete:**
- None (old single-pulse tests remain for backward compatibility)

**Acceptance criteria:**
1. 2-file fixture with 10 pulses per file generates normalized product with 20 total pulses
2. 10-file fixture with 5 pulses per file generates normalized product with 50 total pulses
3. Converted lite output contains correct pulse count in metadata
4. Aperture ordering is deterministic (same input produces same output)
5. All tests pass in CI without external data

**Risks:**
- Fixture generation code must be deterministic (mitigated by hardcoded synthetic data)

**Rollback plan:**
- Remove test fixtures and test file

**CI-safe:** Yes (deterministic synthetic fixtures only)

---

### PR7: Update Conversion Report for Full-Aperture Pulse Accounting

**Title:** Enhance conversion report to clearly document subset vs. full-aperture conversion

**Purpose:** Ensure conversion reports are explicit about how many pulses were read and from which files.

**Files to touch:**
- `examples/SAR/include/sar/io/ConversionReport.hpp` (add fields: total_pulses_read, pulses_per_file, subset_mode_enabled)
- `examples/SAR/src/io/ConversionReport.cpp` (populate new fields during conversion)
- `examples/SAR/src/graphx_gotcha_to_crsd.cpp` (emit subset mode status in report)

**Files to delete:**
- None

**Tests to add:**
- `test_conversion_report_pulse_accounting.cpp`: Verify report contains correct pulse counts for multi-file input

**Tests to delete:**
- None

**Acceptance criteria:**
1. Conversion report includes: `total_files_read`, `total_pulses_read`, `pulses_per_file` (array)
2. Report distinguishes between "full-aperture mode" (all pulses) and "subset mode" (selected pulses only)
3. If subset mode is used, report clearly states `pulse_selection_method: "single_index"` or similar
4. Report generation does not fail on multi-file input

**Risks:**
- Report format changes may affect downstream parsing (mitigated by backward-compatible JSON schema versioning)

**Rollback plan:**
- Remove new report fields; revert to simple format

**CI-safe:** Yes (reporting layer only)

---

### PR8: Implement Local-Only Real GOTCHA Multi-File Validation

**Title:** Add gated local-only workflow for real GOTCHA full-aperture validation

**Purpose:** Enable local-only validation against real `/subData` directory without adding CI dependencies.

**Files to touch:**
- `examples/SAR/test/test_gotcha_real_full_aperture_validation.cpp` (new file)
- `examples/SAR/test/CMakeLists.txt` (add conditional compilation for real data tests)
- `scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh` (update to use full-aperture path)
- `docs/sar/gotcha_large_scene_data_description.md` (link to validation instructions)

**Files to delete:**
- None

**Tests to add:**
- `test_gotcha_real_full_aperture_validation.cpp` (gated by GRAPHX_SAR_GOTCHA_DATASET)
  - Test case: `RealGotchaFullApertureReadAndConvert_SkipsIfNoDataset` (skip with message if env var not set)
  - Test case: `RealGotchaFullApertureReadAndConvert_VerifiesAllPulsesWithoutSubset` (requires env var, does full read)
  - Test case: `RealGotchaFullApertureConversionProducesValidLite` (requires env var, verifies lite output)

**Tests to delete:**
- Old local-only single-pulse test (superseded by full-aperture variant)

**Acceptance criteria:**
1. Test is skipped in CI (no GRAPHX_SAR_GOTCHA_DATASET set)
2. Test runs only if user exports GRAPHX_SAR_GOTCHA_DATASET=/path/to/subData
3. Test verifies that all pulses from all 10 files are read and converted
4. Conversion report shows total_pulses_read == sum(Np) for all 10 files
5. Output lite directory contains all converted chunks with correct pulse counts

**Risks:**
- Real data path may have unexpected field names or layouts (mitigated by field validation from PR1)

**Rollback plan:**
- Remove test file and environment gating logic

**CI-safe:** Yes (gated by environment variable; skips in CI)

---

### PR9: Documentation Update for Full-Aperture GOTCHA Conversion

**Title:** Document full-aperture GOTCHA conversion workflow and field mapping

**Purpose:** Establish clear documentation linking GOTCHA dataset description to conversion logic.

**Files to touch:**
- `docs/sar/gotcha_large_scene_data_description.md` (already created; add full-aperture conversion notes)
- `docs/sar/crsd_definition.md` (update GOTCHA field mapping table to include all fields)
- `docs/CONSOLIDATED_OPERATIONS.md` (add section on full-aperture GOTCHA conversion)
- `examples/SAR/README.md` (add link to gotcha_large_scene_data_description.md)

**Files to delete:**
- None

**Tests to add:**
- None (documentation verification is manual)

**Tests to delete:**
- None

**Acceptance criteria:**
1. `gotcha_large_scene_data_description.md` documents Np, K, deltaF, minF, AntX/Y/Z, R0, phdata fields
2. CRSD definition field mapping includes GOTCHA -> normalized -> CRSD paths
3. Full-aperture conversion workflow is documented step-by-step
4. Local-only real-data validation instructions cite environment variables
5. Notes clarify that local Cartesian frame is preserved (not geodetic)

**Risks:**
- None (documentation only)

**Rollback plan:**
- Revert documentation changes

**CI-safe:** Yes (documentation only)

---

## Cross-Cutting Concerns

### Backward Compatibility
- Not required by planning rules
- Existing single-pulse tests should continue to pass (covered by test fixtures)
- Single-pulse mode can be deprecated after full-aperture validation

### MATLAB Dependency
- No MATLAB used anywhere in the PR sequence
- HDF5 remains optional (deferred to future MAT v7.3 reader PR)
- Current sidecar-based approach requires no new dependencies

### CRSD Writer
- Standards-compliant CRSD writer is deferred to a later PR phase
- Current target is graphx-crsd-lite (permanent non-standard format)
- PR sequence does not attempt to implement CRSD writer

### External Baselines
- SarPy harness remains local-only and optional
- No external package dependency is added to core build
- Comparison harnesses are separate from full-aperture ingestion

## Implementation Order and Dependencies

**Recommended PR order:**

1. **PR1 (Field Validation)** → PR2/PR4 depend on it
2. **PR2 (Full-Pulse Reader)** → PR3/PR6 depend on it
3. **PR3 (Multi-File Ordering)** → PR6 depends on it
4. **PR4 (Normalized Product Validation)** → PR5/PR8 depend on it
5. **PR5 (Metadata Mapping)** → PR6/PR7 depend on it
6. **PR6 (Test Fixtures)** → can run after PR2/PR3/PR5
7. **PR7 (Conversion Report)** → can run after PR6
8. **PR8 (Real Data Validation)** → can run after PR6/PR7
9. **PR9 (Documentation)** → can run anytime; best after PR1-PR8

## Risk Summary

| Risk | Mitigation | Severity |
|------|-----------|----------|
| Memory overhead for large apertures | Test with modest Np values (5-10 per file) first | Low |
| Antenna position inconsistency | Validator reports but doesn't block; documents per-file differences | Low |
| Frequency axis derivation assumptions | Documented as evenly-spaced samples assumption | Low |
| Metadata mapping gaps for missing GOTCHA fields | PR1 validation rejects incomplete data; clear error messages | Low |
| Real-data path field name variations | PR1 field inventory check catches variations; clear diagnostic | Medium |
| Downstream consumption shape mismatch | PR4 validator ensures consistency; PR6 comprehensive test fixtures | Low |

## Success Criteria

By the completion of PR9, the GraphX GOTCHA conversion path shall:

1. ✅ Read all Np pulses from each of the ten GOTCHA files
2. ✅ Concatenate files in deterministic order (lexical or manifest)
3. ✅ Validate that all required GOTCHA fields are present
4. ✅ Map frequency, antenna, and geometry metadata correctly into CRSD/lite
5. ✅ Generate conversion reports that document full-aperture status
6. ✅ Support local-only real-data validation (gated by environment variables)
7. ✅ Provide comprehensive CI-safe test fixtures for multi-file multi-pulse scenarios
8. ✅ Maintain CI safety (no external dataset downloads, no MATLAB dependency)
9. ✅ Document the full workflow and field mapping

## Future Work (Out of Scope)

- Standards-compliant CRSD writer (deferred to later PR phase)
- Native GOTCHA MAT v7.3 / HDF5 reader (deferred; sidecar-based approach is current)
- Polarization and antenna-pattern metadata handling (not provided by current dataset)
- Geodetic scene-center and absolute collection time mapping (requires external reference data)
- Substitution experiments with external SAR packages (separate from full-aperture ingestion)

## Conclusion

This planner report proposes a low-risk, modular sequence of nine PRs that extend the GOTCHA conversion system from single-pulse-per-file handling to correct full-aperture conversion while maintaining CI safety, preserving existing architecture, and deferring CRSD writer work to a later phase.

Each PR is small, focuses on one architectural concern, and includes comprehensive tests with synthetic fixtures. The sequence respects the planning rules: no MATLAB, no backward-compatibility burden, no complexity accumulation, and explicit validation over silent assumptions.

Total estimated effort: ~40-60 hours of implementation, test writing, and documentation (high uncertainty; actual implementation will refine estimates).
