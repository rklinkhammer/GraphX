> ARCHIVAL STATUS (2026-06-14): Historical planning/prompt artifact. It may reference deprecated GraphX SAR conversion lanes or naming. Use plan/prompt examples/doc.md for current CRSD-only operational guidance.

# SAR PR Implementor And Verifier Agents

Source roadmap: `plan/reviews/SAR_PLANNER_REPORT.md`

Use one implementor prompt and one verifier prompt per PR. Each agent must read:

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`

Global constraints for every agent:

- MATLAB must never become a build, runtime, or test dependency.
- GOTCHA is an importer.
- CRSD, CPHD, SICD, and `graphx-crsd-lite` are writers/export targets.
- `graphx-crsd-lite` is permanent, non-standard, and must be labeled `NON-STANDARD`.
- SarPy is validation/comparison infrastructure only.
- Do not combine PR scopes.
- Do not start future PR work.
- Backward compatibility is not required.

---

## PR1: Repository Discovery For GOTCHA To CRSD

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/reviews/SAR_PLANNER_REPORT.md: Repository Discovery For GOTCHA To CRSD.

Scope:
- Create docs/sar/gotcha_crsd_repo_discovery.md only, unless existing doc tooling requires a minimal docs index update.
- Inspect the current repository to document build system placement, CLI conventions, test framework, fixture conventions, dependency policy, HDF5 availability, classic MAT reader gaps, existing SAR abstractions, and proposed files.
- Explicitly state that MATLAB is not used and must not become a build/runtime/test dependency.
- Identify whether existing CRSD writer support exists in the repository.

Do not implement code.
Do not add dependencies.
Do not create the CRSD definition document; that is PR2.

Output:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed.
5. Build/test command, if any.
6. Remaining follow-up work.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- docs/sar/gotcha_crsd_repo_discovery.md exists.
- Report documents build placement, CLI conventions, tests, fixtures, dependency policy, HDF5 availability, MAT reader gaps, existing SAR abstractions, and proposed files.
- Report states MATLAB is not used.
- Report identifies whether CRSD writer support exists.
- No implementation code, dependency additions, or PR2 content was added.

Stop after verifier report.
```

---

## PR2: CRSD Definition And Mapping Document

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/reviews/SAR_PLANNER_REPORT.md: CRSD Definition And Mapping Document.

Scope:
- Create docs/sar/crsd_definition.md.
- Cover CRSD overview, CRSD -> CPHD -> SICD, collection metadata, timing, channel metadata, PVP, support arrays, signal arrays, geometry, and antenna concepts.
- Document GOTCHA -> GraphX normalized model -> CRSD mappings.
- Define gotcha_crsd_index.json and conversion_report.json schemas.
- State MATLAB is not used.
- State GOTCHA is compensated phase history and initial output may be single-channel pseudo-CRSD.

Do not implement code.
Do not create MAT reader, CLI, lite writer, or Python tools.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- docs/sar/crsd_definition.md exists and covers all PR2 acceptance criteria.
- It defines index and conversion report schemas.
- It states MATLAB is not used.
- It does not introduce implementation code or dependency changes.

Stop after verifier report.
```

---

## PR3: Clean SAR GPU Naming Before New Utility Work

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/reviews/SAR_PLANNER_REPORT.md: Clean SAR GPU Naming Before New Utility Work.

Scope:
- Remove SAR GPU compatibility aliases for H2D/D2H/backprojection.
- Update the definitive SAR config, plugin registration, tests, and README references to explicit SAR-token GPU node names.
- Delete examples/SAR/include/sar/H2DAsyncNode.hpp.
- Delete examples/SAR/include/sar/D2HAsyncNode.hpp.
- Delete examples/SAR/include/sar/SarBackprojectionTransformNode.hpp.

Do not implement GOTCHA/CRSD utility work.
Do not add compatibility shims.
Do not preserve old aliases because tests reference them.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Deleted alias headers are gone.
- Maintained SAR configs no longer depend on alias names.
- Resolver diagnostics do not redefine generic H2D/D2H as SarAccelControlToken.
- examples/SAR/main.cpp still runs with the definitive config.
- Accel-token guardrails still pass.
- No future GOTCHA/CRSD work was added.

Stop after verifier report.
```

---

## PR4: Stabilize External Baseline Planning Artifacts

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/reviews/SAR_PLANNER_REPORT.md: Stabilize External Baseline Planning Artifacts.

