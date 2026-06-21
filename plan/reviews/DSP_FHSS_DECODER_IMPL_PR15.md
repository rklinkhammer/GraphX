# DSP FHSS Decoder PR15 Implementer Report

## PR

PR15: Channelized Lane Promotion And Correlator-Bank Deprecation Plan

## Summary

Promoted the PR14 channelized FHSS fixture graph to the canonical deterministic
FHSS fixture graph and retained the PR8 correlator-bank graph as a compatibility
and reference topology only.

The canonical config is now explicitly identified as:

```text
libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
```

The retained reference config is:

```text
libdsp/config/fhss_cpsm_fixture_500msps.json
```

## Implemented

- Added top-level graph-role metadata to the channelized config:
  - `fhss_graph_role = canonical_channelized_fixture`
  - `canonical_fhss_graph = true`
  - `reference_only = false`
- Added top-level graph-role metadata to the PR8 correlator-bank config:
  - `fhss_graph_role = reference_correlator_bank_fixture`
  - `canonical_fhss_graph = false`
  - `reference_only = true`
- Updated `docs/dsp/fhss_decoder.md` to state that the channelized graph is the
  canonical FHSS fixture graph.
- Updated the docs to state that the correlator-bank graph is retained only for
  PR1 compatibility and regression/reference comparison.
- Added guardrail coverage proving the channelized config is canonical and the
  correlator-bank config is reference-only.
- Added regression guardrail coverage preventing docs/configs from labeling the
  correlator-bank graph as canonical or production-like channelization.
- Kept the full deterministic FHSS executor lane covered through the existing
  channelized executor test.

## Out Of Scope

No protocol behavior changes, production RF claims, Metal/GPU execution,
Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostics
were added.

## Validation

Passed:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXExecutorTest.*'

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=*FHSS*:*CPSM*'

git diff --check
```

## Notes

The correlator-bank graph was retained instead of deleted because PR14 explicitly
kept it as a reference fixture and it remains useful for deterministic
regression comparison. PR15 removes the ambiguity by making it machine-readable
as `reference_only` and by adding guardrails against relabeling it as canonical
or production-like channelization.
