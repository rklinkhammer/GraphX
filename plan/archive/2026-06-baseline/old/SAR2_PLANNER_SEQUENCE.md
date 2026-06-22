# SAR2 Planner Sequence

Date: 2026-06-10
Role: PLANNER
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
Inputs:
- `plan/reviews/SAR2_INSPECTOR.md`
- `plan/reviews/SAR2_SIMPLIFIER.md`
- `plan/reviews/SAR2_ARCHITECT.md`
- current repository state
- verifier report: `plan/reviews/SAR_VERIFY_RRP4_1.md`

## 1) Executive Planning Recommendation

Proceed with a 5-PR sequence that starts with ambiguity-reducing cleanup, then semantic transport clarification, then duplication reduction, then CI-safe SAR correctness hardening, and finally local-only external artifact comparison wiring.

This keeps PRs reviewable, avoids large rewrites, and separates cleanup, correctness, and external baseline work.

Current checkpoint alignment:
- `AccelControlToken<SarSidecar>` remains the canonical SAR runtime contract.
- The SAR validation layer already includes benchmark tracing, external-baseline policy/registry, comparator tooling, replay guide, tiny fixture, and bounded CI lane.
- Working tree is clean.

## 2) Proposed Next PRs

### PR1

- Title: Retire Unused SAR Transport Helper Structs
- Purpose: Remove or explicitly retire `SarMessageEnvelope`, `SarBufferDescriptor`, and `SarGpuMetadata` to reduce canonical model ambiguity.
- Files likely touched:
  - `examples/SAR/include/sar/SarMessages.hpp`
  - `examples/SAR/test/CMakeLists.txt`
- Files likely deleted:
  - None required if removed in-place.
- Tests to add:
  - Compile-time contract test asserting canonical SAR contract remains `AccelControlToken<SarSidecar>`.
- Tests to update/delete:
  - Update tests that reference retired structs, if any.
- Acceptance criteria:
  - No in-repo references to retired struct names remain.
  - Canonical token contract remains unchanged and test-backed.
- Verifier checks:
  - Search confirms zero references to retired type names.
  - SAR unit target builds and passes.
- Scope class: CI-safe
- Risk level: Low

### PR2

- Title: Freeze Opaque Transport Semantics for `host_ptr` and `ready_event`
- Purpose: Remove ambiguity by defining these fields as opaque transport metadata only, with SAR identity remaining in sidecar.
- Files likely touched:
  - `examples/SAR/include/sar/SarMessages.hpp`
  - `examples/SAR/src/H2DAsyncAccelNode.cpp`
  - `examples/SAR/src/D2HAsyncAccelNode.cpp`
  - `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
  - `examples/SAR/src/GotchaReplaySourceNode.cpp` (if identity assumptions exist there)
- Files likely deleted:
  - None
- Tests to add:
  - Assertions that SAR identity decisions derive from sidecar fields and not transport sentinels.
- Tests to update/delete:
  - Update node tests that currently imply identity semantics from `host_ptr`/`ready_event`.
- Acceptance criteria:
  - Documentation and tests consistently treat transport fields as opaque metadata (or remove usage, if chosen in implementation).
  - No regression in existing SAR runtime tests.
- Verifier checks:
  - Negative tests for identity derivation from transport fields.
  - Existing SAR runtime and CI lane tests remain green.
- Scope class: CI-safe
- Risk level: Medium

### PR3

- Title: Consolidate `ElapsedUs` and Diagnostics Sink Resolver Helpers
- Purpose: Reduce duplicated helper logic without changing SAR algorithm behavior.
- Files likely touched:
  - `examples/SAR/src/main.cpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `examples/SAR/src/H2DAsyncAccelNode.cpp`
  - `examples/SAR/src/D2HAsyncAccelNode.cpp`
  - `examples/SAR/src/RangeCompressionNode.cpp`
  - `examples/SAR/src/RangeWindowNode.cpp`
  - `examples/SAR/src/AzimuthTileSplitNode.cpp`
  - `examples/SAR/src/ImageTileMergeNode.cpp`
  - `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
  - `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
  - SAR tests currently duplicating diagnostics sink resolver helpers