Scope:
- Resolve tests that reference missing plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md and plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json.
- Either restore these as real source artifacts or delete/replace tests that only assert missing artifacts exist.
- Keep external baseline tooling local-only and outside libgraph/libgpu.

Do not implement external baseline substitution.
Do not alter GraphX runtime contracts.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- SAR tests no longer fail due to absent planning artifacts.
- External comparison boundaries remain documented or explicitly tested.
- No external package assumptions leaked into libgraph or libgpu.
- No unrelated SAR architecture or GOTCHA/CRSD implementation was added.

Stop after verifier report.
```

---

## PR5: C++ Normalized SAR Product Model And Interfaces

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/reviews/SAR_PLANNER_REPORT.md: C++ Normalized SAR Product Model And Interfaces.

Scope:
- Add the normalized SAR product model in the location identified by PR1.
- Add types: ComplexSample, PulseVector, ChannelSignal, WaveformMetadata, PlatformState, PerVectorParameters, CollectionMetadata, ReferenceGeometry, NormalizedSarProduct.
- Add ISarReader and ISarWriter.
- Add focused unit tests for shape, metadata propagation, pulse/channel/sample indexing, and required fields.

Do not implement MAT reading, validators, lite writer, CRSD writer, CLI, or Python tools.
Do not leak GOTCHA field names into the generic model.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Normalized model supports signal[pulse][channel][sample].
- Required types and reader/writer interfaces exist.
- Tests cover shape/indexing/metadata/required fields.
- GOTCHA-specific names appear only in docs or importer-planned context, not generic model.
- No MAT, lite, CRSD, CLI, or Python work was added.

Stop after verifier report.
```

---

## PR6: Product Validator

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/reviews/SAR_PLANNER_REPORT.md: Product Validator.

Scope:
- Add SarProductValidator against the normalized SAR product model.
- Add deterministic validation for NaN, Inf, metadata completeness, pulse ordering, shape consistency, and sample types.
- Add focused validator unit tests.

Do not implement MAT reader, lite writer, CRSD writer, CLI, or Python tools.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Validator covers NaN/Inf, metadata completeness, pulse ordering, shape consistency, and sample types.
- Errors are deterministic and actionable.
- Validator can be reused by readers/writers.
- No future importer/exporter/CLI work was added.

Stop after verifier report.
```

---

## PR7: Deterministic GOTCHA Input Ordering

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/reviews/SAR_PLANNER_REPORT.md: Deterministic GOTCHA Input Ordering.

Scope:
- Implement lexical and manifest-based input file ordering for future GOTCHA import.
- Add manifest schema docs/tests as needed.
- Add tests for lexical ordering, manifest ordering, missing manifest files, duplicate entries, and empty input directories.

Do not parse MAT contents.
Do not add MATLAB, HDF5, MAT reader, lite writer, CRSD writer, CLI, or Python tools.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Ordering is deterministic.
- Lexical and manifest modes are tested.
- Missing/duplicate/empty cases produce deterministic errors.
- No MAT parsing or dependency work was added.

Stop after verifier report.
```

---

## PR8: C++ MAT Inspection Phase

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR8 from plan/reviews/SAR_PLANNER_REPORT.md: C++ MAT Inspection Phase.

Scope:
- Add C++ MAT inspection only.
- Support MATLAB v7.3/HDF5 detection through open HDF5 support when available.
- For classic/non-HDF5 MAT, use a documented supported subset or deterministic unsupported-format error.
- Emit field_inventory.json and conversion_assumptions.json.
- Add tests for HDF5 detection, classic unsupported error, inventory shape, assumptions shape, and no-MATLAB behavior.

Do not normalize products.
Do not emit graphx-crsd-lite or CRSD.
Do not add MATLAB dependency.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR8 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Inspector emits keys, shapes, and dtypes.
- field_inventory.json and conversion_assumptions.json are tested.
- MATLAB is not a build/runtime/test dependency.
- No normalized product, lite output, CRSD output, CLI, or Python workflow was added.

Stop after verifier report.
```

---

## PR9: GotchaMatReader Normalization

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR9 from plan/reviews/SAR_PLANNER_REPORT.md: GotchaMatReader Normalization.

Scope:
- Implement GotchaMatReader that imports ordered GOTCHA MAT files into the normalized model.
- Extract IQ samples, frequency axis when present, platform positions, pulse time or synthetic index, polarization when present, provenance, source order, and original field-name diagnostics.
- Support initial channel count of one and label it as derived from GOTCHA phase history.

Do not write graphx-crsd-lite.
Do not write CRSD.
Do not add CLI or Python tools.
Do not leak GOTCHA field names into the generic model.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR9 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Reader accepts ordered directory or manifest.
- Required normalized metadata is populated or deterministic errors are emitted.
- Provenance and source order are preserved.
- Original field names stay in diagnostics/importer code.
- No lite/CRSD/CLI/Python work was added.

Stop after verifier report.
```

