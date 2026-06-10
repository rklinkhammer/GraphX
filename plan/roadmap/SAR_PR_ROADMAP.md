# SAR PR Roadmap

Date: 2026-06-09
Planner inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md

Planning constraints:
- Each PR compiles independently
- Each PR adds or updates tests
- No compatibility shims
- One concern per PR where possible
- Remove the most dangerous ambiguity first

## Scope Summary
Current state (from Inspector + Simplifier):
- Canonical token type exists but runtime path is mixed between token and legacy SAR message types.
- Host pointer and event channels must remain transport-only, never identity carriers.
- Side-channel payload store is non-canonical and should be removed from the primary path.
- Canonical target is one SAR GPU flow using AccelControlToken<SarSidecar> across the core path.

## PR1
Title: Remove Encoded Host Pointer and Event Identity
Purpose:
- Eliminate the highest-risk ambiguity first by enforcing sidecar-only SAR identity.

Files to touch:
- examples/SAR/src/AzimuthTileSplitNode.cpp
- examples/SAR/src/D2HAsyncAccelNode.cpp
- examples/SAR/src/H2DAsyncAccelNode.cpp
- examples/SAR/src/SarBackprojectionTransformAccelNode.cpp
- examples/SAR/test/test_sar_accel_nodes.cpp
- examples/SAR/test/test_sar_token_contract.cpp

Files to delete:
- None expected

Tests to add:
- Invariance tests proving sequence/stream/tile identity is unchanged when host_ptr changes.
- Invariance tests proving identity is unchanged when ready_event changes.
- Negative tests proving no node reconstructs SAR identity from host_ptr or ready_event.

Tests to delete:
- Any tests that assert pointer/event-derived identity behavior.

Acceptance criteria:
- No identity derivation from host_ptr or ready_event remains in canonical SAR path.
- SAR identity remains sidecar-only in runtime behavior and tests.
- Build and SAR unit tests pass.

Risks:
- Hidden assumptions in merge or diagnostics around opaque transport fields.

Rollback plan:
- Revert PR1 commit only; no schema or topology changes included.

CI-safe or local-only:
- CI-safe

## PR2
Title: Tokenize Source and DSP Pre-GPU Stages
Purpose:
- Convert source/range stages from SarPulseBlockMessage boundaries to AccelControlToken<SarSidecar> boundaries in canonical path.

Files to touch:
- examples/SAR/src/SyntheticApertureIqSourceNode.cpp
- examples/SAR/src/RangeWindowNode.cpp
- examples/SAR/src/RangeCompressionNode.cpp
- examples/SAR/src/AzimuthTileSplitNode.cpp
- examples/SAR/include/sar/SarMessages.hpp
- examples/SAR/config/sar_stripmap_definitive.json
- examples/SAR/test/test_sar_json_pipeline.cpp
- examples/SAR/test/test_sar_json_runtime.cpp

Files to delete:
- None in this PR (deletion deferred to legacy cleanup PR)

Tests to add:
- Topology tests that validate token contract continuity from source through split.
- Node contract tests for sidecar initialization at source and preservation through DSP stages.

Tests to delete:
- Tests that require message-to-token boundary in canonical path.

Acceptance criteria:
- Canonical definitive topology uses token contract through source and DSP-to-GPU handoff.
- PR compiles and tests pass without compatibility shims.

Risks:
- Contract churn across multiple early nodes.

Rollback plan:
- Revert PR2 only; PR1 invariants remain intact.

CI-safe or local-only:
- CI-safe

## PR3
Title: Tokenize Merge and Diagnostics Boundaries
Purpose:
- Remove non-canonical message exit at merge/diagnostics boundaries in definitive path.

Files to touch:
- examples/SAR/src/ImageTileMergeNode.cpp
- examples/SAR/src/SarDiagnosticsSinkNode.cpp
- examples/SAR/include/sar/SarMessages.hpp
- examples/SAR/config/sar_stripmap_definitive.json
- examples/SAR/test/test_sar_json_pipeline.cpp
- examples/SAR/test/test_sar_accel_nodes.cpp

Files to delete:
- None in this PR

Tests to add:
- Merge/diagnostics tests validating sidecar-derived output semantics from token input only.
- Regression tests confirming no fallback identity derivation outside sidecar.

Tests to delete:
- Tests tied exclusively to SarMergeStatusMessage/SarDiagnosticsMessage as canonical edge contracts.

Acceptance criteria:
- Definitive runtime path remains tokenized through merge/diagnostics boundary.
- Diagnostics and metrics still emitted with unchanged semantic meaning.

Risks:
- Output/report format coupling in benchmark and sink tests.

Rollback plan:
- Revert PR3 only; upstream tokenization remains.

