> ARCHIVAL STATUS (2026-06-15): This document is kept for historical traceability and includes obsolete planner assumptions (including graphx-crsd-lite behavior). Do not use for active planning; use plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md and current CRSD-only docs/scripts.

# SAR Planner Report

Source inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
- Attached GOTCHA `.mat` to CRSD planning prompt

Scope: PR plan only. No implementation.

Planning rules:
- Repository discovery is mandatory PR1. No implementation PR may precede it.
- MATLAB is never a build, runtime, or test dependency.
- GOTCHA is an importer. CRSD, CPHD, SICD, and `graphx-crsd-lite` are writers/export targets.
- `graphx-crsd-lite` is a permanent non-standard GraphX intermediate representation, not a temporary debug format.
- SarPy is validation/comparison infrastructure only and must not shape GraphX runtime contracts.
- Do not combine architecture cleanup, repository discovery, MAT inspection, normalization, lite writing, CRSD writing, SarPy validation, image formation, or comparison tooling in one PR.

## PR1: Repository Discovery For GOTCHA To CRSD

Purpose:
Create the mandatory discovery report before implementation. Identify where the conversion utility belongs and what dependency gaps exist.

Files to touch:
- `docs/sar/gotcha_crsd_repo_discovery.md`

Files to delete:
- None.

Tests to add:
- None, unless the repo already has documentation existence checks.

Tests to delete:
- None.

Acceptance criteria:
- Report documents build system placement, CLI conventions, test framework, fixture conventions, dependency policy, HDF5 availability, classic MAT reader gaps, existing SAR abstractions, and proposed files.
- Report explicitly states MATLAB is not used.
- Report identifies whether existing CRSD writer support exists in the repository.

Risks:
- Discovery may find dependency gaps that require later plan adjustment.

Rollback plan:
- Revert the discovery document.

CI-safe or local-only:
- CI-safe.

## PR2: CRSD Definition And Mapping Document

Purpose:
Document the CRSD target, the permanent lite format boundary, and mappings before adding code.

Files to touch:
- `docs/sar/crsd_definition.md`

Files to delete:
- None.

Tests to add:
- Optional documentation smoke test if existing doc tests support it.

Tests to delete:
- None.

Acceptance criteria:
- Document covers CRSD overview, CRSD -> CPHD -> SICD placement, collection metadata, timing, channel metadata, PVP, support arrays, signal arrays, geometry, and antenna concepts.
- Document maps GOTCHA -> GraphX normalized model -> CRSD.
- Document defines `gotcha_crsd_index.json` and `conversion_report.json` schemas.
- Document states MATLAB is not used.
- Document states GOTCHA is compensated phase history and initial output may be single-channel pseudo-CRSD.

Risks:
- Some field mappings may remain provisional without real GOTCHA data.

Rollback plan:
- Revert the document.

CI-safe or local-only:
- CI-safe.

## PR3: Clean SAR GPU Naming Before New Utility Work

Purpose:
Remove current SAR GPU naming ambiguity so new conversion work does not build on compatibility aliases.

Files to touch:
- `examples/SAR/config/sar_stripmap_definitive.json`
- SAR plugin registration files for H2D/D2H/backprojection
- SAR tests that reference config-facing aliases
- SAR README references to old node names

Files to delete:
- `examples/SAR/include/sar/H2DAsyncNode.hpp`
- `examples/SAR/include/sar/D2HAsyncNode.hpp`
- `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`

Tests to add:
- JSON/runtime test proving definitive config uses explicit SAR-token GPU node names.
- Resolver diagnostic test proving generic H2D/D2H intent is not redefined as `SarAccelControlToken`.

Tests to delete:
- Tests whose only purpose is preserving old alias names.

Acceptance criteria:
- Definitive SAR config builds and runs through `examples/SAR/main.cpp`.
- No maintained SAR config depends on H2D/D2H/backprojection compatibility aliases.
- Existing accel-token guardrails still pass.

Risks:
- Downstream local configs may break; backward compatibility is intentionally not required.

Rollback plan:
- Revert PR3 only.

