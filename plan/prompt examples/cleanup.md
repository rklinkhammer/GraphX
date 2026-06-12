Act as PLANNER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Use `plan/reviews/SAR_INSPECTOR_REPORT.md` as the sole inspection input. Analyze the current repository only.

Your task is to produce an implementation plan that addresses the inspector report’s findings in these areas:

1. Violations of accel-token architecture
2. Obsolete abstractions
3. Complexity hotspots that block clean accel-token architecture
4. Blockers for `AccelControlToken<SarSidecar>`
5. External comparison/baseline gaps only where they affect the above

Do not implement. Do not redesign beyond what is necessary to produce an actionable plan.

Planner output requirements:

- Start with a short summary of the current target state.
- Group work into small PR-sized phases.
- For each phase, include:
  - Goal
  - Files likely affected
  - Specific changes to make
  - Tests or verification to run
  - Risks
  - Dependencies on earlier phases
- Explicitly distinguish:
  - Required cleanup
  - Compatibility-preserving cleanup
  - Optional follow-up
- Preserve current runtime behavior unless the inspector report identifies it as a violation or blocker.
- Do not propose removing compatibility aliases unless you first plan a safe migration path.
- Do not propose replacing the SAR graph architecture wholesale.
- Do not add new external SAR dependencies unless the plan explains why the existing hooks are insufficient.
- Keep all recommendations grounded in observed findings from `SAR_INSPECTOR_REPORT.md`.

Stop after the planner report.
Save the report as `plan/reviews/SAR_PLANNER_REPORT.md`.

======

Act as IMPLEMENTER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implement only PR1 from `plan/reviews/SAR_PLANNER_REPORT.md`: **Make SAR Resolver Contracts Explicit**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`

Scope:
- Current repository only.
- Implement PR1 only.
- Do not implement PR2 or later.
- Do not redesign the SAR graph architecture.
- Preserve current runtime behavior.
- Preserve compatibility aliases for:
  - `H2DAsyncNode`
  - `D2HAsyncNode`
  - `SarBackprojectionTransformNode`
- Do not add external SAR dependencies.
- Do not remove existing tests unless they are directly superseded by PR1 changes.

PR1 goal:
Make SAR resolver contract vocabulary explicit so SAR accel-token edges no longer depend on generic `HostPinnedBufferView` / `DeviceBufferView` labels, while keeping generic GPU mappings available for non-SAR paths.

Required work:
1. Inspect the current resolver/parser/config/test code relevant to PR1.
2. Update SAR resolver mappings/config so SAR edges use explicit SAR accel-token contract labels, such as `SarAccelControlToken`.
3. Keep generic GPU resolver labels available for non-SAR mappings.
4. Ensure `edge_contract: accel-token` accepts canonical SAR token labels.
5. Ensure legacy SAR payload labels are still rejected under `edge_contract: accel-token`.
6. Add or update tests proving:
   - SAR accel-token resolver labels are accepted.
   - Legacy SAR payload labels are rejected.
   - SAR H2D / kernel / D2H resolution still selects the intended implementation using SAR token labels.
7. Run the focused relevant tests:
   - Graph config parser tests.
   - SAR resolver/runtime tests.
   - SAR unit test binary if available.
   - `examples/SAR/main.cpp` executable path if buildable.

Files likely affected:
- `examples/SAR/config/sar_stripmap_definitive.json`
- `libgraph/src/graph/GraphConfigParser.cpp`
- `libgraph/src/graph/NodeResolutionRegistry.cpp`
- SAR resolver/runtime tests under `examples/SAR/test`
- Graph config parser tests under `libgraph/test/unit`

Acceptance criteria:
- Definitive SAR config no longer depends on generic view-label vocabulary for SAR accel-token edges.
- Existing SAR runtime behavior is preserved.
- Generic GPU mappings remain available for non-SAR use.
- Legacy SAR payload guardrails still reject obsolete SAR message contracts.
- Tests cover the new SAR token resolver labels and legacy rejection behavior.

Stop after PR1 is implemented and verified.
Report:
- Files changed.
- Tests run and results.
- Any risks or follow-up left for later PRs.

======

Act as VERIFIER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Verify only PR1: **Make SAR Resolver Contracts Explicit**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_IMPL_PR1_1.md`