CI-safe or local-only:
- CI-safe

## PR4
Title: Remove Side-Channel Payload Store from Primary Path
Purpose:
- Eliminate global token-id keyed payload store and enforce explicit token-carried handoff contract.

Files to touch:
- examples/SAR/src/SarBackprojectionTransformAccelNode.cpp
- examples/SAR/src/SarMaterializedImageSinkNode.cpp
- examples/SAR/include/sar/SarMessages.hpp
- examples/SAR/test/test_sar_accel_nodes.cpp
- examples/SAR/test/test_sar_baseline_compare.cpp

Files to delete:
- Side-channel store definitions and helpers used as primary transport path.

Tests to add:
- Contract tests proving materialized image sink behavior works without global store.
- Tests asserting canonical token path is the only runtime data path.

Tests to delete:
- Tests that rely on global map lookups by token id.

Acceptance criteria:
- No primary-path reliance on global sidecar/payload store.
- Materialization behavior preserved through explicit contracts.

Risks:
- Larger payload movement may expose latent performance assumptions.

Rollback plan:
- Revert PR4 only; token identity invariants remain.

CI-safe or local-only:
- CI-safe

## PR5
Title: Delete Obsolete SAR Message Abstractions
Purpose:
- Remove non-canonical legacy edge abstractions after token path is complete.

Files to touch:
- examples/SAR/include/sar/SarMessages.hpp
- examples/SAR/src/RangeWindowNode.cpp
- examples/SAR/src/RangeCompressionNode.cpp
- examples/SAR/src/ImageTileMergeNode.cpp
- examples/SAR/src/SarDiagnosticsSinkNode.cpp
- examples/SAR/test/test_sar_json_pipeline.cpp
- examples/SAR/test/test_sar_token_contract.cpp

Files to delete:
- Legacy type definitions no longer referenced by definitive runtime path.
- Deprecated SAR definitive config variants when no longer needed:
  - examples/SAR/config/sar_stripmap_definitive_nonmetal.json
  - examples/SAR/config/sar_stripmap_definitive_metal.json

Tests to add:
- Build-time and parser/runtime tests confirming definitive topology references only canonical token contracts.

Tests to delete:
- Tests validating obsolete runtime message contracts.

Acceptance criteria:
- Removed legacy message abstractions are absent from definitive runtime path.
- Parser negative-validation artifacts may remain as strings only where intentionally required for guardrails.

Risks:
- Accidental removal of useful negative-validation test fixtures.

Rollback plan:
- Revert PR5 only.

CI-safe or local-only:
- CI-safe

## PR6
Title: Resolver and Schema Guardrails for Accel Token Contract
Purpose:
- Harden parser and resolver behavior around edge_contract=accel-token and legacy contract rejection.

Files to touch:
- libgraph/src/graph/GraphConfigParser.cpp
- examples/SAR/test/test_sar_accel_token_guardrails.cpp
- examples/SAR/test/test_sar_json_runtime.cpp

Files to delete:
- None expected

Tests to add:
- Parser guardrail tests for legacy contract rejection under accel-token mode.
- Resolver tests for strict/allow_fallback behavior with definitive SAR config.

Tests to delete:
- Overlapping obsolete guardrail tests replaced by stricter cases.

Acceptance criteria:
- Guardrails enforce canonical contract and fail fast on forbidden legacy edges.
- Resolver behavior remains explicit and deterministic.

Risks:
- Over-constraining parser may break non-SAR tests if generalized too far.

Rollback plan:
- Revert PR6 only.

CI-safe or local-only:
- CI-safe

## PR7
Title: Sidecar Preservation Across Metal and Resolver Matrix
Purpose:
- Validate canonical token sidecar preservation across backend selection and provider substitution.

Files to touch:
- examples/SAR/test/test_sar_json_runtime.cpp
- examples/SAR/test/test_sar_accel_nodes.cpp
- examples/SAR/config/sar_stripmap_definitive.json

Files to delete:
- None expected

Tests to add:
- Matrix tests for metal/auto/fallback preserving sidecar identity and timing semantics.
- Dynamic loading/resolver substitution tests that verify no sidecar loss.

Tests to delete:
- Redundant tests superseded by matrix coverage.

Acceptance criteria:
- Sidecar content remains stable across supported backend selection paths.
- GraphX dynamic loading/resolver behavior remains intact.

Risks:
- Test matrix expansion may increase CI duration.

Rollback plan:
- Revert PR7 only.

CI-safe or local-only:
- CI-safe

## PR8
Title: Main Entry and Metrics Contract Hardening
Purpose:
- Ensure examples/SAR/src/main.cpp remains executable reference and emits required metrics contract.