---

## PR10: Permanent graphx-crsd-lite Format

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR10 from plan/reviews/SAR_PLANNER_REPORT.md: Permanent graphx-crsd-lite Format.

Scope:
- Implement GraphxCrsdLiteWriter and GraphxCrsdLiteReader.
- Outputs must include signal.bin, metadata.json, index.json, and conversion_report.json.
- Preserve provenance, pulse ordering, assumptions, metadata, and checksums.
- Clearly label the format NON-STANDARD.
- Add round-trip tests.

Do not implement full CRSD.
Do not add CLI unless required only for unit test harness; PR12 owns CLI.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR10 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Lite format is permanent and labeled NON-STANDARD.
- Writer outputs required files.
- Reader round trips to normalized product.
- Tests cover checksums, metadata, provenance, pulse ordering, assumptions.
- No full CRSD claim or CLI scope creep.

Stop after verifier report.
```

---

## PR11: Chunking, Reports, And Checksums

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR11 from plan/reviews/SAR_PLANNER_REPORT.md: Chunking, Reports, And Checksums.

Scope:
- Add SarProductChunker.
- Add shared report/index/checksum utilities for lite and future CRSD writers.
- Test chunk splitting by max output size, never splitting pulses, deterministic output names, index schema, report schema, warnings log behavior.

Do not implement CLI.
Do not implement full CRSD.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR11 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Chunking never splits pulses.
- Output naming is deterministic.
- Reports include provenance, assumptions, checksums, warnings, pulse ranges, sample shape, channel count, frequency metadata, and validation status.
- Pulse range semantics are documented/tested.
- No CLI or CRSD writer scope creep.

Stop after verifier report.
```

---

## PR12: CLI Skeleton For graphx-crsd-lite

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR12 from plan/reviews/SAR_PLANNER_REPORT.md: CLI Skeleton For graphx-crsd-lite.

Scope:
- Create graphx-gotcha-to-crsd.
- Wire MAT import, validation, chunking, graphx-crsd-lite writing, and reports.
- Support required args: --input-dir, --output-dir, --collection-id, --max-output-size-mb, --sort, --manifest, --mode, --validate, --emit-index.
- --mode graphx-crsd-lite works on tiny fixture.
- --mode crsd fails clearly until full CRSD writer exists.

Do not implement full CRSD.
Do not add Python/SarPy tools.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR12 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- CLI help documents required options.
- Invalid input, empty input, malformed manifest, and unsupported MAT produce deterministic failures.
- Lite mode works end to end on tiny fixture.
- CRSD mode does not fake compliance.
- No Python/SarPy or full CRSD work was added.

Stop after verifier report.
```

---

## PR13: Python GOTCHA Reference Image And Comparison Tools

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR13 from plan/reviews/SAR_PLANNER_REPORT.md: Python GOTCHA Reference Image And Comparison Tools.

Scope:
- Add tools/sarpy/reference_image_from_gotcha.py.
- Add tools/sarpy/compare_images.py.
- Add/update tools/sarpy/requirements.txt with numpy, scipy, h5py, matplotlib, sarpy.
- Python may use scipy.io.loadmat and h5py for validation/reference only.
- Add tests or local-only wrappers for field discovery, reference image generation, and deterministic metrics.

Do not make Python tools runtime dependencies.
Do not implement SarPy CRSD validation; PR14 owns it.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR13 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Python tools are reference/comparison infrastructure only.
- Outputs include reference image, magnitude PNG, metadata JSON, comparison report, difference magnitude PNG, and phase difference PNG.
- Dependencies are local/gated and not GraphX runtime dependencies.
- No SarPy CRSD harness or full CRSD work was added.

Stop after verifier report.
```

---

## PR14: SarPy CRSD Validation Harness

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR14 from plan/reviews/SAR_PLANNER_REPORT.md: SarPy CRSD Validation Harness.

Scope:
- Add tools/sarpy/validate_crsd.py.
- Add tools/sarpy/reference_image_from_crsd.py.
- Add/update tools/sarpy/requirements.txt as needed.
- Add local-only tests or gated smoke tests.
- Report CRSD version, dimensions, dtype, sample slices, PVP arrays, and JSON validation output when SarPy can open the file.

