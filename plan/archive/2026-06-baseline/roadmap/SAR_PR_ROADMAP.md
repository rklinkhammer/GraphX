# SAR PR Cleanup Roadmap

Inputs:

- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Scope: PR-sized cleanup roadmap only. Do not implement from this document directly without issuing a single-PR implementer prompt.

Roadmap rule: there is exactly one canonical SAR GPU path. Maintained SAR GPU runtime edges use `SarAccelControlToken`. CRSD phase-history and focused-image work must either become token-compatible or be explicitly outside the maintained GPU runtime path.

## PR1: Establish Maintained SAR Runtime Boundary

Purpose:

- Remove the most dangerous ambiguity: whether CRSD typed payload stages are part of the canonical SAR GPU runtime.
- Define the maintained runtime boundary as `SarAccelControlToken` on active SAR GPU edges.
- Classify non-token CRSD phase-history and focused-image stages as either CPU/reference stages or work that must be tokenized before being called canonical GPU runtime.

Files to touch:

- `docs/sar/metal_node_truth_in_labeling.md`
- SAR architecture docs under `docs/sar`
- SAR config guardrail tests under `examples/SAR/test`

Files to delete:

- None unless there are docs that explicitly claim non-token CRSD typed edges are the canonical SAR GPU path.

Tests to add:

- Guardrail test that maintained SAR GPU configs use `SarAccelControlToken` for GPU edges.
- Guardrail test that non-token CRSD phase-history/focused-image configs are clearly classified outside the canonical GPU path until tokenized.

Tests to delete:

- Tests whose only purpose is to preserve ambiguity between typed CRSD payloads and canonical SAR GPU tokens.

Acceptance criteria:

- The repository states one canonical SAR GPU path.
- CRSD typed payload stages are not silently treated as the canonical GPU path.
- Tests fail if a maintained GPU config introduces non-token SAR GPU edges.
- The project compiles and the SAR unit tests pass.

Risks:

- Existing CRSD demos may need clearer classification before behavior changes happen.

Rollback plan:

- Revert documentation and guardrail tests only; no runtime behavior should change in this PR.

CI-safe or local-only:

- CI-safe.

## PR2: Tokenize CRSD Focused-Image Runtime Boundaries

Purpose:

- Replace free-standing CRSD phase-history and focused-image graph edges with token-compatible boundaries or move them out of maintained runtime.
- Preserve CRSD focused-image behavior while removing the parallel runtime type system.

Files to touch:

- `examples/SAR/include/sar/SarPhaseHistoryModel.hpp`
- CRSD input, aperture assembly, focused-image transform, and focused-image sink nodes under `examples/SAR`
- SAR CRSD focused-image configs under `examples/SAR/config`
- Focused CRSD node tests under `examples/SAR/test`

Files to delete:

- Any obsolete free-standing graph edge wrappers made unnecessary by token-compatible payload references.

Tests to add:

- CRSD input-to-assembly token lineage test.
- Aperture assembly token sidecar preservation test.
- Focused-image sink artifact lineage test.
- Negative test rejecting non-token CRSD GPU edges in maintained configs.

Tests to delete:

- Tests that assert `SarPhaseHistoryControlMessage` or `FocusedImageResult` is the maintained graph transport type.

Acceptance criteria:

- Maintained CRSD focused-image runtime boundaries are token-compatible or explicitly not maintained GPU runtime.
- Ordered CRSD input still produces deterministic image/artifact output for tiny fixtures.
- No compatibility shim preserves the old graph contract.
- The project compiles and the focused SAR/CRSD tests pass.

Risks:

- This may touch several CRSD nodes and require careful fixture updates.

Rollback plan:

- Revert token-boundary changes and associated tests as one unit.

CI-safe or local-only:

- CI-safe with tiny fixtures.

## PR3: Remove Unsupported Metal Production Surfaces

Purpose:

- Prevent unsupported or incomplete Metal nodes from appearing production-ready.
- Keep truth-in-labeling enforceable.

Files to touch:

- Metal node registration/config files
- `docs/sar/metal_node_truth_in_labeling.md`
- SAR Metal tests under `examples/SAR/test`
- libgpu Metal plugin tests where applicable

Files to delete:

- Active plugin/config exposure for `CollectiveReduceNodeMetal` until supported.
- Production config references to `CrsdFocusedImageTransformMetalNode` unless the node has real native Metal focused-image processing.

Tests to add:

- Test that unsupported Metal nodes are not registered in maintained production configs.
- Test that SAR domain Metal nodes with CPU fallback are labeled experimental or excluded from production configs.

Tests to delete:

- Tests that treat unsupported/incomplete Metal nodes as production-ready capability.

Acceptance criteria:

- `CollectiveReduceNodeMetal` is not exposed as a supported production node.
- Incomplete CRSD focused-image Metal is not advertised as production Metal.
- Generic working Metal transfer/kernel primitives remain covered.
- The project compiles and Metal guardrail tests pass.

Risks:

- Removing active registration may affect demos that were relying on unsupported surfaces.

Rollback plan:

- Revert registration/config removals and guardrail updates.

CI-safe or local-only:

- CI-safe where Metal tests are already optional or guarded.

## PR4: Consolidate Maintained SAR Configs

Purpose:

- Reduce active config surface to the maintained workflows.
- Delete stale scenario/manual configs once equivalent behavior tests exist.

Files to touch:

- `examples/SAR/config`
- SAR config tests under `examples/SAR/test`
- README/docs references to maintained SAR configs

Files to delete:

- Duplicate or stale SAR configs that are not maintained stripmap, CRSD input, focused-image, local-validation, or comparison workflows.

Tests to add:

- Config inventory test listing maintained configs and their intended workflow class.
- Runtime smoke test for each maintained CI-safe config.

Tests to delete:

- Tests that only assert obsolete config files exist.

Acceptance criteria:

- Maintained configs are few, named by behavior, and tested.
- Obsolete configs are gone.
- No compatibility aliases or duplicate legacy config names remain.
- The project compiles and config smoke tests pass.

Risks:

- Some local workflows may need updated documentation if they used deleted configs.

Rollback plan:

- Restore deleted config and test references from the previous commit.

CI-safe or local-only:

- CI-safe, except explicitly local-only configs remain gated and are not required by CI.

## PR5: Clean Planning-Era Naming From Active Surfaces

Purpose:

- Remove `prXX`, `rrpXX`, verifier/implementer phase names, and similar planning-era labels from active code, tests, tools, docs, configs, and user-visible strings.
- Preserve history only in explicit history folders.

Files to touch:

- Active SAR tests, tools, docs, configs, and CMake references containing planning-era names
- `plan/reviews`
- `plan/history`

Files to delete:

- Active tests/docs that only prove historical artifacts existed.

Tests to add:

- Naming hygiene test that rejects planning-era names outside allowed history folders.
- Smoke tests for renamed retained capability tests.

Tests to delete:

- Intermediate-only PR/verification artifact tests.

Acceptance criteria:

- Active SAR files use behavior/product names, not PR history names.
- Historical implementation/verifier reports live only in allowed history locations.
- No compatibility shims preserve old names.
- The project compiles and SAR tests pass.

Risks:

- Broad renames can create stale references.

Rollback plan:

- Revert the rename/delete commit as a unit.

CI-safe or local-only:

- CI-safe.

## PR6: Normalize Intermediate Artifact Naming

Purpose:

- Ensure GraphX-owned intermediate SAR artifacts do not imply CRSD compliance.
- Preserve `--mode crsd` exclusively for standards-targeted CRSD output.

Files to touch:

- GOTCHA/CRSD CLI sources
- GraphX normalized SAR writer/reader files
- Docs and scripts referencing intermediate artifact names
- Tests for CLI modes, reports, and generated artifact labels

Files to delete:

- Old ambiguous intermediate artifact names, extensions, and tests after replacement.

Tests to add:

- CLI test for the GraphX normalized/intermediate mode.
- Guardrail test that intermediate reports say NON-STANDARD and not CRSD.
- Test that `--mode crsd` remains standards-targeted and fails before misleading output when unsupported.

Tests to delete:

- Tests asserting old ambiguous intermediate names.

Acceptance criteria:

- Intermediate artifact naming is unambiguously GraphX-owned and non-standard.
- `--mode crsd` is not used for intermediate output.
- No compatibility alias keeps the old mode name.
- The project compiles and CLI tests pass.

Risks:

- Local scripts and docs must be updated together to avoid user confusion.

Rollback plan:

- Revert mode/name changes and tests as one unit.

CI-safe or local-only:

- CI-safe with synthetic fixtures.

## PR7: Add Basic Performance Instrumentation

Purpose:

- Add stable timing and throughput instrumentation before optimization or baseline substitution.
- Make CPU, transfer, and GPU stages report comparable metrics through sidecar/report artifacts.

Files to touch:

- `SarSidecar` diagnostics fields if needed
- SAR diagnostics sink
- SAR focused-image/report generation
- SAR performance tests/docs

Files to delete:

- Ad hoc performance output that duplicates the new stable report fields.

Tests to add:

- Deterministic schema test for performance fields.
- Smoke test proving instrumentation appears in stripmap and CRSD focused-image reports.