Scope:
- Current repository only.
- Verify PR1 only.
- Do not redesign.
- Do not implement new functionality.
- Do not verify PR2 or later except to confirm they remain untouched.
- Preserve current runtime behavior.
- Treat unrelated dirty working-tree changes as out of scope unless they affect PR1 verification.

PR1 acceptance criteria to verify:
1. Definitive SAR config no longer depends on generic view-label vocabulary for SAR accel-token edges.
2. Existing SAR runtime behavior is preserved.
3. Generic GPU mappings remain available for non-SAR use.
4. Legacy SAR payload guardrails still reject obsolete SAR message contracts.
5. Tests cover the new SAR token resolver labels and legacy rejection behavior.
6. Compatibility aliases remain preserved for:
   - `H2DAsyncNode`
   - `D2HAsyncNode`
   - `SarBackprojectionTransformNode`
7. No external SAR dependencies were added.
8. No PR2+ work was implemented.

Verification tasks:
1. Inspect changed PR1 files and relevant resolver/parser/config/test code.
2. Confirm SAR JSON resolver mappings use `SarAccelControlToken` where appropriate.
3. Confirm generic GPU resolver defaults still use generic view labels where appropriate.
4. Confirm parser accepts `SarAccelControlToken` resolver labels under `edge_contract: accel-token`.
5. Confirm parser still rejects legacy SAR payload labels under `edge_contract: accel-token`.
6. Confirm SAR resolver/runtime tests assert `SarAccelControlToken` diagnostics for H2D / kernel / D2H.
7. Confirm compatibility aliases were not removed.
8. Confirm no external baseline/package dependency was added.
9. Run focused verification commands:
   - Build affected targets if needed.
   - Graph config parser and resolving provider tests.
   - SAR JSON/runtime and accel-token guardrail tests.
   - Full SAR unit test binary if available.
   - `examples/SAR/main.cpp` executable path if buildable.
10. Produce a verification report only.

Report requirements:
- Verdict: PASS / FAIL.
- Files inspected.
- Acceptance criteria results.
- Tests run and results.
- Any risks, gaps, or unrelated dirty-tree notes.
- Do not implement fixes unless explicitly asked in a later prompt.

Save the report as `plan/reviews/SAR_VERIFY_PR1_1.md`.

Act as IMPLEMENTER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implement only PR2 from `plan/reviews/SAR_PLANNER_REPORT.md`: **Centralize Opaque Transport Helper Semantics**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_VERIFY_PR1_1.md`

Scope:
- Current repository only.
- Implement PR2 only.
- Do not implement PR3 or later.
- Do not redesign the SAR graph architecture.
- Preserve current runtime behavior.
- Preserve compatibility aliases.
- Do not change resolver contract vocabulary beyond what PR1 already changed.
- Do not add external SAR dependencies.
- Do not remove existing tests unless directly superseded by PR2 changes.

PR2 goal:
Centralize SAR opaque transport helper semantics so synthetic host pointers, device pointers, and event IDs are generated from one helper location and cannot become accidental SAR identity channels.

Required work:
1. Inspect the current SAR helper/node/test code relevant to PR2.
2. Identify duplicated helper logic for:
   - opaque host pointers,
   - synthetic device pointers,
   - opaque event IDs.
3. Move or consolidate that logic into `examples/SAR/include/sar/SarRuntimeHelpers.hpp` or the existing SAR runtime helper location.
4. Update SAR nodes to use centralized helpers, likely including:
   - `AzimuthTileSplitNode.cpp`
   - `H2DAsyncAccelNode.cpp`
   - `SarBackprojectionTransformAccelNode.cpp`
   - `D2HAsyncAccelNode.cpp`
   - `ImageTileMergeNode.cpp`
5. Preserve existing sidecar identity behavior exactly.
6. Preserve existing token, ticket, and timing behavior unless a test proves a duplicated helper bug.
7. Keep generated values semantically opaque and transport-only.
8. Do not move SAR-specific helper semantics into `libgpu`.
9. Add or update tests proving:
   - centralized helpers produce valid opaque transport metadata,
   - sidecar identity is invariant when transport metadata changes,
   - no SAR node owns private identity-like pointer/event generation logic where PR2 centralization applies.
10. Run focused relevant tests:
   - SAR runtime helper tests.
   - SAR transport opaque contract tests.
   - SAR accel node tests.
   - SAR JSON/runtime tests affected by helper behavior.
   - Full SAR unit test binary if available.

Files likely affected:
- `examples/SAR/include/sar/SarRuntimeHelpers.hpp`
- `examples/SAR/src/AzimuthTileSplitNode.cpp`
- `examples/SAR/src/H2DAsyncAccelNode.cpp`
- `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
- `examples/SAR/src/D2HAsyncAccelNode.cpp`
- `examples/SAR/src/ImageTileMergeNode.cpp`
- SAR token/transport/helper tests under `examples/SAR/test`

