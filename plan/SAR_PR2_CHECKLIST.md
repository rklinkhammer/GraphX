# SAR PR2 Checklist

Status:

- [x] PR2 branch created
- [ ] PR2 implementation complete
- [ ] PR2 ready for review
- [ ] PR2 merged

## Scope (from plan/SAR.md)

- [ ] Replace deterministic placeholder math with stronger matched-filter/backprojection fidelity.
- [ ] Add optional trace export and deeper queue/backpressure diagnostics.
- [ ] Evaluate DeviceReduceNode accumulation showcase.

## Phase A - Matched Filter/Backprojection Fidelity

- [ ] Define deterministic reference dataset and tolerance envelope.
- [ ] Implement stronger matched-filter stage behavior in SAR pipeline.
- [ ] Improve backprojection fidelity while preserving deterministic CI profile.
- [ ] Add/update unit tests validating numeric stability and tolerances.

Exit criteria:

- [ ] Baseline and graph outputs match within explicit tolerance for PR2 dataset.
- [ ] CI-safe profile remains deterministic and stable.

## Phase B - Trace Export and Diagnostics Depth

- [ ] Add optional trace export toggle (off by default).
- [ ] Emit stage-level timing and transfer counters into trace output.
- [ ] Expand queue/backpressure diagnostics depth (without brittle thresholds).
- [ ] Add tests for trace schema presence and diagnostics fields.

Exit criteria:

- [ ] Trace export generated when enabled.
- [ ] Diagnostics fields present and deterministic enough for CI assertions.

## Phase C - DeviceReduceNode Evaluation

- [ ] Prototype DeviceReduceNode accumulation path in SAR flow (feature-gated).
- [ ] Compare behavior/perf against current merge/accumulation path.
- [ ] Document keep/defer decision with rationale.

Exit criteria:

- [ ] Decision recorded with evidence (keep in PR2 or defer to PR3).

## Validation Matrix

- [ ] Build: SAR example + SAR tests + benchmark target.
- [ ] Run: focused SAR unit/integration tests.
- [ ] Run: graph-vs-baseline tolerance comparisons for PR2 dataset.
- [ ] Run: trace-enabled smoke validation.

## PR2 Deliverables

- [ ] Code + tests for fidelity improvements.
- [ ] Documentation updates in examples/SAR/README.md.
- [ ] Benchmark/trace notes updated.
- [ ] PR body includes risk, rollout, and evidence summary.