Tests to delete:

- Brittle tests that assert wall-clock values rather than field presence and consistency.

Acceptance criteria:

- Reports include stable stage timing/throughput fields.
- Tests validate schema and monotonic/non-negative properties, not exact runtime values.
- No optimization is attempted in this PR.
- The project compiles and instrumentation tests pass.

Risks:

- Timing fields can become flaky if tests overconstrain values.

Rollback plan:

- Remove instrumentation fields and report assertions.

CI-safe or local-only:

- CI-safe.

## PR8: External SAR Baseline Survey

Purpose:

- Survey candidate external SAR baseline packages after token architecture and instrumentation are stable.
- Keep the survey outside GraphX core.

Files to touch:

- `docs/sar` or `plan/reviews` baseline survey document
- Baseline policy/registry files if they remain active planning artifacts

Files to delete:

- Obsolete baseline notes that conflict with the new survey.

Tests to add:

- Documentation/registry consistency test, if an existing policy test harness exists.

Tests to delete:

- Tests that assert missing or obsolete baseline artifacts exist.

Acceptance criteria:

- Survey identifies candidates, licensing, install model, input/output support, expected image products, and local-only constraints.
- No dependency is added.
- No runtime contract changes occur.
- Existing tests still pass.

Risks:

- Survey may identify no acceptable package.

Rollback plan:

- Revert survey and registry edits.

CI-safe or local-only:

- CI-safe.

## PR9: Select One External Baseline Package

Purpose:

- Choose one external baseline package for the first comparison lane.
- Document why it was selected and what it will not do.

Files to touch:

- Baseline registry/policy document
- `docs/sar` baseline selection note

Files to delete:

- Superseded candidate-selection notes if they are not history.

Tests to add:

- Registry schema/consistency test naming exactly one selected first baseline.

Tests to delete:

- Tests that expect multiple first baselines.

Acceptance criteria:

- One first baseline is selected.
- The package remains local-only and optional.
- No package install is required for CI.
- Existing tests still pass.

Risks:

- The selected package may later fail on real local data.

Rollback plan:

- Revert selection note and registry change.

CI-safe or local-only:

- CI-safe.

## PR10: Add Local-Only Baseline Runner Script

Purpose:

- Add a local-only runner for the selected baseline package.
- Keep external execution outside libgraph and libgpu.

Files to touch:

- `examples/SAR/tools` or `tools/sarpy` equivalent selected-baseline runner location
- Local-only docs
- Optional CTest label configuration

Files to delete:

- Obsolete runner stubs for non-selected baseline packages.

Tests to add:

- Runner argument/preflight tests using missing-dependency and missing-dataset cases.
- Optional disabled-by-default local test label.

Tests to delete:

- Tests for unselected runner stubs.

Acceptance criteria:

- Runner is explicitly gated by environment variables.
- It does not download data or packages.
- CI passes without the baseline package installed.
- Runtime core is untouched.

Risks:

- Environment-variable contract may need careful documentation.

Rollback plan:

- Remove runner, docs, and tests.

CI-safe or local-only:

- Local-only runner; CI-safe preflight tests only.

## PR11: Add GraphX-vs-Baseline Comparison Harness

Purpose:

- Compare GraphX outputs against selected-baseline outputs using stable artifact contracts.
- Add comparison before any substitution experiment.

Files to touch:

- SAR comparison tools
- Report schema docs
- Comparison tests

Files to delete:

- Duplicate comparison tools that do not use the selected artifact contract.

Tests to add:

- Synthetic comparison test for RMSE, phase error, peak error, correlation, and optional SSIM if available.
- Report schema test for comparison outputs.

Tests to delete:

- Tests tied to obsolete surrogate names or retired artifact contracts.

Acceptance criteria:

- Harness compares artifacts without requiring external packages in CI.
- Metrics are deterministic on tiny fixtures.
- Real-data comparison remains local-only.
- The project compiles and comparison tests pass.

Risks:

- Metric thresholds may be too strict before algorithm parity is understood.

Rollback plan:

- Remove harness and comparison tests.

CI-safe or local-only:

- CI-safe for tiny fixture comparisons; local-only for real baseline runs.

## PR12: Add Tiny Deterministic Fixture Comparison

Purpose:

- Provide a tiny deterministic fixture comparison lane that CI can run.
- Ensure GraphX output comparison is not only local/manual.

Files to touch:

- Tiny SAR fixture files
- Comparison test data
- CMake/CTest wiring

Files to delete:

- Generated or oversized fixtures that should not be checked in.

Tests to add:

- CI fixture test comparing GraphX output to committed expected metrics or derived expected artifacts.

