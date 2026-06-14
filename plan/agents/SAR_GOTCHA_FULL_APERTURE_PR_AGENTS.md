# SAR GOTCHA Full-Aperture Implementor And Verifier Agents

Source roadmap: `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`

Use one implementor prompt and one verifier prompt per PR. Each agent must read:

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `docs/sar/gotcha_large_scene_data_description.md`
- `docs/sar/crsd_definition.md`
- `docs/sar/gotcha_crsd_repo_discovery.md`
- `plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md`

Global constraints for every agent:

- MATLAB must never become a build, runtime, or test dependency.
- GOTCHA source data is processed phase history, not raw collection data.
- Full-aperture conversion means all `Np` pulses from every ordered input file.
- `phdata` is the signal source.
- `K`, `deltaF`, and `minF` drive frequency/sample metadata.
- `AntX`, `AntY`, `AntZ`, and `R0` drive geometry/reference-range mapping.
- The local Cartesian scene-center frame must be preserved or explicitly documented.
- Missing CRSD metadata must be handled honestly: derived, supplied externally, or marked unknown/not modeled.
- `graphx-crsd-lite` remains permanent and NON-STANDARD.
- SarPy remains optional local validation/comparison infrastructure only.
- Keep local-only real GOTCHA workflows optional and out of CI.
- Do not combine PR scopes.
- Do not start future PR work.
- Backward compatibility is not required.

---

## PR1: Extend GOTCHA Field Inventory Validation

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Extend GOTCHA Field Inventory Validation.

Scope:
- Add required GOTCHA field validation for Np, K, deltaF, minF, AntX, AntY, AntZ, R0, and phdata.
- Implement deterministic missing-field/type diagnostics in the existing inspector path.
- Wire CLI preflight so conversion fails before reading when required inventory is incomplete.
- Add focused synthetic JSON/sidecar tests for missing required fields.
- Update docs only where needed to cite docs/sar/gotcha_large_scene_data_description.md as the authoritative dataset field reference.

Do not implement full-pulse reading.
Do not implement aperture concatenation.
Do not change graphx-crsd-lite or CRSD writers.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- Required field validation covers Np, K, deltaF, minF, AntX, AntY, AntZ, R0, and phdata.
- Missing-field/type errors name the field and are deterministic/actionable.
- CLI preflight fails before MAT read/conversion when required inventory is incomplete.
- Focused tests cover all required fields with synthetic fixtures.
- MATLAB and new external dependencies were not added.
- No full-pulse reader, aperture concatenation, lite writer, or CRSD writer work was added.

Stop after verifier report.
Save report
```

---

## PR2: Extend GotchaMatReader To Support Full-Pulse Ingestion

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Extend GotchaMatReader To Support Full-Pulse Ingestion.

Scope:
- Update GotchaMatReader so Read() ingests every pulse described by Np for each input file.
- Remove internal hardcoded single pulse_index behavior from the full-aperture path.
- Preserve per-pulse ordering within each file.
- Ensure the normalized product contains one PulseVector per pulse and total pulse count equals sum(Np).
- Add focused tests with a synthetic multi-pulse-per-file fixture.

Do not implement multi-file aperture validation beyond preserving existing input order.
Do not add metadata mapper work.
Do not change report schemas except as required for existing tests to compile.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- GotchaMatReader reads all Np pulses per file.
- Pulse order within a file is deterministic and preserved.
- Normalized output has one PulseVector per pulse.
- Total normalized pulse count equals sum(Np) across input files.
- Synthetic multi-pulse tests cover the new behavior.
- No aperture concatenation validator, metadata mapper, report schema expansion, CRSD writer, or MATLAB dependency was added.

Stop after verifier report.
save report
```

---

## PR3: Add Multi-File Aperture Ordering And Validation

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Add Multi-File Aperture Ordering And Validation.

Scope:
- Add deterministic multi-file aperture ordering rules for subData01.mat through subData10.mat.
- Preserve existing lexical and manifest modes, with manifest mode allowed to override lexical order.
- Add validation for duplicate, missing, out-of-order, or gapped aperture pulse/file sequencing where the model provides enough information.
- Apply ordering before reader ingestion in the CLI path.
- Add CI-safe synthetic multi-file aperture ordering/validation tests.

Do not change the normalized product model except where strictly needed to consume existing ordering metadata.
Do not add metadata mapper work.
Do not add report schema work.
Do not add real-data tests.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- Lexical ordering of subData01.mat through subData10.mat is deterministic.
- Manifest ordering can override lexical ordering.
- Duplicate, missing, out-of-order, or gapped aperture sequence cases produce deterministic diagnostics.
- CLI applies ordering before GOTCHA read/conversion.
- Synthetic multi-file tests cover valid and invalid apertures.
- No metadata mapper, report schema expansion, real-data workflow, CRSD writer, or MATLAB dependency was added.