Acceptance criteria:
- Opaque host pointer, synthetic device pointer, and opaque event generation are centralized in SAR runtime helpers.
- SAR nodes use the centralized helpers instead of private duplicate helper logic.
- Sidecar identity remains unchanged through H2D, backprojection, D2H, split, and merge.
- Transport fields remain documented/tested as opaque transport metadata only.
- Existing SAR runtime behavior is preserved.
- PR1 resolver-contract changes remain intact.
- No external dependencies are added.

Stop after PR2 is implemented and verified.
Report:
- Files changed.
- Files deleted.
- Tests added or updated.
- Tests run and results.
- Any risks or follow-up left for later PRs.

Act as VERIFIER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Verify only PR2: **Centralize Opaque Transport Helper Semantics**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_VERIFY_PR1_1.md`
- `plan/reviews/SAR_IMPL_PR2_1.md`

Scope:
- Current repository only.
- Verify PR2 only.
- Do not redesign.
- Do not implement new functionality.
- Do not verify PR3 or later except to confirm they remain untouched.
- Preserve current runtime behavior.
- Treat unrelated dirty working-tree changes as out of scope unless they affect PR2 verification.

PR2 acceptance criteria to verify:
1. Opaque host pointer generation is centralized in SAR runtime helpers.
2. Synthetic device pointer generation is centralized in SAR runtime helpers.
3. Opaque event ID generation is centralized in SAR runtime helpers.
4. SAR nodes use centralized helpers instead of private duplicate helper logic.
5. Sidecar identity remains unchanged through split, H2D, backprojection, D2H, and merge.
6. Transport fields remain documented/tested as opaque transport metadata only.
7. Existing SAR runtime behavior is preserved.
8. PR1 resolver-contract changes remain intact.
9. No external SAR dependencies were added.
10. No PR3+ work was implemented.

Verification tasks:
1. Inspect changed PR2 files and relevant helper/node/test code.
2. Confirm `SarRuntimeHelpers.hpp` owns the centralized helper APIs for:
   - `OpaqueHostPointer`
   - `OpaqueReadyEventNotSignaled`
   - `NextOpaqueEventId`
   - `SyntheticDevicePointer`
3. Confirm SAR nodes call `sar::runtime` helpers and no longer define private duplicate opaque host pointer, synthetic device pointer, or opaque event helper functions.
4. Confirm token-id sequencing was not unnecessarily moved or redesigned.
5. Confirm PR1 SAR config resolver labels still use `SarAccelControlToken`, and generic view labels have not reappeared in SAR configs.
6. Confirm generic GPU resolver defaults were not changed.
7. Confirm no external baseline/dependency files were changed by PR2.
8. Run focused verification commands:
   - Build affected targets if needed.
   - SAR runtime helper tests.
   - SAR transport opaque contract tests.
   - SAR accel node tests.
   - SAR JSON/runtime tests affected by helper behavior.
   - Full SAR unit test binary if available.
   - `examples/SAR/main.cpp` executable path if buildable.
9. Produce a verification report only.

Report requirements:
- Verdict: PASS / FAIL.
- Files inspected.
- Acceptance criteria results.
- Tests run and results.
- Any risks, gaps, or unrelated dirty-tree notes.
- Do not implement fixes unless explicitly asked in a later prompt.

Save the report as `plan/reviews/SAR_VERIFY_PR2_1.md`.

=======

Act as IMPLEMENTER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implement only PR3 from `plan/reviews/SAR_PLANNER_REPORT.md`: **Add Sidecar Preservation Coverage For Resolver And Metal Paths**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_VERIFY_PR1_1.md`
- `plan/reviews/SAR_VERIFY_PR2_1.md`
- `plan/reviews/SAR_VERIFY_PR2_2.md`

