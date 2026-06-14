> ARCHIVAL STATUS (2026-06-14): This document is kept for historical traceability. It may reference deprecated GraphX SAR conversion lanes, flags, or scripts. Use the active CRSD-only workflow in plan/prompt examples/doc.md and scripts/convert_gotcha_subdata_to_crsd.sh.

# Why There Is No Real GOTCHA-To-CRSD Converter Yet

Inspector role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: current-state explanation only. This report does not redesign or implement.

## 1. Executive Answer

- Observed: The repository has a GOTCHA-to-normalized-product path and a normalized-product-to-`graphx-crsd-lite` path.
- Observed: The repository does not have a standards-compliant GOTCHA-to-CRSD converter that emits SarPy-openable CRSD.
- Observed: `graphx-crsd-lite` is deliberately labeled `NON-STANDARD` and is implemented as a permanent GraphX intermediate format, not as CRSD.
- Observed: `CrsdWriter::Write` validates that required normalized fields exist, then returns `crsd_writer_unavailable:valid_sarpy_openable_crsd_writer_not_implemented` before creating output artifacts.
- Observed: The CLI `--mode crsd` path calls `CrsdWriter` and fails clearly when the writer is unavailable.
- Inferred: There is no real converter because the planner intentionally built prerequisites first and made PR15 the first PR allowed to claim standards-targeted CRSD output. The implemented PR15 behavior currently preserves the fail-before-misleading-output boundary rather than faking CRSD compliance.

## 2. Planner Timeline

- Observed: Planning rules state that MATLAB is never a build, runtime, or test dependency; GOTCHA is an importer; CRSD/CPHD/SICD/`graphx-crsd-lite` are writer/export targets; `graphx-crsd-lite` is permanent and non-standard; SarPy is validation/comparison infrastructure only.
- Observed: PR1 and PR2 are discovery and definition documents, not implementation.
- Observed: PR5 introduces the normalized SAR product model and reader/writer interfaces.
- Observed: PR6 adds reusable validation before importers/exporters.
- Observed: PR7 adds deterministic GOTCHA input ordering without MAT parsing.
- Observed: PR8 adds MAT inspection only, not normalization or export.
- Observed: PR9 implements `GotchaMatReader` normalization.
- Observed: PR10 implements permanent `graphx-crsd-lite`.
- Observed: PR11 implements chunking, reports, checksums, `gotcha_crsd_index.json`, `conversion_report.json`, and warning logs.
- Observed: PR12 creates `graphx-gotcha-to-crsd` but explicitly says CRSD mode must not fake compliance.
- Observed: PR13 and PR14 add Python/SarPy reference and validation harnesses outside GraphX runtime.
- Observed: PR15 is the first planner item allowed to implement standards-targeted CRSD output. Its acceptance criteria allow either SarPy-openable CRSD output or a clear failure before misleading output.
- Observed: PR16 validates the permanent lite lane end to end without CRSD requirements.
- Observed: PR17 and PR18 add comparison and local-only real-data workflows after deterministic conversion artifacts exist.

## 3. Implemented Capabilities