CI-safe or local-only:
- CI-safe.

## PR4: Stabilize External Baseline Planning Artifacts

Purpose:
Resolve the current broken test/planning surface where tests reference missing files under `plan/reviews`.

Files to touch:
- `examples/SAR/test/CMakeLists.txt`
- `examples/SAR/test/test_external_baseline_policy_registry.cpp`
- `examples/SAR/test/test_rrp7_sarpy_harness.cpp`
- Optional: `plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md`
- Optional: `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json`

Files to delete:
- Tests that only assert missing planner artifacts exist, if those files are not source artifacts.

Tests to add:
- Focused test proving external baseline tooling remains local-only and outside `libgraph`/`libgpu`.

Tests to delete:
- Missing-artifact existence tests if not replaced by real checked-in artifacts.

Acceptance criteria:
- Full SAR unit target no longer depends on absent files.
- External comparison boundaries remain documented or explicitly tested.

Risks:
- Removing tests may reduce policy visibility if the missing files were accidental omissions.

Rollback plan:
- Revert and restore checked-in policy/registry files.

CI-safe or local-only:
- CI-safe.

## PR5: C++ Normalized SAR Product Model And Interfaces

Purpose:
Add the internal format-independent model and reader/writer interfaces. This is the architectural foundation for GOTCHA import and future exporters.

Files to touch:
- New SAR utility headers/sources in the location identified by PR1
- CMake files for the utility library/test target

Files to delete:
- None.

Tests to add:
- Product shape tests for `signal[pulse][channel][sample]`.
- Metadata propagation tests.
- Pulse/channel/sample indexing tests.
- Required-field validation tests.

Tests to delete:
- None.

Acceptance criteria:
- Namespace `graphx::sar` or the repo-approved equivalent exists.
- Core types exist: `ComplexSample`, `PulseVector`, `ChannelSignal`, `WaveformMetadata`, `PlatformState`, `PerVectorParameters`, `CollectionMetadata`, `ReferenceGeometry`, `NormalizedSarProduct`.
- Interfaces exist: `ISarReader`, `ISarWriter`.
- No GOTCHA field names leak into the generic normalized model.

Risks:
- Model could overfit guessed fields before real MAT inspection.

Rollback plan:
- Revert the utility target and tests.

CI-safe or local-only:
- CI-safe.

## PR6: Product Validator

Purpose:
Add a reusable C++ validator before writing importers or exporters.

Files to touch:
- `SarProductValidator` source/header
- Validator tests

Files to delete:
- None.

Tests to add:
- NaN/Inf checks.
- Metadata completeness checks.
- Pulse ordering checks.
- Shape consistency checks.
- Sample type checks.

Tests to delete:
- None.

Acceptance criteria:
- Validator can be reused by `GotchaMatReader`, `GraphxCrsdLiteReader`, `GraphxCrsdLiteWriter`, and `CrsdWriter`.
- Validation errors are deterministic and actionable.

Risks:
- Required metadata policy may need adjustment as real fields are discovered.

Rollback plan:
- Revert validator files and tests.

CI-safe or local-only:
- CI-safe.

## PR7: Deterministic GOTCHA Input Ordering

Purpose:
Implement ordered file discovery and manifest ordering without parsing MAT contents.

Files to touch:
- GOTCHA importer utility files
- Manifest schema docs/tests

Files to delete:
- None.

Tests to add:
- Lexical ordering.
- Manifest ordering.
- Missing manifest file.
- Duplicate manifest entry.
- Empty input directory.

Tests to delete:
- None.

Acceptance criteria:
- Ordered input file list is deterministic.
- Manifest and lexical sort modes are covered.
- No MATLAB or MAT parser dependency is introduced.

Risks:
- Manifest schema may require later expansion.

Rollback plan:
- Revert ordering module and tests.

CI-safe or local-only:
- CI-safe.

## PR8: C++ MAT Inspection Phase

Purpose:
Understand GOTCHA `.mat` files before normalization. Add inspection output only.

Files to touch:
- C++ MAT inspection source/header
- HDF5 dependency discovery from PR1, if available
- Tiny generated MAT/HDF5 fixture support

