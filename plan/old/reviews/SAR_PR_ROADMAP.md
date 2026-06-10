# SAR PR Cleanup Roadmap

Inputs:
- [plan/reviews/SAR_INSPECTOR_REPORT.md](plan/reviews/SAR_INSPECTOR_REPORT.md)
- [plan/reviews/SAR_SIMPLIFIER_REPORT.md](plan/reviews/SAR_SIMPLIFIER_REPORT.md)

Constraints:
- Each PR compiles independently.
- Each PR includes tests.
- No compatibility shims.
- Move toward exactly one canonical SAR GPU path.

## PR1: Introduce Canonical Token and Sidecar Types

- Title: Introduce `AccelControlToken<SarSidecar>` type foundation
- Purpose: Add explicit canonical token and sidecar type model with testable contract boundaries.
- Files to touch:
  - [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp)
  - [examples/SAR/include/sar/H2DAsyncNode.hpp](examples/SAR/include/sar/H2DAsyncNode.hpp)
  - [examples/SAR/include/sar/D2HAsyncNode.hpp](examples/SAR/include/sar/D2HAsyncNode.hpp)
  - [examples/SAR/include/sar/SarBackprojectionTransformNode.hpp](examples/SAR/include/sar/SarBackprojectionTransformNode.hpp)
  - [examples/SAR/test/CMakeLists.txt](examples/SAR/test/CMakeLists.txt)
- Files to delete: none.
- Tests to add:
  - New SAR unit tests asserting token and sidecar structural contract.
- Tests to delete: none.
- Acceptance criteria:
  - Build passes.
  - New token contract tests pass.
  - No shim layer introduced.
- Risks:
  - Creating parallel contract models if old contracts remain primary.
- Rollback plan:
  - Revert token type additions and associated tests.

## PR2: Remove Encoded `host_ptr`/`ready_event` Identity

- Title: Remove pointer/event identity encoding from SAR runtime path
- Purpose: Eliminate most dangerous ambiguity: encoded identity in pointer/event channels.
- Files to touch:
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp)
  - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp)
  - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp)
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp)
  - [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp)
  - [examples/SAR/include/sar/ImageTileMergeNode.hpp](examples/SAR/include/sar/ImageTileMergeNode.hpp)
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp)
  - [examples/SAR/test/test_sar_json_pipeline.cpp](examples/SAR/test/test_sar_json_pipeline.cpp)
- Files to delete:
  - [examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp](examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp)
  - [examples/SAR/src/SarAccelTokenSidecarStore.cpp](examples/SAR/src/SarAccelTokenSidecarStore.cpp)
- Tests to add:
  - Explicit assertions that identity is not transported through pointer/event encoding.
- Tests to delete:
  - Tests validating encoded pointer/event identity behavior.
- Acceptance criteria:
  - SAR path preserves identity via explicit token/sidecar only.
  - Sidecar global store removed.
  - SAR unit suite passes.
- Risks:
  - Hidden coupling in merge diagnostics and benchmark token lifecycle fields.
- Rollback plan:
  - Restore deleted sidecar store files and revert affected runtime node changes.

## PR3: Convert SAR H2D/Kernel/D2H Path to Explicit Tokens

- Title: Migrate SAR GPU runtime path to explicit token contracts
- Purpose: Convert core SAR GPU path to one canonical tokenized contract flow.
- Files to touch:
  - [examples/SAR/include/sar/RangeWindowNode.hpp](examples/SAR/include/sar/RangeWindowNode.hpp)
  - [examples/SAR/include/sar/RangeCompressionNode.hpp](examples/SAR/include/sar/RangeCompressionNode.hpp)
  - [examples/SAR/src/RangeWindowNode.cpp](examples/SAR/src/RangeWindowNode.cpp)
  - [examples/SAR/src/RangeCompressionNode.cpp](examples/SAR/src/RangeCompressionNode.cpp)
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp)
  - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp)
  - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp)
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp)
  - [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp)
  - [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json)
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp)
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp)
  - [examples/SAR/test/test_sar_pr3_metal_json.cpp](examples/SAR/test/test_sar_pr3_metal_json.cpp)
- Files to delete: none.
- Tests to add:
  - Definitive topology token-contract runtime and resolver coverage.
- Tests to delete:
  - Temporary substitution blocker tests that only apply to mixed contracts.
- Acceptance criteria:
  - Definitive topology executes with tokenized SAR GPU stages.
  - Strict-metal and fallback resolver tests pass.
- Risks:
  - Broad signature changes across multiple nodes in one slice.
- Rollback plan:
  - Revert node signature migrations and definitive JSON contract updates atomically.

## PR4: Delete Legacy SAR Transfer/Lease Message Types

- Title: Remove obsolete SAR message abstractions
- Purpose: Remove non-canonical legacy edge abstractions from SAR message model.
- Files to touch:
  - [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp)
  - [examples/SAR/test/test_sar_diagnostics_contract.cpp](examples/SAR/test/test_sar_diagnostics_contract.cpp)
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp)
  - [examples/SAR/test/test_sar_accel_token_guardrails.cpp](examples/SAR/test/test_sar_accel_token_guardrails.cpp)
- Files to delete:
  - Legacy-type-specific tests and references no longer valid after type removal.
- Tests to add:
  - Compile/runtime checks proving legacy SAR message-edge contracts are absent.