Stop after verifier report.
save report
```

---

## PR4: Update Normalized Product For Full-Aperture Pulse Metadata

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Update Normalized Product For Full-Aperture Pulse Metadata.

Note plan/reviews/SAR_RENAME_CRSD_LITE_IMPLEMENTER_REPORT.md for post PR3 changes.

Scope:
- Review and minimally extend NormalizedSarProduct, PulseVector, and PerVectorParameters only as needed for full-aperture pulse/file metadata.
- Extend SarProductValidator for pulse-count consistency, shape consistency, frequency metadata consistency, and geometry completeness.
- Distinguish blocking errors from informational warnings where antenna/platform differences are expected across pulses.
- Add focused validator/model tests for multi-file full-aperture products.

Do not implement GOTCHA reader changes beyond consuming model fields already produced by earlier PRs.
Do not implement graphx-crsd-lite metadata mapping.
Do not add real-data tests.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- NormalizedSarProduct can represent sum(Np) pulses across multiple files.
- Validator checks pulse count, shape consistency, frequency metadata consistency, and geometry completeness.
- Validator diagnostics distinguish blocking errors from informational warnings.
- Antenna/platform differences are not incorrectly blocked when represented per-pulse.
- Focused tests cover multi-file validation.
- No lite metadata mapper, real-data workflow, CRSD writer, or MATLAB dependency was added.

Stop after verifier report.
save report
```

---

## PR5: Map GOTCHA Metadata To CRSD/Lite Fields

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Map GOTCHA Metadata To CRSD/Lite Fields.

Note plan/reviews/SAR_RENAME_CRSD_LITE_IMPLEMENTER_REPORT.md for post PR3 changes.


Scope:
- Add a focused GOTCHA-to-product metadata mapper in the SAR IO area.
- Map K, deltaF, and minF to frequency_axis, carrier_hz, bandwidth_hz, and sample count.
- Map AntX, AntY, AntZ, and R0 to antenna phase-center/local geometry/reference-range metadata.
- Update graphx-crsd-lite metadata output to preserve these fields with local Cartesian frame labeling.
- Add round-trip/unit tests proving mapped metadata appears in lite metadata JSON.

Do not implement standards-compliant CRSD metadata beyond existing writer boundaries.
Do not add real-data tests.
Do not add MATLAB or new external dependencies.
Do not change local-only workflow requirements.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- carrier_hz is derived as minF + (K - 1) * deltaF / 2, or any different formula is explicitly documented and tested.
- bandwidth_hz is derived deterministically from K and deltaF.
- frequency_axis contains K sample frequencies.
- AntX/AntY/AntZ and R0 are preserved in lite metadata.
- Local Cartesian scene-center frame is labeled clearly.
- Round-trip/unit tests verify metadata preservation.
- No real-data workflow, standards CRSD expansion, MATLAB dependency, or new external dependency was added.

Stop after verifier report.
save report
```

---

## PR6: Add Synthetic Multi-File Multi-Pulse Fixtures And Tests

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Add Synthetic Multi-File Multi-Pulse Fixtures And Tests.

Scope:
- Add deterministic CI-safe synthetic full-aperture fixtures for 2-file/10-pulse-each and 10-file/5-pulse-each cases.
- Add manifest/checksum fixture metadata as planned.
- Add full-aperture integration tests that read fixtures, validate total pulse counts, convert to graphx-crsd-lite, and verify metadata preservation.
- Verify repeated runs are deterministic.

Do not use real GOTCHA data.
Do not require CRSD validation.
Do not add standards CRSD writer work.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- 2-file fixture produces 20 total pulses.
- 10-file fixture produces 50 total pulses.
- graphx-crsd-lite output contains correct pulse counts and mapped metadata.
- Repeated fixture conversions are deterministic.
- Tests are CI-safe and do not require real GOTCHA data or CRSD validation.
- No standards CRSD writer, real-data workflow, MATLAB dependency, or new external dependency was added.

Stop after verifier report.
save report
```

---

## PR7: Update Conversion Report For Full-Aperture Pulse Accounting

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Update Conversion Report For Full-Aperture Pulse Accounting.

Scope:
- Extend conversion_report.json schema/data with total_files_read, total_pulses_read, pulses_per_file, full_aperture/subset status, and pulse_selection_method when applicable.
- Populate the new fields from full-aperture conversion.
- Ensure reports clearly state when subset mode is used.
- Add focused tests for multi-file report pulse accounting.

