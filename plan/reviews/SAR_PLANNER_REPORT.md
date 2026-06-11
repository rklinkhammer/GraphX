# SAR Planner Report

Scope: Current repository state only, using `plan/reviews/SAR_INSPECTOR_REPORT.md` as the sole inspection input. No implementation included.

Role: `PLANNER` per `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

## Target State Summary

The repository already has the core SAR accel-token model in place: `SarAccelControlToken = AccelControlToken<SarSidecar>`, active SAR nodes use that token, sidecar identity is documented, and legacy SAR payload contracts are rejected under `edge_contract: accel-token`.

The remaining target is cleanup and validation around the existing architecture: remove ambiguity around generic resolver token labels, centralize duplicated synthetic transport helpers, keep compatibility aliases on a planned migration path, reduce merge/benchmark complexity where it blocks clarity, and make external baseline hooks explicit without pulling external package assumptions into GraphX core.

## Cleanup Classification

- Required cleanup:
  - Align resolver contract vocabulary with `SarAccelControlToken` where SAR graphs currently depend on generic `HostPinnedBufferView` / `DeviceBufferView` labels.
  - Add or strengthen tests that prove resolver/Metal substitution preserves `SarSidecar`.
  - Centralize synthetic opaque transport helper behavior so pointer/event metadata cannot become accidental identity.

- Compatibility-preserving cleanup:
  - Keep `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` aliases for now, but document and test their mapping to accel-token implementations.
  - Preserve current JSON graph behavior while making the accel-token contract more explicit.

- Optional follow-up:
  - Split benchmark/reporting concerns out of `sar_benchmark.cpp`.
  - Isolate `ImageTileMergeNode` diagnostics/ticket synthesis from merge semantics if future changes require it.
  - Add local-only external baseline execution for gotcha-back, OpenSAR, or OpenSARLab only after the current comparison harness boundary is explicit.

## PR1: Make SAR Resolver Contracts Explicit

- Title: Explicit SAR accel-token resolver contract labels
- Purpose / Goal: Remove the ambiguity identified by the inspector where SAR runtime payloads are `SarAccelControlToken`, but resolver mappings still describe generic view-level contracts.
- Category: Required cleanup
- Files likely affected:
  - `examples/SAR/config/sar_stripmap_definitive.json`
  - `libgraph/src/graph/GraphConfigParser.cpp`
  - `libgraph/src/graph/NodeResolutionRegistry.cpp`
  - Existing SAR resolver/runtime tests under `examples/SAR/test`
  - Existing graph config parser tests under `libgraph/test/unit`
- Files to delete: None planned.
- Specific changes to make:
  - Introduce explicit SAR accel-token contract labels in SAR resolver mappings, for example `SarAccelControlToken`, while preserving the current runtime node sequence.
  - Keep generic GPU labels available for non-SAR generic GPU mappings.
  - Ensure `edge_contract: accel-token` accepts the canonical SAR token label and still rejects legacy SAR payload names.
  - Preserve existing compatibility aliases in config-facing names for this PR.
- Tests to add:
  - Parser test accepting SAR accel-token resolver labels.
  - Parser test rejecting legacy SAR payload labels under `edge_contract: accel-token`.
  - Runtime resolver test proving SAR H2D / kernel / D2H resolution still selects the intended node implementation with SAR token labels.
- Tests to delete: None planned.
- Tests or verification to run:
  - SAR unit test binary.
  - Graph config parser unit tests.
  - `examples/SAR/main.cpp` executable path if currently buildable in the local configuration.
- Acceptance criteria:
  - Definitive SAR config no longer depends on generic view-label vocabulary for SAR accel-token edges.
  - Existing SAR runtime behavior is preserved.
  - Legacy payload guardrails still reject obsolete SAR message contracts.
- Risks:
  - Resolver mappings may be shared by generic GPU tests that expect view-level names.
  - JSON fixtures may need synchronized updates.
- Rollback plan:
  - Revert resolver-label changes and restore previous JSON mappings; no type-model rollback should be required.
- Dependencies: None.
- CI-safe or local-only: CI-safe.

## PR2: Centralize Opaque Transport Helper Semantics

- Title: Centralize SAR opaque transport helpers
- Purpose / Goal: Address the inspector finding that synthetic host pointers, device pointers, and event IDs are scattered across nodes, making future identity coupling more likely.
- Category: Required cleanup
- Files likely affected:
  - `examples/SAR/include/sar/SarRuntimeHelpers.hpp`
  - `examples/SAR/src/AzimuthTileSplitNode.cpp`
  - `examples/SAR/src/H2DAsyncAccelNode.cpp`
  - `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
  - `examples/SAR/src/D2HAsyncAccelNode.cpp`
  - `examples/SAR/src/ImageTileMergeNode.cpp`
  - Existing token/transport tests under `examples/SAR/test`