Scope:
- Current repository only.
- Implement PR3 only.
- Do not implement PR4 or later.
- Do not redesign the SAR graph architecture.
- Preserve current runtime behavior.
- Preserve compatibility aliases for:
  - `H2DAsyncNode`
  - `D2HAsyncNode`
  - `SarBackprojectionTransformNode`
- Do not add external SAR dependencies.
- Do not change resolver contract vocabulary beyond what PR1 already established.
- Do not change opaque transport helper semantics beyond what PR2 already centralized.
- Do not remove existing tests unless directly superseded by PR3 changes.

PR3 goal:
Add test coverage proving resolver-selected SAR H2D, backprojection/kernel, and D2H paths preserve `SarSidecar` identity, including native Metal path coverage where locally available or safely capability-gated.

Required work:
1. Inspect the current SAR resolver/runtime/accel-node/Metal test code relevant to PR3.
2. Add or update tests proving resolver-selected SAR H2D, kernel/backprojection, and D2H implementations preserve `SarSidecar`.
3. Add native Metal backprojection sidecar-preservation coverage where Metal capabilities are available, or a capability-gated equivalent if local/CI Metal availability varies.
4. Assert that transport-only fields such as `host_ptr`, `device_ptr`, and `ready_event` do not become SAR identity channels after resolver substitution.
5. Confirm native and synthetic paths have equivalent sidecar identity semantics.
6. Keep capability gating meaningful: Metal unavailability may skip Metal-specific assertions, but synthetic/resolver coverage must still run in CI.
7. Preserve PR1 SAR resolver labels using `SarAccelControlToken`.
8. Preserve PR2 centralized helper usage and do not reintroduce private duplicate opaque transport helper logic.
9. Do not implement compatibility alias migration work from PR4.

Files likely affected:
- `examples/SAR/test/test_sar_accel_nodes.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`
- Existing Metal-capability test fixtures, if present
- `examples/SAR/config/sar_stripmap_definitive.json` only if test fixtures require explicit mapping updates

Acceptance criteria:
- Resolver-selected SAR H2D, backprojection/kernel, and D2H implementations preserve sidecar identity.
- Native Metal and synthetic paths have equivalent sidecar identity semantics where Metal is available or mockable.
- Tests fail if SAR identity is reintroduced through transport fields.
- Capability-gated Metal coverage does not hide all meaningful PR3 assertions in CI.
- Existing SAR runtime behavior is preserved.
- PR1 and PR2 changes remain intact.
- No external dependencies are added.
- No PR4+ work is implemented.

Focused verification to run:
- Build affected targets if needed.
- SAR JSON runtime tests.
- SAR accel node tests.
- Metal-related SAR tests when available locally.
- Full SAR unit test binary if available.
- `examples/SAR/main.cpp` executable path if buildable.

Stop after PR3 is implemented and verified.

Save the implementation report as:
- `plan/reviews/SAR_IMPL_PR3_1.md`

Report:
- Files changed.
- Tests added or updated.
- Tests run and results.
- Any skipped Metal/capability-gated tests and why.
- Any risks or follow-up left for later PRs.

