# SAR Verifier Report: PR6

Date: 2026-06-10
PR: PR6
Title: Resolver and Schema Guardrails for Accel Token Contract
Verifier role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## Verdict
Pass with caveat.

## Pass/Fail
- PR6 acceptance criteria status: **PASS**

## Blocking Issues
- None.

## Non-Blocking Issues
1. Full current CTest lane is not green due to a separate instability in:
   - `SarPr2FanoutJsonTest.ExecutesGraphVisibleFanoutTopology`
   - File: `examples/SAR/test/test_sar_pr2_fanout_json.cpp`
   - Reproduced in focused run; observed lower-than-threshold counters (sequence, pulses, bytes_h2d, kernel_dispatches, duplicate_tile_count).
2. This failure is not attributable to PR6 parser/resolver guardrail changes.

## Acceptance Criteria Verification

### 1) Guardrails enforce canonical contract and fail fast on forbidden legacy edges
Status: **Satisfied**

Evidence:
- Legacy payload contract names are normalized and rejected via guardrail list in parser:
  - `libgraph/src/graph/GraphConfigParser.cpp` (`IsLegacySarPayloadContract`)
- Parse-time fail-fast now enforced when `edge_contract == "accel-token"` for:
  - `resolver_mappings[*].input_token_type`
  - `resolver_mappings[*].output_token_type`
  - `edges[*].payload_contract`
  - all in `libgraph/src/graph/GraphConfigParser.cpp`
- Tests assert parse-time failure (`ValidationFailed`) rather than validate-time-only rejection:
  - `libgraph/test/unit/test_graph_config_parser.cpp`
    - `ParseSafeRejectsLegacySarPayloadContractForAccelTokenGraph`
    - `ParseSafeRejectsLegacyResolverMappingTokenTypeForAccelTokenGraph`
  - `examples/SAR/test/test_sar_accel_token_guardrails.cpp`
    - `RejectsLegacyPayloadContractUnderAccelTokenMode`

### 2) Resolver behavior remains explicit and deterministic
Status: **Satisfied**

Evidence:
- Parser explicitly validates resolver policy fields and accepted domains:
  - `execution_backend`
  - `backend_fallback_policy`
  - `edge_contract`
  - in `libgraph/src/graph/GraphConfigParser.cpp`
- Runtime tests cover strict vs fallback behavior and deterministic backend/concrete resolution:
  - `examples/SAR/test/test_sar_json_runtime.cpp`
    - `DefinitivePresetStrictMetalSelectionFailsWithoutConcreteProvider`
    - `DefinitivePresetResolvesCommonMetalNodesWithComposedProvider`
    - contract-blocking substitution guardrail tests for range window/compression mappings.

## Suggested Fixes
1. Keep PR6 accepted as implemented.
2. Open a separate follow-up for fanout test stability in `examples/SAR/test/test_sar_pr2_fanout_json.cpp`.
3. Consider converting brittle lower-bound assertions in that fanout test to deterministic invariants tied to configured pulse/tile counts and completion guarantees.
4. Optional PR6 hardening: add one positive parser test that canonical non-legacy resolver mapping token types are accepted under `edge_contract: accel-token`.