- Files to delete: None planned.
- Specific changes to make:
  - Move duplicated opaque host pointer, synthetic device pointer, and opaque event generation helpers into a single SAR runtime helper location.
  - Keep generated values semantically opaque and transport-only.
  - Keep sidecar identity unchanged by all helper use.
  - Avoid changing public node names or graph topology.
- Tests to add:
  - Helper-level tests for opaque host pointer/event/device pointer behavior.
  - Node-path regression test that sidecar identity remains invariant when transport metadata changes.
- Tests to delete: None planned.
- Tests or verification to run:
  - SAR accel token tests.
  - SAR transport opaque contract tests.
  - SAR accel node tests.
- Acceptance criteria:
  - No SAR node owns private identity-like pointer/event generation logic.
  - Existing transport opacity tests still pass.
  - No runtime behavior change except helper consolidation.
- Risks:
  - Existing tests may assert exact synthetic values.
  - Centralizing counters can accidentally change determinism if sequence ownership is altered.
- Rollback plan:
  - Restore node-local helper implementations while keeping transport opacity tests.
- Dependencies: PR1 preferred but not strictly required.
- CI-safe or local-only: CI-safe.

## PR3: Add Sidecar Preservation Coverage For Resolver And Metal Paths

- Title: Resolver and Metal sidecar preservation tests
- Purpose / Goal: Close the blocker that SAR accel-token compatibility currently depends on wrapper aliases and test discipline around generic resolver behavior.
- Category: Required cleanup
- Files likely affected:
  - `examples/SAR/test/test_sar_accel_nodes.cpp`
  - `examples/SAR/test/test_sar_json_runtime.cpp`
  - Existing Metal-capability test fixtures, if present
  - `examples/SAR/config/sar_stripmap_definitive.json` only if test fixtures need explicit mapping updates
- Files to delete: None planned.
- Specific changes to make:
  - Add test coverage that resolved SAR H2D, backprojection, and D2H paths preserve `SarSidecar`.
  - Add native Metal path coverage for sidecar preservation where Metal capabilities are available or mockable.
  - Assert that `host_ptr` and `ready_event` changes do not alter SAR identity after resolver substitution.
- Tests to add:
  - Resolver-selected SAR H2D/kernel/D2H sidecar preservation test.
  - Native Metal backprojection sidecar preservation test or capability-gated equivalent.
- Tests to delete: None planned.
- Tests or verification to run:
  - SAR JSON runtime tests.
  - SAR accel node tests.
  - Metal-related tests when available locally.
- Acceptance criteria:
  - Resolver-selected implementations preserve sidecar identity.
  - Native and synthetic paths have equivalent sidecar identity semantics.
  - Tests fail if SAR identity is reintroduced through transport fields.
- Risks:
  - Metal test availability may vary by developer machine and CI environment.
  - Capability gating must not hide all meaningful assertions in CI.
- Rollback plan:
  - Revert new tests and retain existing transport opacity tests.
- Dependencies: PR1 recommended.
- CI-safe or local-only: CI-safe for stub/synthetic resolver coverage; Metal-specific coverage may require capability gating.

## PR4: Put Compatibility Aliases On An Explicit Migration Path