Do not implement CRSD writer.
Do not make SarPy a GraphX runtime dependency.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR14 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- SarPy harness is optional/local-only unless dependency is already available.
- validate_crsd.py emits JSON validation reports.
- Harness reports required CRSD metadata when possible.
- SarPy does not shape GraphX runtime architecture.
- No CRSD writer was added.

Stop after verifier report.
```

---

## PR15: Full CRSD Writer

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR15 from plan/reviews/SAR_PLANNER_REPORT.md: Full CRSD Writer.

Scope:
- Implement CrsdWriter and --mode crsd behavior.
- Assemble standards-targeted CRSD metadata, signal array, PVP, source provenance, chunk index, and pulse ranges.
- Integrate SarPy validation hooks where available.
- If valid output cannot be produced, fail before writing misleading files.

Prerequisites:
- graphx-crsd-lite, validator, reports, CLI, and SarPy harness must already exist.

Do not fake CRSD compliance.
Do not remove graphx-crsd-lite.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR15 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- --mode crsd either produces SarPy-openable files or fails clearly before misleading output.
- Generated CRSD includes required metadata, signal array, PVP, provenance, chunk index, and pulse ranges.
- SarPy validation passes when dependency is available.
- graphx-crsd-lite remains permanent and NON-STANDARD.

Stop after verifier report.
```

---

## PR16: End-To-End graphx-crsd-lite Lane

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR16 from plan/reviews/SAR_PLANNER_REPORT.md: End-To-End graphx-crsd-lite Lane.

Scope:
- Add end-to-end test for GOTCHA -> normalized product -> graphx-crsd-lite -> reports.
- Use tiny synthetic MAT fixtures only.
- Verify deterministic repeated runs and report checksums.

Do not require CRSD validation.
Do not require real GOTCHA data.
Do not add full CRSD work.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR16 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- End-to-end lite conversion is deterministic.
- Output includes lite artifacts, index, report, and warnings log.
- Tests avoid volatile timestamp brittleness.
- CI does not require real GOTCHA data or CRSD validation.

Stop after verifier report.
```

---

## PR17: GraphX Image Comparison Lane

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR17 from plan/reviews/SAR_PLANNER_REPORT.md: GraphX Image Comparison Lane.

Scope:
- Add GraphX image output comparison against Python reference outputs.
- Add deterministic tiny fixture comparison.
- Test/report metrics: RMSE, phase error, peak error, correlation, optional SSIM.
- Emit comparison_report.json, difference_magnitude.png, and phase_difference.png.

Do not require real GOTCHA data in CI.
Do not alter core GraphX contracts for Python/SarPy tools.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR17 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Comparison lane uses tiny deterministic fixtures for CI.
- Metrics and output artifacts match acceptance criteria.
- Real dataset comparison remains local-only.
- External/Python assumptions do not leak into GraphX core.

Stop after verifier report.
```

---

## PR18: Local-Only Real GOTCHA Validation

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR18 from plan/reviews/SAR_PLANNER_REPORT.md: Local-Only Real GOTCHA Validation.

Scope:
- Add an explicitly enabled local workflow for real GOTCHA .mat directories.
- Gate by GRAPHX_SAR_GOTCHA_DATASET.
- Add local runner docs/scripts and optional disabled-by-default test/CTest label.

Do not download datasets.
Do not check in GOTCHA data.
Do not make local-only workflow required by CI.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR18 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- Requires explicit GRAPHX_SAR_GOTCHA_DATASET.
- No downloads.
- No checked-in GOTCHA data.
- CI remains deterministic without real data.
- Workflow is clearly local-only.

Stop after verifier report.
```

---

## PR19: Future Exporter Interface Extension

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR19 from plan/reviews/SAR_PLANNER_REPORT.md: Future Exporter Interface Extension.

Scope:
- Only add writer dispatch/registry/interface behavior if existing CLI mode dispatch is insufficient.
- Future writers must implement ISarWriter without changing GOTCHA importer or normalized model.
- Add tests for graphx-crsd-lite and crsd writer mode dispatch and deterministic rejection of unsupported modes.

Do not implement CPHD.
Do not implement SICD.
Do not redesign the normalized model.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR19 from plan/reviews/SAR_PLANNER_REPORT.md.

Required checks:
- PR is skipped or minimal if existing mode dispatch is sufficient.
- Future writer extension does not alter GOTCHA importer or normalized model.
- Unsupported modes fail deterministically.
- No CPHD/SICD implementation or broad redesign was added.

Stop after verifier report.
```
