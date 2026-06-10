# SAR_IMPL_PR5_1

## Task
Implement PR5 only.

**PR5 title:** Delete Obsolete SAR Message Abstractions

**PR5 scope:**
- Remove non-canonical legacy edge abstractions after token path is complete.

## Result
PR5 is implemented and validated.

The definitive SAR runtime path now uses canonical accel-token and sidecar state only. The removed legacy SAR message abstractions are no longer present in the definitive runtime path. Remaining legacy SAR message names are retained only as intentional string-based parser and negative-validation guardrails.

## Acceptance Criteria Check

### 1. Removed legacy message abstractions are absent from definitive runtime path
Satisfied.

Implemented changes:
- Removed legacy runtime message structs from the shared SAR type surface in `examples/SAR/include/sar/SarMessages.hpp`:
  - `SarPulseBlockMessage`
  - `SarMergeStatusMessage`
  - `SarDiagnosticsMessage`
- Replaced message-style diagnostics projection with canonical state:
  - `SarDiagnosticsSinkNode::last_status()` removed
  - `SarDiagnosticsSinkNode::last_token()` added
  - diagnostics sink now stores the canonical `SarAccelControlToken`
  - diagnostics sink now exposes `SarDiagnosticsSnapshot` instead of `SarDiagnosticsMessage`
- Updated benchmark/runtime/test consumers to use canonical token and sidecar state directly rather than message wrappers.

### 2. Parser negative-validation artifacts may remain as strings only where intentionally required for guardrails
Satisfied.

Intentional retained string-only guardrails remain in:
- `libgraph/src/graph/GraphConfigParser.cpp`
- `examples/SAR/test/test_sar_accel_token_guardrails.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`

These references are negative-validation artifacts only and are not part of the runtime edge contract.

## Files Changed
- `examples/SAR/include/sar/SarMessages.hpp`
- `examples/SAR/include/sar/SarDiagnosticsSinkNode.hpp`
- `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
- `examples/SAR/src/sar_benchmark.cpp`
- `examples/SAR/src/main.cpp`
- `examples/SAR/test/test_sar_baseline_compare.cpp`
- `examples/SAR/test/test_sar_diagnostics_contract.cpp`
- `examples/SAR/test/test_sar_json_pipeline.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`
- `examples/SAR/test/test_sar_pr2_fanout_json.cpp`
- `examples/SAR/test/test_sar_pr3_metal_json.cpp`
- `examples/SAR/test/test_sar_projectile_scenario.cpp`
- `examples/SAR/test/test_gotcha_dataset_adapter.cpp`

## Files Deleted
None.

## Tests Added
None.

## Tests Removed Or Replaced
No test files were deleted.

Obsolete message-based assertions were replaced with canonical token and diagnostics snapshot assertions in:
- `examples/SAR/test/test_sar_diagnostics_contract.cpp`
- `examples/SAR/test/test_sar_json_pipeline.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`
- `examples/SAR/test/test_sar_pr2_fanout_json.cpp`
- `examples/SAR/test/test_sar_pr3_metal_json.cpp`
- `examples/SAR/test/test_sar_baseline_compare.cpp`
- `examples/SAR/test/test_sar_projectile_scenario.cpp`
- `examples/SAR/test/test_gotcha_dataset_adapter.cpp`

## Build Commands Run
Used CMake Tools build targets:
- `sar_benchmark`
- `sar_example`
- `test_sar_example_unit`

## Test Commands Run
Used CMake Tools CTest run on the active build tree.

Result:
- `libgraph_unit` passed
- `libgraph_integration` passed
- `libgpu_stub_unit` passed
- `libgpu_metal_runtime` passed
- `sar_example_unit` passed

Summary:
- 5/5 tests passed

## Remaining Follow-up Items
None required for PR5 scope.

The only remaining legacy SAR message names are intentional string-based rejection artifacts for parser guardrails, which is permitted by the PR5 acceptance criteria.