Files to delete:
- None.

Tests to add:
- MATLAB v7.3/HDF5 detection on tiny fixture.
- Classic/non-HDF5 deterministic unsupported-format error when no classic MAT reader is integrated.
- `field_inventory.json` shape check.
- `conversion_assumptions.json` shape check.
- No-MATLAB dependency check by build/runtime behavior.

Tests to delete:
- None.

Acceptance criteria:
- Inspector emits keys, shapes, and dtypes.
- No normalized product is emitted.
- No CRSD or lite output is emitted.
- MATLAB is not required.

Risks:
- HDF5 availability may vary by environment.
- Synthetic fixture may not represent real GOTCHA fields.

Rollback plan:
- Revert MAT inspection code and tests.

CI-safe or local-only:
- CI-safe if fixtures are generated locally without downloads.

## PR9: GotchaMatReader Normalization

Purpose:
Implement `GotchaMatReader` that imports ordered GOTCHA MAT files into the normalized model.

Files to touch:
- `GotchaMatReader` source/header
- Normalization tests and fixtures

Files to delete:
- None.

Tests to add:
- IQ sample extraction.
- Frequency axis extraction when present.
- Platform position extraction.
- Pulse time extraction or synthetic pulse index fallback.
- Polarization extraction when present.
- Original field-name diagnostics.
- Provenance and source order preservation.

Tests to delete:
- None.

Acceptance criteria:
- Reader accepts ordered directory or manifest.
- Reader outputs required normalized metadata: collection ID, source files, pulse count, channel count, samples per pulse, sample type, center frequency, bandwidth, frequency vector, positions, optional velocity/time/polarization.
- Initial channel count of one is supported and labeled as derived from GOTCHA phase history.
- GOTCHA-specific field names stay inside importer diagnostics and docs.

Risks:
- Real GOTCHA fields may differ from synthetic fixtures.

Rollback plan:
- Revert reader implementation while keeping model/docs.

CI-safe or local-only:
- CI-safe with synthetic tiny fixtures; real GOTCHA remains local-only.

## PR10: Permanent graphx-crsd-lite Format

Purpose:
Implement permanent non-standard GraphX intermediate representation with round-trip support.

Files to touch:
- `GraphxCrsdLiteWriter`
- `GraphxCrsdLiteReader`
- Lite format docs/schema, if separate from `crsd_definition.md`
- Tests

Files to delete:
- None.

Tests to add:
- Write/read round trip.
- Checksum verification.
- Metadata propagation.
- Provenance preservation.
- Pulse ordering preservation.
- Assumptions preservation.
- Explicit `NON-STANDARD` labeling.

Tests to delete:
- None.

Acceptance criteria:
- Outputs include `signal.bin`, `metadata.json`, `index.json`, and `conversion_report.json`.
- Lite reader reconstructs a normalized product suitable for validation/tests.
- Lite format is not labeled as CRSD and is not treated as temporary.

Risks:
- Lite format may expand as CRSD requirements become clearer.

Rollback plan:
- Revert lite reader/writer and tests.

CI-safe or local-only:
- CI-safe.

## PR11: Chunking, Reports, And Checksums

Purpose:
Add chunk splitting and reporting shared by lite and future CRSD writers.

Files to touch:
- `SarProductChunker`
- Report/index generation code
- Checksum utilities
- Tests

Files to delete:
- None.

Tests to add:
- Chunk splitting by maximum output size.
- Never split pulses.
- Deterministic output names.
- `gotcha_crsd_index.json` schema.
- `conversion_report.json` schema.
- `conversion_warnings.log` behavior.

Tests to delete:
- None.

Acceptance criteria:
- Reports include provenance, assumptions, checksums, warnings, pulse ranges, sample shape, channel count, frequency metadata, and validation status.
- Pulse range semantics are documented and tested.

Risks:
- Byte-size estimation may differ between lite and final CRSD.

Rollback plan:
- Revert chunk/report utilities and tests.

CI-safe or local-only:
- CI-safe.

## PR12: CLI Skeleton For graphx-crsd-lite

