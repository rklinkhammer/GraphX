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