Do not add reader behavior beyond consuming existing pulse accounting.
Do not add metadata mapper work beyond existing fields.
Do not add real-data tests.
Do not add standards CRSD writer work.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- conversion_report.json includes total_files_read, total_pulses_read, and pulses_per_file.
- Report distinguishes full-aperture mode from subset mode.
- Subset mode, if still available, reports pulse_selection_method clearly.
- Multi-file report tests verify correct accounting.
- Existing conversion reports remain deterministic.
- No real-data workflow, standards CRSD writer, MATLAB dependency, or new external dependency was added.

Stop after verifier report.
save report
```

---

## PR8: Implement Local-Only Real GOTCHA Multi-File Validation

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR8 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Implement Local-Only Real GOTCHA Multi-File Validation.

Scope:
- Add a local-only real GOTCHA full-aperture validation test/workflow gated by GRAPHX_SAR_GOTCHA_DATASET.
- Update scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh only as needed to use the full-aperture path.
- Verify local workflow reads and converts all pulses from all ten files when the dataset is provided.
- Ensure CI skips cleanly when the environment variable is not set.
- Link docs/sar/gotcha_large_scene_data_description.md to the validation instructions.

Do not download datasets.
Do not check in GOTCHA data.
Do not make real-data workflow required by CI.
Do not add standards CRSD writer work.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
save report
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR8 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- Local validation requires explicit GRAPHX_SAR_GOTCHA_DATASET.
- CI/default run skips without real GOTCHA data.
- No downloads and no checked-in GOTCHA data.
- When enabled locally, workflow verifies all pulses from all ten files and graphx-crsd-lite output pulse counts.
- Conversion report shows total_pulses_read == sum(Np) for the real dataset.
- No standards CRSD writer, MATLAB dependency, or new external dependency was added.

Stop after verifier report.
```

---

## PR9: Documentation Update For Full-Aperture GOTCHA Conversion

### Implementor Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR9 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md: Documentation Update For Full-Aperture GOTCHA Conversion.

Scope:
- Update docs/sar/gotcha_large_scene_data_description.md with final full-aperture conversion notes.
- Update docs/sar/crsd_definition.md GOTCHA mapping for Np, K, deltaF, minF, AntX, AntY, AntZ, R0, and phdata.
- Update docs/CONSOLIDATED_OPERATIONS.md with full-aperture GOTCHA conversion and local validation instructions.
- Update examples/SAR/README.md only if it exists; otherwise update the nearest SAR-facing README reference.
- Clarify local Cartesian frame handling and missing metadata boundaries.

Do not implement code.
Do not change tests except documentation path assertions if existing tests require it.
Do not add standards CRSD writer work.
Do not add MATLAB or new external dependencies.

Output the standard IMPLEMENTER summary.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR9 from plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md.

Required checks:
- Dataset doc covers all GOTCHA fields and full-aperture behavior.
- CRSD definition maps GOTCHA fields through normalized model to lite/CRSD concepts.
- Consolidated operations doc includes full-aperture conversion and local-only validation instructions.
- SAR-facing README/docs link to the dataset description.
- Docs state MATLAB is not used.
- Docs explain local Cartesian frame handling and missing metadata boundaries.
- No implementation code, standards CRSD writer work, MATLAB dependency, or new external dependency was added.

Stop after verifier report.
```



Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Fix the PR2 verifier failure documented in:

plan/reviews/SAR_GOTCHA_FULL_APERTURE_VERIFY_PR2.md

Context:
PR2 full-pulse reader behavior passed, but the full SAR unit binary failed because older conversion-lane tests generate synthetic GOTCHA sidecars that are missing the PR1-required `Np` field.

Scope:
- Update only the failing conversion-lane test fixtures/helpers so their generated sidecar JSON includes all PR1-required GOTCHA fields, especially `Np`.
- Target the failing tests named in the verifier report:
  - `GraphxGotchaToCrsdCliTest.GraphxCrsdLiteModeWorksOnTinyFixture`
  - `GraphxGotchaToCrsdCliTest.UnsupportedMatFailsClearlyAndCrsdModeProducesSarpyOpenableOutput`
  - `GraphxCrsdLiteLaneTest.EndToEndTinySyntheticConversionEmitsReportsAndChecksums`
  - `GraphxCrsdLiteLaneTest.RepeatedTinySyntheticConversionIsDeterministic`
- Preserve the intended test behavior and assertions.
- Keep the fixtures synthetic, tiny, deterministic, and CI-safe.
- Run the focused failing test filter and the full SAR unit binary.

Do not change `GotchaMatReader` behavior.
Do not weaken PR1 required-field validation.
Do not add compatibility shims or default missing required fields in production code.
Do not implement aperture concatenation validation, metadata mapper work, report schema expansion, CRSD writer changes, or real-data workflow changes.
Do not add MATLAB or new external dependencies.

Output:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed.
5. Build/test command and result.
6. Remaining follow-up work.

save report