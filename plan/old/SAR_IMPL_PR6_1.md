# SAR Implementation Report: PR6

Date: 2026-06-10
PR: PR6
Title: Resolver and Schema Guardrails for Accel Token Contract
Scope: Harden parser and resolver behavior around `edge_contract=accel-token` and enforce fail-fast rejection of legacy SAR contracts.

## Summary
PR6 is implemented with parser-level guardrails that now fail fast when legacy SAR contract names appear in accel-token configurations. This applies to both edge payload contracts and resolver mapping token type fields. Tests were updated to reflect the stricter parse-time semantics, and the SAR lane remains green.

## 1) Files Changed
- `libgraph/src/graph/GraphConfigParser.cpp`
  - Added parse-time rejection for legacy contracts in resolver mappings under accel-token mode:
    - `input_token_type`
    - `output_token_type`
  - Added parse-time rejection for legacy `payload_contract` values on edges under accel-token mode.
- `libgraph/test/unit/test_graph_config_parser.cpp`
  - Updated legacy contract guardrail test to assert `ParseSafe` fails with `ValidationFailed` (instead of parse-success plus `Validate` failure).
  - Added a new test for legacy resolver mapping token type rejection under accel-token mode.
- `examples/SAR/test/test_sar_accel_token_guardrails.cpp`
  - Updated expected behavior to parse-time rejection (`ParseFileSafe` returns `ValidationFailed`) for legacy payload contracts.

## 2) Files Deleted
- None.

## 3) Tests Added
- `libgraph/test/unit/test_graph_config_parser.cpp`
  - `ParseSafeRejectsLegacyResolverMappingTokenTypeForAccelTokenGraph`

## 4) Tests Removed/Replaced
- No test files removed.
- Existing expectations were replaced from validate-time rejection to parse-time rejection in:
  - `libgraph/test/unit/test_graph_config_parser.cpp`
  - `examples/SAR/test/test_sar_accel_token_guardrails.cpp`

## 5) Build Commands Run
- CMake Tools build targets:
  - `test_libgraph_unit`
  - `test_sar_example_unit`

## 6) Test Commands Run
- CMake Tools CTest run (current configured build tree):
  - Initial run exposed one expected-behavior mismatch in `SarAccelTokenGuardrailsTest` after parser hardening.
  - After updating test expectation, reran build + CTest.
  - Final result: 5/5 tests passed.
    - `libgraph_unit`
    - `libgraph_integration`
    - `libgpu_stub_unit`
    - `libgpu_metal_runtime`
    - `sar_example_unit`

## 7) Remaining Follow-Up Items
- Optional: Add one positive parser test proving canonical non-legacy resolver mapping token names are accepted under accel-token mode.
- Optional: Extend verifier artifact chain with a dedicated PR6 verifier report file if required by process.

## Acceptance Criteria Check
- Guardrails enforce canonical contract and fail fast on forbidden legacy edges: **Satisfied**.
- Resolver behavior remains explicit and deterministic: **Satisfied**.
