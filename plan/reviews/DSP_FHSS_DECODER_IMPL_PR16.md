# DSP FHSS Decoder PR16 Implementer Report

## PR

PR16: RF Feasibility, Full Selectable-Frequency Strategy, And Impairment Plan

## Summary

Implemented the PR16 planning update without adding DSP behavior. The roadmap
and FHSS docs now explicitly preserve the deterministic CPU fixture boundary,
avoid production RF/channelizer claims, choose a future selectable-frequency
coverage strategy, and define planned impairment status vocabulary before any
impairment implementation.

## Implemented

- Updated `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md` to identify the
  channelized graph as the canonical deterministic fixture graph and the PR8
  correlator-bank graph as reference-only.
- Kept occupied-bandwidth/channel-filter requirements unresolved for PR16 and
  explicitly required a future spectral-occupancy estimator or RF spectral mask
  before production channelizer claims.
- Chose retuned sub-band windows as the default future full
  selectable-frequency coverage strategy.
- Preserved the invariant that the receiver configuration has one logical
  GraphX channel output port per configured frequency while stating that a
  physical 500 Msps capture window only realizes the subset that fits the
  declared sub-band.
- Documented that full-table simultaneous alias-free capture requires a higher
  sample rate or a later explicit alias/downconversion model.
- Defined planned future impairment status vocabulary:
  - `unsupported`
  - `disabled`
  - `configured_rejected`
  - `estimated`
  - `degraded`
  - `invalid`
- Kept current fixture diagnostics limited to disabled/rejected impairment
  status, including `unsupported_impairments_rejected`.
- Updated `docs/dsp/fhss_decoder.md` with the PR16 RF feasibility, coverage,
  and impairment planning boundary.
- Added guardrail tests proving docs/roadmap retain these RF feasibility and
  non-claim boundaries.

## Out Of Scope

No Doppler, noise, CFO, phase-drift, multipath, Metal/GPU,
overlap-aware separation, external dataset, occupied-bandwidth estimator,
production RF behavior, production channelizer claim, or canonical PDW
diagnostic path was implemented.

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

No offline spectral-analysis fixture tests were added because PR16 does not add
a deterministic occupied-bandwidth estimator. The plan deliberately keeps that
as a future prerequisite before any production channelizer separation claim.