- Observed: GOTCHA input ordering exists with lexical and manifest modes. It is used by the CLI before reading.
- Observed: MAT inspection exists in `GotchaMatInspector`. It detects MATLAB v7.3/HDF5 signatures, emits `field_inventory.json` and `conversion_assumptions.json`, records `matlab_dependency: not_used`, and reports classic/non-HDF5 MAT as unsupported in the inspection path.
- Observed: `GotchaMatReader` exists and produces `NormalizedSarProduct` from ordered `.mat` filenames plus adjacent JSON sidecars. It preserves source ordering and provenance, extracts IQ samples, waveform metadata, platform position/velocity, pulse time, range sample start, polarization, and original field-name diagnostics from sidecar JSON.
- Observed: The normalized SAR model exists with `ComplexSample`, `PulseVector`, `ChannelSignal`, `WaveformMetadata`, `PlatformState`, `PerVectorParameters`, `CollectionMetadata`, `ReferenceGeometry`, `NormalizedSarProduct`, `ISarReader`, and `ISarWriter`.
- Observed: `SarProductValidator` exists and is used by the CLI when `--validate` is set.
- Observed: `GraphxCrsdLiteWriter` and `GraphxCrsdLiteReader` exist. The writer emits `signal.bin`, `metadata.json`, `index.json`, `conversion_report.json`, and `conversion_warnings.log` inside `.graphx-crsd-lite` chunk directories.
- Observed: `graphx-gotcha-to-crsd` exists. It supports `--input-dir`, `--output-dir`, `--collection-id`, `--max-output-size-mb`, `--sort`, `--manifest`, `--mode`, `--validate`, `--allow-classic-mat-with-sidecar`, and `--emit-index`.
- Observed: The CLI emits root-level `gotcha_crsd_index.json`, `conversion_report.json`, and `conversion_warnings.log` when `--emit-index` is used.
- Observed: Local-only real GOTCHA validation exists through `examples/SAR/tools/local_gotcha_validation.sh`; it is gated by `GRAPHX_SAR_GOTCHA_DATASET`, uses `graphx-crsd-lite`, and is disabled by default in CTest.
- Observed: Python/SarPy tools exist for reference image generation, image comparison, CRSD validation probing, and CRSD reference magnitude extraction. They are local/reference tooling and do not define GraphX runtime contracts.

## 4. Missing Capabilities

- Observed: No standards-compliant CRSD writer exists.
- Observed: No code currently writes a SarPy-openable CRSD product.
- Observed: `CrsdWriter` declares standards-targeted filenames/schema constants, but it does not assemble actual CRSD metadata, PVP arrays, support arrays, signal array packaging, or a SarPy-readable CRSD container.
- Observed: The real GOTCHA-to-CRSD field mapping is incomplete. Current normalization is sidecar-driven and synthetic/tiny-fixture-friendly; it does not fully parse real GOTCHA MAT field layouts into a complete CRSD metadata model.
- Observed: The CLI has mode dispatch for `crsd`, but the writer currently fails before producing output.
- Inferred: The missing core is not command-line plumbing; it is standards-compliant CRSD construction from sufficiently complete GOTCHA-derived normalized metadata.

## 5. Why `graphx-crsd-lite` Is Not CRSD

- Observed: The planner states that `graphx-crsd-lite` is a permanent non-standard GraphX intermediate representation, not a temporary debug format.
- Observed: `GraphxCrsdLiteWriter::kFormatName` is `graphx-crsd-lite`.
- Observed: `GraphxCrsdLiteWriter::kNonStandardLabel` is `NON-STANDARD`.
- Observed: PR16 tests assert that chunk metadata and root reports contain `format == "graphx-crsd-lite"` and `label == "NON-STANDARD"`.
- Observed: Lite output is a GraphX-defined directory layout with `signal.bin`, `metadata.json`, `index.json`, `conversion_report.json`, and `conversion_warnings.log`.
- Observed: Lite output is useful for deterministic round trips, checksums, indexing, report validation, and downstream comparison lanes.
- Inferred: Lite is intentionally close enough to carry normalized phase-history-like data through GraphX validation, but it is not a CRSD container and does not satisfy CRSD standard packaging or SarPy open/read expectations.

## 6. Why `--mode crsd` Fails

- Observed: `CrsdWriter::Write` first rejects missing required fields with `missing_required_fields`.
- Observed: For a product with required fields, `CrsdWriter::Write` returns `crsd_writer_unavailable:valid_sarpy_openable_crsd_writer_not_implemented`.
- Observed: The writer ignores the output directory on this path and does not create artifacts.
- Observed: The CLI `--mode crsd` branch sets CRSD labels/assumptions, then calls `CrsdWriter::Write` for each planned chunk.
- Observed: If `CrsdWriter::Write` fails, the CLI prints `crsd_write_failed:<chunk_dir>:<message>` and exits with failure.
- Inferred: This behavior exists to keep the CLI mode visible while preventing invalid files from being mistaken for real CRSD.
- Inferred: The design favors correctness and observability over convenience: fail before output is safer than writing a pseudo-CRSD artifact.