Purpose:
Create `graphx-gotcha-to-crsd` and wire MAT import, validation, chunking, lite writing, and reports. CRSD mode must not fake compliance.

Files to touch:
- CLI source
- CMake target
- CLI tests

Files to delete:
- None.

Tests to add:
- `--help`.
- Invalid input directory.
- Empty input directory.
- Malformed manifest.
- Unsupported MAT format.
- `--mode graphx-crsd-lite` end-to-end on tiny fixture.
- `--mode crsd` clear failure until full CRSD writer exists.

Tests to delete:
- None.

Acceptance criteria:
- Required arguments are supported: `--input-dir`, `--output-dir`, `--collection-id`, `--max-output-size-mb`, `--sort`, `--manifest`, `--mode`, `--validate`, `--emit-index`.
- Errors are deterministic.
- Lite mode produces deterministic outputs and reports.
- CRSD mode either succeeds only when valid CRSD writer exists or fails clearly.

Risks:
- CLI conventions may need adjustment based on PR1 discovery.

Rollback plan:
- Revert CLI target and tests.

CI-safe or local-only:
- CI-safe.

## PR13: Python GOTCHA Reference Image And Comparison Tools

Purpose:
Add local/reference Python tooling outside GraphX runtime.

Files to touch:
- `tools/sarpy/reference_image_from_gotcha.py`
- `tools/sarpy/compare_images.py`
- `tools/sarpy/requirements.txt`
- Python tests or local-only test wrappers

Files to delete:
- None.

Tests to add:
- Field discovery on tiny fixture.
- Reference image generation on tiny fixture.
- Metrics test for deterministic arrays.

Tests to delete:
- None.

Acceptance criteria:
- Python tools may use `scipy.io.loadmat`, `h5py`, `numpy`, and `matplotlib`.
- Outputs include `reference_image.npy`, `reference_magnitude.png`, `reference_metadata.json`, `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png`.
- Tools are documented as comparison infrastructure, not runtime dependencies.

Risks:
- Python dependencies may not be available in CI.

Rollback plan:
- Revert Python tools and tests.

CI-safe or local-only:
- Local-only unless dependencies are already available and tests are gated.

## PR14: SarPy CRSD Validation Harness

Purpose:
Add SarPy validation tooling before full CRSD writing.

Files to touch:
- `tools/sarpy/validate_crsd.py`
- `tools/sarpy/reference_image_from_crsd.py`
- `tools/sarpy/requirements.txt`
- Local-only tests

Files to delete:
- None.

Tests to add:
- SarPy environment probe.
- Invalid CRSD path report.
- Optional tiny valid CRSD smoke test if fixture/dependency exists.

Tests to delete:
- None.

Acceptance criteria:
- Validation script reports CRSD version, dimensions, dtype, sample slices, and PVP arrays when SarPy can open the file.
- Emits JSON validation report.
- Harness is optional/local-only unless SarPy is available in CI.

Risks:
- SarPy API differences by version.

Rollback plan:
- Revert harness scripts and tests.

CI-safe or local-only:
- Local-only by default.

## PR15: Full CRSD Writer

Purpose:
Implement standards-targeted CRSD output only after lite, validator, reports, CLI, and SarPy harness exist.

Files to touch:
- `CrsdWriter`
- CRSD metadata assembly
- CLI mode handling
- SarPy validation integration hooks
- CRSD tests

Files to delete:
- None.

Tests to add:
- CRSD writer smoke test.
- SarPy open/read validation on generated tiny output when available.
- Failure when metadata is insufficient.
- Chunked CRSD index/report validation.

Tests to delete:
- None.

Acceptance criteria:
- `--mode crsd` produces files that SarPy can open/read or fails before writing misleading output.
- Generated CRSD includes metadata, signal array, PVP, source provenance, chunk index, and pulse ranges.
- Lite mode remains permanent and explicitly non-standard.

Risks:
- CRSD compliance may be too large if no writer support exists.

Rollback plan:
- Revert CRSD mode; keep lite mode.

CI-safe or local-only:
- CI-safe only with tiny fixture and no dependency downloads; otherwise local-only validation.