- Title: Document and test SAR compatibility alias boundaries
- Purpose / Goal: Address obsolete public node names without removing them abruptly. The inspector identified aliases as compatibility scaffolding, not independent runtime paths.
- Category: Compatibility-preserving cleanup
- Files likely affected:
  - Alias headers in `examples/SAR/include/sar`
  - `examples/SAR/config/sar_stripmap_definitive.json`
  - SAR token contract tests under `examples/SAR/test`
  - Any local SAR docs that list definitive node names
- Files to delete: None in this PR.
- Specific changes to make:
  - Add explicit tests documenting that compatibility aliases resolve to accel-token implementations.
  - Mark config-facing old names as compatibility names in documentation or comments where the repository convention allows it.
  - Define the future removal criteria for aliases without deleting them in this PR.
- Tests to add:
  - Compile-time alias identity checks if not already complete.
  - Config/runtime test that old names and canonical accel-token implementations do not diverge.
- Tests to delete: None planned.
- Tests or verification to run:
  - SAR token contract tests.
  - SAR JSON runtime tests.
- Acceptance criteria:
  - Alias behavior is explicitly covered.
  - No second SAR GPU path exists through alias names.
  - Future alias removal has clear prerequisites.
- Risks:
  - Comments/docs can drift if not tied to tests.
  - Removing aliases too early would break existing configs, so this PR must avoid deletion.
- Rollback plan:
  - Remove added alias-boundary documentation/tests; runtime code remains unchanged.
- Dependencies: PR1 and PR3 recommended.
- CI-safe or local-only: CI-safe.

## PR5: Split Benchmark And Main Validation Responsibilities

