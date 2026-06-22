# DSP FHSS Decoder PR5 Verifier Report

Role: VERIFIER

PR: PR5 - CPSM Branch Metric And Viterbi/MLSE Decoder

Verdict: Pass

## Files Reviewed

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md`
- `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`
- `libdsp/include/dsp/fhss/FHSSCpsmDecoder.hpp`
- `libgraph/test/unit/test_fhss_cpsm_decoder.cpp`

## Required Checks

- Pass: `CPSMBranchMetricNode` and `CPSMViterbiDecoderNode` exist in `FHSSCpsmDecoder.hpp`.
- Pass: No production `2^32` brute-force pulse decode path exists. The only brute-force code found is `BruteForceOracleReduced` in the PR5 test file, exercised on a six-symbol fixture.
- Pass: Estimator assumptions are explicit and match the fixture:
  - `h = 1/2`
  - rectangular full-response phase pulse via the PR2 `RectangularFullResponsePhasePulse`
  - initial phase state `0`
  - phase state is accumulated CPM phase modulo `2*pi`
  - continuity inside one pulse only
- Pass: `q(t)`, `theta(t)`, branch metric equations, trellis state count, transition table, terminal phase policy, path metric, and confidence metric are defined.
- Pass: Tests prove rectangular full-response `q(t)` keeps `theta(t)` continuous.
- Pass: Tests prove trellis state is accumulated CPM phase modulo `2*pi`.
- Pass: Tests prove a known generated pulse decodes to the expected 32-symbol decisions.
- Pass: Viterbi/MLSE matches a brute-force oracle on a reduced fixture only.
- Pass: Magnitude-only inputs are impossible by decoder type and invalid short evidence is rejected.
- Pass: Decoder decisions come from complex sample evidence. The generated-pulse test uses PR2 synthetic IQ, PR4 dehopped complex evidence, and PR5 Viterbi decisions; it does not feed truth metadata into the decoder.
- Pass: No pulse-word `uint32_t` mapping, preamble detector, message assembler, graph runtime lane, channelizer, Metal/GPU path, or Doppler/noise behavior was added by PR5.

## Technical Notes

- The trellis has four states: `0`, `pi/2`, `pi`, and `3pi/2`.
- Branch transitions use symbol `+1` as a `+1` state advance and symbol `-1` as a `-1 mod 4` state advance.
- Branch cost is `1 - correlation`, where correlation is the mean real part of normalized complex evidence multiplied by the conjugate predicted CPM phasor.
- Viterbi complexity is `O(symbol_count * 4 * 2)`, not exponential in the 32-symbol pulse length.
- Terminal phase is unconstrained by default and can be explicitly checked by expected terminal state.
- Confidence is derived from best-versus-second-best terminal path metric separation normalized by symbol count.

## Tests Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSCpsmDecoderTest.*'`
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*'`
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`

All tests passed.

## Findings

- None.