## PR16: End-To-End graphx-crsd-lite Lane

Purpose:
Exercise the full non-standard conversion path without CRSD requirements.

Files to touch:
- CLI end-to-end tests
- Tiny synthetic MAT fixture generator
- Expected report/index fixture snippets

Files to delete:
- None.

Tests to add:
- GOTCHA -> normalized product -> graphx-crsd-lite -> reports.
- Repeated-run determinism.
- Report checksum verification.

Tests to delete:
- None.

Acceptance criteria:
- Tiny fixture conversion is deterministic.
- Output includes lite artifacts, index, report, and warnings log.
- No CRSD validation is required for this lane.

Risks:
- End-to-end tests can become brittle if they compare volatile timestamps.

Rollback plan:
- Revert end-to-end tests only.

CI-safe or local-only:
- CI-safe.

## PR17: GraphX Image Comparison Lane

Purpose:
Compare GraphX image output against Python reference outputs after conversion artifacts are stable.

Files to touch:
- GraphX image output harness
- Comparison invocation docs/tests
- Scenario fixture metadata

Files to delete:
- None.

Tests to add:
- Tiny deterministic image comparison.
- Metrics threshold tests for RMSE, phase error, peak error, correlation, and optional SSIM.

Tests to delete:
- None.

Acceptance criteria:
- Comparison emits `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png`.
- Tiny fixture comparison is deterministic.
- Real datasets remain local-only.

Risks:
- Existing SAR image formation may not be physically equivalent to Python reference.

Rollback plan:
- Revert comparison lane; conversion utility remains.

CI-safe or local-only:
- CI-safe for synthetic fixtures; local-only for real GOTCHA.

## PR18: Local-Only Real GOTCHA Validation

Purpose:
Add an explicitly enabled local workflow for real GOTCHA `.mat` directories.

Files to touch:
- Local runner script
- Documentation
- Optional disabled-by-default test/CTest label

Files to delete:
- None.

Tests to add:
- Local-only smoke command gated by `GRAPHX_SAR_GOTCHA_DATASET`.

Tests to delete:
- None.

Acceptance criteria:
- No downloads.
- No checked-in GOTCHA data.
- Explicit environment variable required.
- CI remains deterministic without real data.

Risks:
- Local-only workflows can drift.

Rollback plan:
- Revert local runner/docs.

CI-safe or local-only:
- Local-only.

## PR19: Future Exporter Interface Extension

Purpose:
Prepare writer registry/interface behavior for future CPHD/SICD exporters without implementing those formats.

Files to touch:
- `ISarWriter` docs or registry code, if needed
- Tests for writer selection behavior

Files to delete:
- None.

Tests to add:
- Writer mode dispatch test for `graphx-crsd-lite` and `crsd`.
- Placeholder rejection test for unsupported future modes.

Tests to delete:
- None.

Acceptance criteria:
- Future writers can implement `ISarWriter` without changing GOTCHA importer or normalized model.
- Unsupported modes fail deterministically.

Risks:
- Premature registry work if CLI mode dispatch is already sufficient.

Rollback plan:
- Revert registry/interface extension.

CI-safe or local-only:
- CI-safe.

## Sequencing Notes

- PR1 and PR2 are documentation/discovery only; PR1 is mandatory before any implementation.
- PR3 and PR4 clean existing SAR ambiguity and broken planning/test artifacts before new utility code.
- PR5 through PR12 build a CI-safe C++ path ending in permanent `graphx-crsd-lite`.
- PR13 and PR14 add Python/SarPy validation and reference tooling outside GraphX runtime.
- PR15 is the first PR allowed to claim standards-targeted CRSD output.
- PR16 validates the permanent lite lane end to end.
- PR17 through PR18 add comparison and real-data workflows after deterministic conversion is stable.
- PR19 is optional and should be skipped if existing mode dispatch is enough.

## Decision Records

Inputs:
 - `docs/sar/gotcha_crsd_repo_discovery.md`

- HDF5 should be a required dependency for the converter 
- classic MAT support should use a third-party C++ library if available.