- Tests to delete:
  - Tests tied to removed transfer/lease message wrappers.
- Acceptance criteria:
  - Removed legacy message types are not referenced by definitive runtime path.
  - SAR unit and parser-related tests pass.
- Risks:
  - Hidden references in non-definitive presets/tests.
- Rollback plan:
  - Restore removed type declarations and deleted test files.

## PR5: Resolver/Metal Sidecar Preservation Tests

- Title: Harden resolver and sidecar preservation behavior
- Purpose: Ensure resolver-selected concrete nodes preserve sidecar semantics under strict and fallback modes.
- Files to touch:
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp)
  - [examples/SAR/test/test_sar_pr3_metal_json.cpp](examples/SAR/test/test_sar_pr3_metal_json.cpp)
  - [libgraph/test/unit/test_resolving_node_provider.cpp](libgraph/test/unit/test_resolving_node_provider.cpp)
  - [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json)
- Files to delete:
  - Old blocker tests tied to deleted mixed-contract assumptions.
- Tests to add:
  - Resolver diagnostics and sidecar-preservation assertions for definitive topology in strict/fallback lanes.
- Tests to delete:
  - Obsolete contract-blocker tests.
- Acceptance criteria:
  - Resolver diagnostics prove concrete selection and sidecar continuity.
- Risks:
  - Tests could validate diagnostics strings without enforcing full end-to-end behavior.
- Rollback plan:
  - Revert test/config hardening changes only.

## PR6: Schema Guardrails for `edge_contract: "accel-token"`

- Title: Enforce accel-token schema guardrails
- Purpose: Tighten parser validation so legacy payload contracts cannot re-enter accel-token mode.
- Files to touch:
  - [libgraph/src/graph/GraphConfigParser.cpp](libgraph/src/graph/GraphConfigParser.cpp)
  - [libgraph/include/graph/GraphConfigParser.hpp](libgraph/include/graph/GraphConfigParser.hpp)
  - [examples/SAR/test/test_sar_accel_token_guardrails.cpp](examples/SAR/test/test_sar_accel_token_guardrails.cpp)
  - [libgraph/test/unit/test_graph_config_parser.cpp](libgraph/test/unit/test_graph_config_parser.cpp)
- Files to delete:
  - Legacy parser allowances/tests for SAR payload contracts under accel-token mode.
- Tests to add:
  - Positive/negative parser tests around accel-token edge contract invariants.
- Tests to delete:
  - Tests expecting legacy payload acceptance under accel-token.
- Acceptance criteria:
  - Parser rejects forbidden legacy contracts deterministically.
  - libgraph + SAR parser tests pass.
- Risks:
  - Over-constraining non-SAR graph configurations.
- Rollback plan:
  - Revert parser guardrail logic and associated tests.

## PR7: CPU Reference SAR Validation Consolidation

- Title: Consolidate deterministic CPU reference validation on canonical path
- Purpose: Keep correctness gates explicit and independent from transport cleanup.
- Files to touch:
  - [examples/SAR/test/test_sar_cpu_reference.cpp](examples/SAR/test/test_sar_cpu_reference.cpp)
  - [examples/SAR/test/test_sar_json_pipeline.cpp](examples/SAR/test/test_sar_json_pipeline.cpp)
  - [examples/SAR/test/test_sar_trace_schema.cpp](examples/SAR/test/test_sar_trace_schema.cpp)
- Files to delete:
  - Stale parity tests dependent on removed identity channels.
- Tests to add:
  - Token-sidecar parity metric assertions against deterministic CPU reference.
- Tests to delete:
  - Obsolete parity assertions tied to pointer/event semantics.
- Acceptance criteria:
  - Deterministic CPU parity suite is green on canonical token path.
- Risks:
  - Tolerance drift hiding regressions.
- Rollback plan:
  - Revert parity test updates while preserving prior transport PRs.

## PR8: Native Metal Parity Finalization

- Title: Finalize native-metal parity on single canonical SAR path
- Purpose: Lock native-metal parity evidence and remove residual dual-path artifacts.
- Files to touch:
  - [examples/SAR/test/test_sar_pr3_metal_json.cpp](examples/SAR/test/test_sar_pr3_metal_json.cpp)
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp)
  - [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp)
  - [examples/SAR/config/sar_stripmap_definitive.json](examples/SAR/config/sar_stripmap_definitive.json)
  - [plan/SAR_PR10_CHECKLIST.md](plan/SAR_PR10_CHECKLIST.md)
- Files to delete:
  - Remaining compatibility-only tests/config fragments not aligned to canonical path.
- Tests to add:
  - Native-metal parity checks tied to definitive topology and sidecar continuity.
- Tests to delete:
  - Transitional tests no longer meaningful after canonical lock.
- Acceptance criteria:
  - Definitive topology is the single canonical SAR GPU runtime path.
  - Full CTest lane passes.
  - Benchmark attribution policy remains intact.
- Risks:
  - Late-stage cleanup removing useful coverage inadvertently.
- Rollback plan:
  - Restore removed tests/config artifacts and revert final lock-step edits.

## Global roadmap guardrails

1. No compatibility shims.
2. One architectural concern per PR.
3. Every PR compiles and runs targeted tests independently.
4. Every PR reduces mixed-contract surface area and converges to one canonical SAR GPU path.
