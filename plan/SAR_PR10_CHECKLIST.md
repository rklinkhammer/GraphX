# SAR PR10 Checklist: Definitive Pipeline Closure and Validation Stabilization

Status:

- [x] PR10 planned
- [x] PR10 implementation started
- [x] PR10 implementation complete
- [x] PR10 ready for review
- [ ] PR10 merged

## Objective

Close the remaining gaps identified by the PR9/definitive-pipeline audit while preserving GraphX SAR runtime contracts:

1. Restore reliable completion signaling for the single definitive SAR run through sar_example.
2. Stabilize image parity validation so SAR lane test outcomes are deterministic and meaningful.
3. Add direct test coverage for definitive config and resolver/contract behavior, including dynamic METAL substitution.

## Inputs and Context

- plan/SAR_DEFINITIVE_PIPELINE_PROMPT.md
- plan/sar_test.md
- plan/SAR_PR9_CHECKLIST
- plan/SAR_PR9_REVIEW_MAP.md
- plan/pr_checklist.md

## Carry-Forward Requirements

- [x] Keep GraphExecutorBuilder + JSON config as canonical runtime contract.
- [x] Keep edge_contract set to accel-token in all maintained SAR runtime presets.
- [x] Keep one definitive SAR JSON config (portable intent nodes only) as the canonical topology artifact.
- [x] Ensure ResolverConfig dynamic METAL selection remains explicit via strict-fail guardrail and allow-fallback runtime path.
- [x] Keep resolver metadata explicit in definitive presets:
  - [x] execution_backend
  - [x] backend_fallback_policy
  - [x] resolver_diagnostics
  - [x] edge_contract
- [x] Keep direct/non-graph paths limited to baseline, parity, and attribution.

## Scope

- [x] Consolidate to one definitive SAR config and retire dual-config workflow.
- [x] Add definitive config integration tests for one canonical file:
  - [x] examples/SAR/config/sar_stripmap_definitive.json
- [x] Validate completion signaling semantics end-to-end through sar_example-compatible GraphExecutor execution.
- [x] Resolve strict-threshold instability in PR7 materialized-image parity checks.
- [x] Preserve and re-assert overhead attribution policy fields and claim discipline.
- [x] Keep all updates backward compatible with PR6/PR7/PR8/PR9 trace and contract expectations.

## Initial Execution Order (First PR10 Slice)

- [x] Slice 10.1: Definitive runtime coverage
  - [x] Add tests that execute one definitive JSON with plugin directory bootstrap.
  - [x] Verify ResolverConfig METAL strict policy behavior (fails without concrete provider) and allow-fallback execution from same topology.
  - [x] Assert run success plus expected completion behavior.
  - [x] Assert diagnostics sink counters are populated and stable.

- [x] Slice 10.2: Completion signal root-cause hardening
  - [x] Trace completion-signal path from sink to executor for definitive topologies.
  - [x] Fix timeout-driven completion mismatch so completion reflects terminal EOS state in sar_example.
  - [x] Add regression assertions for host/simulated and METAL resolver selections using the same definitive JSON.

- [x] Slice 10.3: Parity tolerance stabilization
  - [x] Investigate tiny numeric drift currently breaching relative L2 and dynamic-range tolerance.
  - [x] Choose one controlled strategy:
    - [x] tolerance rebasing with documented rationale and evidence.
  - [x] Ensure updated thresholds remain strict enough to catch regressions.

- [x] Slice 10.4: Attribution and claim-policy guardrails
  - [x] Re-run and assert graph_overhead_ms and graph_run_minus_baseline_median consistency.
  - [x] Re-assert performance_claim_policy constraints in trace schema tests.

- [x] Slice 10.5: Single-config migration
  - [x] Update docs and scripts to reference the single definitive JSON.
  - [x] Remove references to separate non-METAL and METAL definitive config files.
  - [x] Preserve benchmark comparability by varying resolver/backend selection, not topology files.

## Mandatory GraphExecutor / JSON Contract (from pr_checklist)

- [x] Does this PR preserve examples/SAR/src/main.cpp as the canonical entrypoint?
- [x] Are all new or changed nodes usable from JSON config?
- [x] Are plugin registration and dynamic loading covered?
- [x] Were examples/SAR/config/*.json files updated or explicitly validated?
- [x] Does at least one GraphExecutor-driven test or benchmark exercise the change?
- [x] Is any direct/non-graph path limited to baseline or parity measurement?

## Accel-Token and Resolver Guardrails

- [x] No legacy SAR payload-edge contracts under accel-token mode.
- [x] edge_contract remains accel-token for definitive and maintained presets.
- [x] Resolver metadata fields remain present and parse/validate successfully.
- [x] Portable intent remains intact; backend resolution behavior is explicit and test-verified.

## Accuracy and Fidelity Gates

- [x] PR7 materialized image parity tests pass consistently in SAR lane.
- [x] Relative L2 parity gate is stable under repeated local/CI-like runs.
- [x] Dynamic-range delta gate is stable under repeated local/CI-like runs.
- [x] Existing CPU-reference parity checks remain green.

## Performance and Attribution

- [x] benchmark_main_metal_vs_nonmetal.sh executes one definitive config under two resolver/backend selections and reports per-run plus avg/min/max.
- [x] Trace schema retains required fields:
  - [x] overhead_ms.graph_run_minus_baseline_median
  - [x] overhead_attribution.cost_buckets.graph_overhead_ms
  - [x] performance_claim_policy.speedup_basis
  - [x] performance_claim_policy.disallow_lifecycle_total_as_speedup_basis
- [x] No speedup claims from lifecycle total metrics.

## Risks and Mitigations

- [x] Risk: completion signaling behavior diverges across resolver-selected backend variants.
  - [x] Mitigation: single-config regression tests with explicit EOS/terminal assertions across resolver selections.
- [x] Risk: parity thresholds become either flaky or too loose.
  - [x] Mitigation: evidence-backed tolerance policy with repeatability checks.
- [x] Risk: ResolverConfig substitution logic drifts from tested contract assumptions.
  - [x] Mitigation: dedicated substitution and parser validation checks tied to the single definitive config.

## Non-Goals

- No framework-wide scheduler redesign.
- No replacement of GraphExecutor + JSON runtime contract.
- No broad SAR architecture rewrites beyond PR10 closure targets.

## Acceptance Criteria

- [x] Single definitive config executes successfully and signals completion as expected under host/simulated and METAL resolver selections.
- [x] SAR lane tests are green, including previously unstable parity checks.
- [x] Definitive single-config and resolver substitution/strict-policy coverage exists in automated tests.
- [x] Accel-token and resolver guardrails remain green.
- [x] Overhead attribution and performance-claim policy checks remain green.

## PR10 Reviewer Checklist

- [x] Completion signaling behavior is explicit, tested, and stable.
- [x] Parity gate updates are justified with numeric evidence.
- [x] Single definitive config and ResolverConfig METAL strict-policy/fallback tests are present and meaningful.
- [x] No regression in accel-token, resolver, or trace schema contracts.
- [x] Validation summary includes SAR test lane and benchmark evidence.