Act as IMPLEMENTER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implement only PR4 from `plan/reviews/SAR_PLANNER_REPORT.md`: **Put Compatibility Aliases On An Explicit Migration Path**.

Use these reports as context:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_VERIFY_PR1_1.md`
- `plan/reviews/SAR_VERIFY_PR2_1.md`
- `plan/reviews/SAR_VERIFY_PR2_2.md`
- `plan/reviews/SAR_IMPL_PR3_1.md`

Scope:
- Current repository only.
- Implement PR4 only.
- Do not implement PR5 or later.
- Do not redesign the SAR graph architecture.
- Preserve current runtime behavior.
- Preserve compatibility aliases for:
  - `H2DAsyncNode`
  - `D2HAsyncNode`
  - `SarBackprojectionTransformNode`
- Do not remove compatibility aliases in this PR.
- Do not add external SAR dependencies.
- Do not change resolver contract vocabulary beyond what PR1 established.
- Do not change opaque transport helper semantics beyond what PR2 centralized.
- Do not expand resolver/Metal sidecar coverage beyond what PR3 requires unless directly needed for alias-boundary tests.

PR4 goal:
Put the existing SAR compatibility aliases on an explicit migration path without removing them, and prove they resolve to the canonical accel-token implementations rather than creating a second SAR GPU path.

Required work:
1. Inspect the current alias headers, SAR config, token contract tests, and JSON/runtime tests relevant to PR4.
2. Add tests documenting that compatibility aliases are type aliases for the canonical accel-token implementations:
   - `H2DAsyncNode` -> `H2DAsyncAccelNode`
   - `D2HAsyncNode` -> `D2HAsyncAccelNode`
   - `SarBackprojectionTransformNode` -> `SarBackprojectionTransformAccelNode`
3. Add or strengthen tests proving alias input/output token types remain `SarAccelControlToken`.
4. Add config/runtime coverage proving old config-facing names do not create a second SAR GPU path and still resolve/run through the canonical accel-token implementation.
5. Add lightweight documentation or comments, only where consistent with repository style, marking these names as compatibility aliases and stating future removal criteria.
6. Define a safe migration path for future alias removal, but do not remove aliases now.
7. Preserve PR1 definitive config resolver labels using `SarAccelControlToken`.
8. Preserve PR2 centralized helper usage.
9. Preserve PR3 sidecar-preservation coverage.
10. Do not implement PR5 main/benchmark validation work.

Files likely affected:
- Alias headers under `examples/SAR/include/sar`, likely:
  - `examples/SAR/include/sar/H2DAsyncNode.hpp`
  - `examples/SAR/include/sar/D2HAsyncNode.hpp`
  - `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`
- `examples/SAR/test/test_sar_token_contract.cpp`
- SAR JSON/runtime tests under `examples/SAR/test`
- Any local SAR docs that already list definitive node names, if present and appropriate
- `examples/SAR/config/sar_stripmap_definitive.json` only if alias-boundary tests require explicit fixture updates

Acceptance criteria:
- Compatibility aliases remain present.
- Alias behavior is explicitly covered by tests.
- Alias token contracts are `SarAccelControlToken`.
- Config-facing compatibility names resolve to the canonical accel-token implementations.
- No second SAR GPU path exists through alias names.
- Future alias removal criteria are documented or encoded in tests/docs.
- Existing SAR runtime behavior is preserved.
- PR1, PR2, and PR3 changes remain intact.
- No external dependencies are added.
- No PR5+ work is implemented.

Focused verification to run:
- Build affected targets if needed.
- SAR token contract tests.
- SAR JSON/runtime tests.
- PR3 resolver/sidecar tests affected by alias behavior.
- Full SAR unit test binary if available.
- `examples/SAR/main.cpp` executable path if buildable.

Stop after PR4 is implemented and verified.

Save the implementation report as:
- `plan/reviews/SAR_IMPL_PR4_1.md`

Report:
- Files changed.
- Tests added or updated.
- Tests run and results.
- Any documentation/comments added for alias migration.
- Any risks or follow-up left for later PRs.