Files to touch:
- examples/SAR/src/main.cpp
- examples/SAR/src/sar_benchmark.cpp
- examples/SAR/test/test_sar_trace_schema.cpp
- examples/SAR/test/test_sar_json_runtime.cpp

Files to delete:
- None expected

Tests to add:
- Tests proving required benchmark attribution and trace fields are present and stable.
- Smoke test for main entry against definitive config path.

Tests to delete:
- Any legacy trace-field tests tied to pointer/event identity semantics.

Acceptance criteria:
- main entry remains canonical, test-covered, and metrics-producing.
- Trace schema reflects canonical identity semantics.

Risks:
- Tooling that parses existing traces may need coordinated updates.

Rollback plan:
- Revert PR8 only.

CI-safe or local-only:
- CI-safe

## PR9
Title: CPU Reference Validation Lane
Purpose:
- Lock correctness baseline against deterministic CPU reference after canonical path cleanup.

Files to touch:
- examples/SAR/src/sar_benchmark.cpp
- examples/SAR/test/test_sar_baseline_compare.cpp
- examples/SAR/BENCHMARK_REPORT.md

Files to delete:
- None expected

Tests to add:
- Deterministic fixture parity tests with explicit tolerance policy.

Tests to delete:
- Obsolete baseline tests that depended on pre-canonical behavior.

Acceptance criteria:
- Deterministic parity checks pass in CI-safe lane.
- Baseline comparison is repeatable and documented.

Risks:
- Tight tolerances may be brittle across environments.

Rollback plan:
- Revert PR9 only.

CI-safe or local-only:
- CI-safe

## PR10
Title: Native Metal Parity Finalization
Purpose:
- Finalize parity evidence for native metal on the single canonical SAR path.

Files to touch:
- examples/SAR/src/SarBackprojectionTransformAccelNode.cpp
- examples/SAR/config/sar_stripmap_definitive.json
- examples/SAR/test/test_sar_accel_nodes.cpp
- examples/SAR/test/test_sar_json_runtime.cpp

Files to delete:
- Residual dual-path artifacts discovered during parity closure.

Tests to add:
- Metal parity tests against canonical reference thresholds.

Tests to delete:
- Tests that validate dual-path transitional behavior.

Acceptance criteria:
- Definitive topology is the single canonical SAR GPU runtime path.
- Full SAR CTest lane passes with Metal-first policy preserved.

Risks:
- Platform-specific nondeterminism in backend timing and ordering.

Rollback plan:
- Revert PR10 only.

CI-safe or local-only:
- CI-safe

---

## External Baseline Follow-On Roadmap (After PR1-PR10)

### EB1
Title: External SAR Baseline Survey and Selection
Purpose: Select one primary and two secondary baseline packages with licensing and CI constraints.
CI-safe or local-only: CI-safe (documentation and selection only)

### EB2
Title: Baseline Runner Scaffolding (Local-Only)
Purpose: Add local-only runner script for selected external baseline.
CI-safe or local-only: Local-only

### EB3
Title: GraphX vs Baseline Comparison Harness
Purpose: Add artifact-level comparison harness with stable metric schema.
CI-safe or local-only: CI-safe (with tiny fixture mode)

### EB4
Title: Tiny Deterministic Fixture Comparison
Purpose: Add small deterministic fixture and CI thresholds.
CI-safe or local-only: CI-safe

### EB5
Title: CI-Safe Derived Fixture Expansion
Purpose: Expand fixture if licensing allows, keep runtime bounded.
CI-safe or local-only: CI-safe

### EB6
Title: Optional Local Gotcha/OpenSAR Benchmark
Purpose: Add optional benchmark lane using larger external data.
CI-safe or local-only: Local-only

### EB7
Title: Substitution Experiment Boundary
Purpose: Evaluate one bounded stage substitution without polluting GraphX core contracts.
CI-safe or local-only: Local-only (initial), promote selective checks later

### EB8
Title: External Baseline Governance Guardrails
Purpose: Ensure external package assumptions do not leak into core GraphX architecture.
CI-safe or local-only: CI-safe

---

## Cross-PR Build and Test Policy
Each PR must include in its description:
- Build command(s) run
- Test command(s) run
- Exact test files added/updated/removed
- Confirmation that no compatibility shim was introduced
- Confirmation that unchanged future-PR scope was not modified

## Dependency and Ordering Notes
- PR1 is mandatory first.
- PR2 and PR3 can proceed sequentially (PR3 depends on PR2 outcomes).
- PR4 depends on PR2-PR3 token continuity.
- PR5 depends on PR2-PR4 completion.
- PR6 and PR7 may overlap after PR5 but should land separately.
- PR8 follows PR6-PR7 to lock entrypoint and metrics contracts.
- PR9 and PR10 finalize correctness and backend parity respectively.