## 7. Test Evidence

- Observed: `CrsdIoTest.WriterFailsBeforeEmittingMisleadingCrsdArtifacts` asserts that `CrsdWriter` fails with `kUnavailableMessage` and that the output directory does not exist.
- Observed: `CrsdIoTest.WriterFailsForMissingRequiredFields` asserts missing required fields fail and do not create output.
- Observed: `GraphxGotchaToCrsdCliTest.GraphxCrsdLiteModeWorksOnTinyFixture` asserts lite mode succeeds and emits chunk/root lite artifacts.
- Observed: `GraphxGotchaToCrsdCliTest.UnsupportedMatFailsClearlyAndCrsdModeFailsBeforeMisleadingOutput` asserts unsupported classic MAT fails clearly and `--mode crsd` fails with `CrsdWriter::kUnavailableMessage` without creating the CRSD output directory.
- Observed: `Pr16GraphxCrsdLiteLaneTest.EndToEndTinySyntheticConversionEmitsReportsAndChecksums` asserts the end-to-end lite lane emits `graphx-crsd-lite`, `NON-STANDARD`, checksums, index/report files, warning logs, and pulse ranges.
- Observed: `Pr16GraphxCrsdLiteLaneTest.RepeatedTinySyntheticConversionIsDeterministic` compares repeated lite outputs for deterministic JSON, warning logs, and signal checksums.
- Observed: `Pr18LocalGotchaValidationTest` asserts the real-data local workflow requires `GRAPHX_SAR_GOTCHA_DATASET`, does not contain download/install commands, uses `graphx-crsd-lite`, and skips real-data execution when the dataset environment variable is absent.

## 8. Blocking Gaps For Real CRSD

- Observed: Standards-compliant CRSD metadata assembly is not implemented.
- Observed: PVP generation is not implemented.
- Observed: CRSD signal array packaging is not implemented.
- Observed: CRSD provenance and chunk-index artifacts are declared as constants but not written as SarPy-openable CRSD.
- Observed: Support array and antenna concepts are documented in `docs/sar/crsd_definition.md`, but not implemented in a CRSD writer.
- Observed: The current reader path depends on adjacent sidecar JSON for normalized data. This is not the same as complete real GOTCHA MAT parsing.
- Inferred: Real GOTCHA MAT files may have field layout and metadata differences not covered by tiny synthetic sidecars.
- Inferred: A real converter needs both a more complete real-GOTCHA import/mapping layer and a standards-compliant CRSD writer that SarPy can open.
- Unknown: Whether all required CRSD fields can be derived from the currently available GOTCHA data without additional assumptions or external metadata.

## 9. Conclusion

- Observed: There is a working deterministic GraphX conversion lane: GOTCHA-like MAT inputs plus sidecars -> normalized SAR product -> `graphx-crsd-lite` -> root/chunk reports and checksums.
- Observed: There is not a real GOTCHA-to-CRSD converter because the standards-compliant CRSD writer remains unavailable.
- Observed: The planner intentionally separated prerequisite infrastructure from real CRSD writing, and explicitly made PR15 the first point where standards-targeted CRSD could be claimed.
- Observed: Current PR15 behavior chooses the allowed acceptance path of clear failure before misleading output.
- Inferred: The repository is correctly refusing to call lite output CRSD because doing so would collapse an important architecture and validation boundary.
- Inferred: The absence of a real converter is not accidental; it is the result of preserving correctness until CRSD metadata, PVP, signal array packaging, provenance, chunking, and SarPy-openable output are genuinely implemented.