- Files likely deleted:
  - Duplicate local helper definitions replaced by shared helper(s).
- Tests to add:
  - Minimal coverage for shared helper behavior.
- Tests to update/delete:
  - Update SAR tests to use shared resolver helper.
- Acceptance criteria:
  - Zero duplicated `ElapsedUs(...)` helper definitions in SAR sources.
  - Single shared diagnostics resolver usage path for main, benchmark, and tests.
  - No behavior change in SAR test outcomes.
- Verifier checks:
  - Search confirms helper deduplication.
  - SAR example unit target passes.
- Scope class: CI-safe
- Risk level: Medium

### PR4

- Title: Strengthen Tiny Fixture Correctness Assertions
- Purpose: Move from scaffolding-only validation toward meaningful SAR correctness checks while staying CI-safe.
- Files likely touched:
  - `examples/SAR/test/test_rrp6_tiny_fixture.cpp`
  - `examples/SAR/test/test_rrp7_ci_validation_lane.cpp`
  - `examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json`
  - `examples/SAR/test/sar_pr7_parity_fixture.hpp`
- Files likely deleted:
  - Superseded weak assertions, if any.
- Tests to add:
  - Deterministic output checks beyond existence: stable dimensions, finite-value constraints, bounded metrics, deterministic EOS/merge invariants.
- Tests to update/delete:
  - Tighten permissive tolerance checks where needed.
- Acceptance criteria:
  - CI lane remains bounded and deterministic.
  - Tiny fixture checks validate output properties, not only orchestration completion.
- Verifier checks:
  - Repeat runs produce stable pass/fail outcomes.
  - Existing RRP7 CI lane remains green.
- Scope class: CI-safe
- Risk level: Medium

### PR5

- Title: Local Runner-to-Comparator Integration for `scenario_001` Artifacts
- Purpose: Close the RRP4 non-blocking gap by wiring scenario-driven artifact production to comparator invocation in one local-only reproducible flow.
- Files likely touched:
  - `examples/SAR/tools/rrp1_local_runner.py`
  - `examples/SAR/tools/rrp4_image_comparator.py`
  - `examples/SAR/tools/rrp5_frozen_scenario_replay.md`
  - `examples/SAR/test/test_rrp4_image_comparator.cpp`
  - `examples/SAR/test/test_rrp5_frozen_scenario_replay.cpp`
- Files likely deleted:
  - None
- Tests to add:
  - Local-only integration validation proving produced GraphX/reference artifact contracts feed comparator directly.
- Tests to update/delete:
  - Update replay documentation test expectations if command contract changes.
- Acceptance criteria:
  - One documented command path (or short sequence) materializes both artifact contracts and runs comparator with structured pass/fail output.
  - CI does not require external dataset download.
- Verifier checks:
  - Local-only runbook is reproducible without reverse engineering.
  - Comparator report schema and deterministic metrics remain preserved.
- Scope class: Local-only (with CI-safe doc/test guards)
- Risk level: Medium

## 3) Items Explicitly Deferred

- Native performance optimization and tuning beyond no-regression checks.
- Any new baseline policy/registry governance work.
- Full external package integration changes that alter GraphX architecture shape.
- Large-scale topology redesign.
- SAR math rewrites.
- External data requirements in CI.

## 4) Things Not To Do

- Do not create another baseline registry PR.
- Do not combine cleanup, SAR correctness, and performance work in one PR.
- Do not add SarPy, ISCE3, or gotcha-back integration unless directly tied to artifact comparison or reproduction testing.
- Do not introduce compatibility shims for obsolete helper types.
- Do not alter the canonical `AccelControlToken<SarSidecar>` contract while doing helper cleanup.
