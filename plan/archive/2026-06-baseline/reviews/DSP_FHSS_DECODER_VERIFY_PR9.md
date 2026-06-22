# DSP FHSS Decoder PR9 Verifier Report

PR9: Documentation And Truth-In-Labeling Guardrails

## Verdict

PASS.

PR9 satisfies the documentation and truth-in-labeling scope. The new FHSS DSP
document describes the deterministic CPU-only fixture lane, protocol limits,
CPSM assumptions, baseband/IF offset frequency mapping, unsupported behavior,
GraphX node/edge contracts, and future boundaries. README changes are limited
to DSP documentation indexing, and guardrail tests were added following the
existing FHSS guardrail-test convention.

## Required Checks

- PASS: FHSS docs explain how to build/run the deterministic lane.
  - `docs/dsp/fhss_decoder.md` includes the PR8 config path and CI-safe build/test commands for the deterministic lane.
- PASS: Docs describe fixture-only behavior versus future production-like work.
  - The doc labels the lane as a deterministic CPU-only test fixture and separates unsupported/future work.
- PASS: Docs mention baseband/offset frequencies and do not claim direct 1 GHz RF sampling at 500 Msps.
  - The doc states 1 GHz RF frequencies are metadata and fixture IQ uses baseband/IF offsets.
  - It states a 500 Msps complex stream cannot represent the full 64-entry 1 GHz RF table as direct sampled RF while preserving spacing.
- PASS: Docs describe the 64-entry RF metadata table, selectable indices `[1, 62]`, and reserved edge indices `0` and `63`.
- PASS: Docs/config identify CPU-only behavior.
  - The doc labels the lane CPU-only.
  - The fixture config contains no Metal/GPU/channelizer node references and keeps impairments/overlap disabled.
- PASS: Docs/config identify GraphX nodes and GraphX edge contracts as the canonical FHSS model and do not describe deleted pre-GraphX pseudo-nodes as current.
  - The doc identifies real GraphX nodes, dynamically loadable plugins, `ControlToken<...>` sidecars, and PR7A packet contracts as canonical.
  - It states deleted pre-GraphX pseudo-node scaffolding is not the current node model.
- PASS: Docs state that magnitude-only DFT/FFT output is not the canonical decoder input.
- PASS: Docs state that overlap is unsupported in PR1 behavior.
- PASS: Docs capture future boundaries for channelizer implementation, Doppler/noise, overlap support, Metal acceleration, and optional PDW diagnostics.
- PASS: README changes are limited to DSP example indexing.
  - README only adds a pointer to `docs/dsp/fhss_decoder.md`.
- PASS: Guardrail tests exist where repository conventions support them.
  - Added FHSS guardrail tests for RF/baseband labeling, CPU-only/future boundaries, GraphX node canonical model, magnitude-only DFT rejection, and config impairment/overlap disablement.
- PASS: No production RF, external waveform compatibility, GPU/Metal acceleration, channelizer separation, Doppler/noise support, overlap support, or canonical PDW diagnostic claim was added.

## Validation Run

- Build:
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Result: no work to do, target current.
- Focused PR9 guardrail/GraphX tests:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXGuardrailTest.*:FHSSGraphXExecutorTest.*:FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*'`
  - Result: 23 passed.
- Broad FHSS/CPSM regression:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*'`
  - Result: 86 passed.

## Findings

No blocking findings.

## Residual Risk

The documentation points users to test-based execution of the deterministic
lane rather than a standalone FHSS demo runner. That matches the current PR8
implementation surface. If a standalone runner is added later, the PR9 docs and
guardrails should be updated to include that command without weakening the
truth-in-labeling boundaries.
