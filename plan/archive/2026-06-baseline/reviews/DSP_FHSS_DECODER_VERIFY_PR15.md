# DSP FHSS Decoder PR15 Verifier Report

PR: PR15 - Channelized Lane Promotion And Correlator-Bank Deprecation Plan

Role: VERIFIER

Verdict: FAIL

## Scope Verified

Verified PR15 against:

- `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`
- `plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md`
- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- Current repository docs, configs, tests, and runtime test output

## Blocking Finding

1. Roadmap still does not clearly identify the PR15 canonical graph decision.

   The docs and configs now identify
   `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json` as the canonical
   FHSS fixture graph and mark `libdsp/config/fhss_cpsm_fixture_500msps.json`
   as reference-only. However, the roadmap still describes:

   - "Current PR8 CPU fixture graph" using `FHSSCorrelatorBankDetectorNode`
   - "Longer-term channelized graph" using `FHSSDownconverterNode`,
     `ChannelizerNode`, and `PerChannelPulseDetectorNode[]`

   This leaves the roadmap stale relative to the PR15 decision and fails the
   verifier requirement that roadmap/docs/config clearly identify the canonical
   FHSS graph shape.

   Required fix: update the roadmap to state that the channelized graph is now
   the canonical deterministic FHSS fixture graph, with the PR8 correlator-bank
   graph retained as compatibility/reference only.

## Required Checks

| Check | Result | Evidence |
| --- | --- | --- |
| Roadmap/docs/config clearly identify the canonical FHSS graph shape | FAIL | Docs/config do; roadmap still labels PR8 as current and channelized as longer-term |
| Correlator-bank detector graph is removed or clearly labeled compatibility/reference only | PASS | Docs/config mark PR8 graph as reference-only; it is retained, not removed |
| Guardrail test identifies the canonical FHSS graph config | PASS | `FHSSGraphXGuardrailTest.FhssCanonicalGraphConfigIsChannelized` |
| Regression test proves no doc/config labels correlator-bank detector as production-like channelization | PASS | `FHSSGraphXGuardrailTest.FhssDocsAndConfigsKeepCorrelatorBankReferenceOnly` |
| At least one full deterministic FHSS executor lane remains covered in CI | PASS | `FHSSGraphXExecutorTest.ChannelizedJsonTopologyRunsThroughGraphExecutorBuilderAndMatchesTruth` |
| No protocol behavior change, production RF claim, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostic path was added | PASS | Text scan and existing guardrails keep these features unsupported/future-boundary only |

## Test Results

Commands run:

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXExecutorTest.*'
```

Result: PASS, 18 tests.

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=*FHSS*:*CPSM*'
```

Result: PASS, 105 tests.

```text
git diff --check
```

Result: PASS.

## Notes

Implementation behavior appears correct and deterministic. The failure is a
planning/documentation consistency failure: PR15 requires consolidation around
the selected canonical graph, and the roadmap remains out of sync with the docs
and config metadata.