Tests to delete:

- Brittle tests using timestamps or machine-local paths.

Acceptance criteria:

- CI runs comparison without real GOTCHA/OpenSAR data.
- Fixtures are small and deterministic.
- No licensing issue is introduced.
- The project compiles and fixture comparison passes.

Risks:

- Fixture may be too simple to catch meaningful SAR regressions.

Rollback plan:

- Remove fixture files and CI test wiring.

CI-safe or local-only:

- CI-safe.

## PR13: Add CI-Safe Derived Fixture If Licensing Permits

Purpose:

- Add a more realistic derived fixture only if licensing and provenance allow it.
- Keep real datasets out of the repository.

Files to touch:

- Derived fixture provenance docs
- Fixture generation or storage location
- CI comparison tests

Files to delete:

- Any fixture candidate whose provenance is unclear.

Tests to add:

- Provenance/manifest test for derived fixture metadata.
- CI comparison test using the derived fixture.

Tests to delete:

- None unless replacing a weaker fixture.

Acceptance criteria:

- Licensing/provenance is documented.
- Fixture is small, deterministic, and CI-safe.
- No real restricted dataset is checked in.
- The project compiles and derived fixture tests pass.

Risks:

- Licensing may block this PR entirely.

Rollback plan:

- Delete derived fixture and associated tests/docs.

CI-safe or local-only:

- CI-safe only if licensing permits; otherwise skip this PR.

## PR14: Add Optional Local GOTCHA/OpenSAR Benchmark

Purpose:

- Add explicitly enabled local benchmarking for real datasets after CI fixture comparison exists.
- Use the stable performance instrumentation from PR7.

Files to touch:

- Local benchmark scripts
- Local-only docs
- Optional disabled-by-default CTest labels

Files to delete:

- Obsolete local benchmark scripts with unclear dataset requirements.

Tests to add:

- Preflight tests for missing dataset, missing baseline package, and output directory validation.

Tests to delete:

- Tests that require real data in CI.

Acceptance criteria:

- Benchmark requires explicit dataset environment variables.
- No downloads.
- No real data checked in.
- CI remains deterministic without real data.
- Reports include GraphX and selected-baseline metrics when both are available.

Risks:

- Local benchmark runtime may be long.

Rollback plan:

- Remove benchmark script, docs, and optional test label.

CI-safe or local-only:

- Local-only, with CI-safe preflight tests.

## PR15: Add Baseline Substitution Experiment

Purpose:

- Add an experiment where GraphX replaces the selected baseline SAR stage in one selected test.
- Keep it experimental and outside core runtime contracts unless independently promoted later.

Files to touch:

- Experimental substitution harness
- Local-only docs
- Optional test label
- Comparison report tooling if needed

Files to delete:

- Any previous substitution stubs that bypass the selected comparison harness.

Tests to add:

- Local-only substitution preflight test.
- Tiny fixture substitution smoke test if it does not require external package installation.

Tests to delete:

- Tests that imply substitution is a production runtime contract.

Acceptance criteria:

- Substitution experiment is clearly experimental.
- It runs only when explicitly enabled.
- It uses the selected baseline and comparison harness.
- GraphX core contracts do not change.
- The project compiles and CI-safe smoke/preflight tests pass.

Risks:

- Substitution can blur external-tool and runtime boundaries if not clearly gated.

Rollback plan:

- Remove substitution harness and optional labels.

CI-safe or local-only:

- Local-only, with CI-safe smoke/preflight coverage where possible.

## Final Verification Step

Purpose:

- Verify the cleanup roadmap delivered the intended architecture without historical or unsupported surfaces leaking back into active use.

Files to touch:

- Naming/architecture guardrail tests
- Documentation index if needed

Files to delete:

- Any remaining active obsolete artifact found by the verification search.

Tests to add:

- Repository-wide guardrail that rejects planning-era active names outside allowed history folders.
- Repository-wide guardrail that rejects unsupported Metal nodes in production configs.
- Repository-wide guardrail that maintained SAR GPU configs use `SarAccelControlToken`.

Tests to delete:

- None unless they duplicate the final guardrails exactly.

Acceptance criteria:

- Searches for forbidden planning-era names pass outside allowed history folders.
- Maintained SAR GPU configs have exactly one canonical token path.
- Unsupported Metal nodes are absent from production configs.
- External baseline tools remain optional/local-only.
- Full SAR unit test suite passes.

Risks:

- Repository-wide searches can be noisy if allowlists are not precise.

Rollback plan:

- Revert final guardrails and any final cleanup deletes.

CI-safe or local-only:

- CI-safe.
