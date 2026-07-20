# FHSS Architecture / Dashboard Plan Conformance Report

Date: 2026-07-19

Scope: compare [docs/dsp/fhss_architecture.md](docs/dsp/fhss_architecture.md) with [docs/dsp/fhss_dashboard_implementation_plan.md](docs/dsp/fhss_dashboard_implementation_plan.md) for implementation conformance. This report is analysis only; no source documents were modified.

## Executive Summary

The dashboard implementation plan is broadly conformant with the FHSS architecture document. The two documents agree on the most important boundaries: synthetic-only evidence, no HWIL/OTA qualification claim, receiver truth isolation, and the distinction between the canonical fixture lane and the truth-free binary-IQ receiver lane.

The main difference is scope, not contradiction. The architecture document defines the runtime FHSS implementation and its evidence boundary, while the dashboard plan adds a multi-phase delivery roadmap for transport, UI, operator workflow, security, packaging, and qualification. Those additions are not inconsistent with the architecture, but they are external implementation constraints that must remain subordinate to the receiver-only input contract and the synthetic-only evidence class.

## Conformance Assessment

| Area | Architecture position | Plan position | Assessment |
|---|---|---|---|
| Synthetic-only boundary | Phase 3/4/5+ infrastructure is outside the runtime graph and all validation data is synthetic or software evidence. | The plan repeatedly states that all dashboard evidence is synthetic, with no HWIL/OTA/conducted-RF claims. | Conformant. |
| Receiver truth isolation | The truth-free binary-IQ receiver receives only IQ and receiver config; generator truth is not passed to execution. | Phase 2 explicitly keeps receiver config minimal and forbids generator truth in the receiver path. | Conformant. |
| Canonical vs alternative lanes | The canonical lane is the fixture graph; the binary-IQ lane is an alternative, not a consecutive stage. | The plan preserves a FHSS-specific workflow and does not collapse the two lanes into one shared generic path. | Conformant. |
| Observation boundary | Dashboard observation is a separate phase boundary, with expected truth derived independently from authoritative config. | Phase 4 keeps expected and observed data separate and avoids inventing values. | Conformant. |
| Runtime ownership | Real executor ownership and lifecycle control are part of the runtime contract, not simulated state. | Phase 3 makes lifecycle controls operate a real FHSS receiver graph and introduces a runtime owner. | Conformant, assuming implementation follows the stated contract. |
| Accelerator claims | Native Metal/CUDA FHSS kernels are not implemented; fallback must not be reported as native acceleration. | The plan does not assert native accelerator execution. | Conformant. |
| Packaging / dashboard delivery | The architecture focuses on FHSS runtime behavior and observation boundaries, not web packaging details. | The plan adds dashboard packaging, browser security, and operator tooling requirements. | Not a conflict, but an extension beyond the architecture spec. |

## Notable Alignment Points

The architecture doc explicitly anchors the canonical fixture lane and the truth-free receiver lane, including the receiver-only input contract and the requirement that `preamble_pulses` are sufficient for the assembler while redundant `active_frequency_indices` are not needed in the receiver path. The dashboard plan matches that shape by keeping the receiver minimal and by treating truth as a separate artifact rather than a runtime input.

The plan also respects the architecture’s evidence classification. It avoids claiming production RF qualification and repeatedly says the workflow is synthetic-only. That is consistent with the architecture’s statement that current validation data is synthetic or software evidence, not hardware evidence.

## Scope Extensions That Need Care

Several plan sections go beyond what the architecture document itself defines:

1. HTTP semantics, JSON Patch/Pointer, WebSocket streaming, CSP, and RFC-level API requirements.
2. Dashboard packaging, installed-tree deployment, accessibility, browser automation, and operator guidance.
3. Job/control-plane phases that sit above the runtime graph.

These are acceptable roadmap additions, but they should not be treated as architectural facts unless the codebase and its runtime docs are updated to reflect them.

The only real implementation risk visible from the docs is boundary drift: if later work allows the dashboard to reintroduce generator truth into the receiver path, or if observed fields silently fall back to truth-derived data, that would violate the architecture. The plan appears to guard against that, but it should remain a hard acceptance rule during implementation.

## Final Judgment

Overall conformance: **good**.

No direct contradiction was found between the two documents. The implementation plan is a broader dashboard roadmap built on top of the FHSS architecture, and it stays aligned so long as the receiver remains truth-free, evidence stays synthetic, and dashboard transport/security features remain an envelope around the existing FHSS runtime contract rather than a replacement for it.

## References

- [fhss_architecture.md](docs/dsp/fhss_architecture.md#L13)
- [fhss_architecture.md](docs/dsp/fhss_architecture.md#L68)
- [fhss_architecture.md](docs/dsp/fhss_architecture.md#L84)
- [fhss_architecture.md](docs/dsp/fhss_architecture.md#L101)
- [fhss_dashboard_implementation_plan.md](docs/dsp/fhss_dashboard_implementation_plan.md#L38)
- [fhss_dashboard_implementation_plan.md](docs/dsp/fhss_dashboard_implementation_plan.md#L238)
- [fhss_dashboard_implementation_plan.md](docs/dsp/fhss_dashboard_implementation_plan.md#L380)
- [fhss_dashboard_implementation_plan.md](docs/dsp/fhss_dashboard_implementation_plan.md#L588)