- Title: Clarify SAR benchmark, main execution, and performance reporting boundaries
- Purpose / Goal: Address the inspector hotspot around `sar_benchmark.cpp` and the role requirement that `examples/SAR/main.cpp` be tested and report performance metrics.
- Category: Required cleanup for validation clarity; optional for architecture if PR1-PR4 already satisfy token blockers.
- Files likely affected:
  - `examples/SAR/src/main.cpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `examples/SAR/test/CMakeLists.txt`
  - Existing SAR runtime/CI lane tests
- Files to delete: None in the first validation PR.
- Specific changes to make:
  - Add direct test coverage for the `examples/SAR/main.cpp` executable path.
  - Ensure main execution reports minimal performance metrics already available from the token/diagnostics path.
  - Keep detailed benchmark and graph-vs-direct comparison logic in benchmark-specific code.
  - Do not optimize or change SAR algorithm behavior.
- Tests to add:
  - CTest entry or equivalent test that runs the SAR example executable.
  - Output assertion for completion and minimal metrics.
- Tests to delete: None planned.
- Tests or verification to run:
  - SAR example executable.
  - SAR example unit tests.
  - Existing benchmark test/build target if available.
- Acceptance criteria:
  - `examples/SAR/main.cpp` is test-covered.
  - Main reports basic performance/diagnostic metrics without absorbing benchmark complexity.
  - Existing benchmark behavior is preserved.
- Risks:
  - Runtime output assertions can become brittle.
  - Performance metrics may differ across backend/capability environments.
- Rollback plan:
  - Remove the new executable test and output assertions; leave benchmark code unchanged.
- Dependencies: PR1-PR3 preferred, because resolver/token behavior should be stable first.
- CI-safe or local-only: CI-safe if using tiny deterministic config/fixture and stable output matching.

## PR6: Bound Merge Diagnostics And Ticket Synthesis

- Title: Isolate merge semantics from accel-ticket diagnostics
- Purpose / Goal: Reduce the inspector-identified complexity in `ImageTileMergeNode`, where merge semantics, diagnostics aggregation, ticket synthesis, ordering checks, and sidecar finalization are combined.
- Category: Optional follow-up unless future PRs need to modify merge behavior.
- Files likely affected:
  - `examples/SAR/src/ImageTileMergeNode.cpp`
  - `examples/SAR/include/sar/SarMessages.hpp`
  - Merge-related tests under `examples/SAR/test`
- Files to delete: None initially.
- Specific changes to make:
  - Separate pure sidecar merge finalization from diagnostic/ticket construction using local helper functions or existing helper patterns.
  - Preserve emitted token fields and current runtime behavior.
  - Keep all SAR-specific behavior in `examples/SAR`, not `libgpu`.
- Tests to add:
  - Merge sidecar finalization regression test.
  - Ticket/diagnostic field preservation test.
- Tests to delete: None planned.
- Tests or verification to run:
  - Merge node tests.
  - SAR materialized/replay tests that consume merge output.
- Acceptance criteria:
  - Merge output token is behaviorally unchanged.
  - Sidecar finalization can be reasoned about independently from diagnostic ticket synthesis.
- Risks:
  - Refactoring merge code can introduce ordering or completion regressions.
  - Over-abstraction could obscure the currently direct flow.
- Rollback plan:
  - Revert helper extraction and restore the single implementation body.
- Dependencies: PR2 recommended.
- CI-safe or local-only: CI-safe.

## PR7: Make External Baseline Boundary Explicit Before Adding More Baselines

- Title: External SAR baseline boundary documentation and harness checks
- Purpose / Goal: Address only the external baseline gap that affects architecture cleanup: current hooks exist, but external execution is not fully wired and OpenSAR/OpenSARLab are not present.
- Category: Optional follow-up for architecture; required before any new external baseline substitution work.
- Files likely affected:
  - `plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md`
  - `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json`
  - `examples/SAR/tools/rrp3_gotcha_back_adapter.py`
  - `examples/SAR/tools/rrp4_image_comparator.py`
  - Baseline-related tests under `examples/SAR/test`
- Files to delete: None planned.
- Specific changes to make:
  - Document that external packages compare artifacts only and must not define GraphX core token contracts.
  - Add harness checks that distinguish deterministic generated references from externally executed baselines.
  - Add OpenSAR/OpenSARLab registry entries only as uninstalled/survey candidates unless a later PR justifies actual local-only adapters.
- Tests to add:
  - Comparator contract test confirming artifact-level comparison boundaries.
  - Registry validation test if registry schema is enforced in repo.
- Tests to delete: None planned.
- Tests or verification to run:
  - Existing RRP baseline/comparator tests.
  - JSON/schema validation for baseline registry if available.
- Acceptance criteria:
  - The repo clearly distinguishes deterministic internal references from external package outputs.
  - No external package assumptions leak into `AccelControlToken<SarSidecar>` or resolver contracts.
  - No new dependency is required for CI.
- Risks:
  - Baseline policy can become documentation-only unless paired with harness assertions.
  - Adding OpenSAR/OpenSARLab as candidates may be mistaken for supported execution.
- Rollback plan:
  - Remove new registry entries and policy text; comparator behavior remains unchanged.
- Dependencies: PR1-PR5 preferred.
- CI-safe or local-only: CI-safe for documentation/harness boundary checks; actual external execution remains local-only future work.

## Deferred Local-Only Work

- Title: Optional local OpenSAR/OpenSARLab or gotcha-back execution adapter
- Purpose: Add real external execution only after GraphX SAR token architecture, resolver vocabulary, main execution validation, and artifact comparison boundaries are stable.
- Files likely affected:
  - New adapter under `examples/SAR/tools`
  - Baseline package registry
  - Local-only documentation/tests
- Tests to add:
  - Local-only adapter smoke test gated on installed package/data.
- Acceptance criteria:
  - Adapter produces artifact-compatible output for the existing comparator.
  - GraphX core contracts remain unchanged.
- CI-safe or local-only: Local-only.

## Recommended PR Order

1. PR1: Make SAR resolver contracts explicit.
2. PR2: Centralize opaque transport helper semantics.
3. PR3: Add sidecar preservation coverage for resolver and Metal paths.
4. PR4: Put compatibility aliases on an explicit migration path.
5. PR5: Split benchmark and main validation responsibilities.
6. PR6: Bound merge diagnostics and ticket synthesis.
7. PR7: Make external baseline boundary explicit before adding more baselines.

This order removes the most dangerous ambiguity first: SAR accel-token edges currently work, but resolver vocabulary still points at generic view payloads. External package work is deliberately last and bounded to comparison artifacts.
