# SAR Verifier Report - PR2

## Pass/fail
FAIL

## Blocking issues
1. PR2 acceptance criterion "canonical definitive topology uses token contract through source and DSP-to-GPU handoff" is not met in runtime contracts.
   - `examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp` still declares source output as `SarPulseBlockMessage`.
   - `examples/SAR/include/sar/RangeWindowNode.hpp` still uses `SarPulseBlockMessage -> SarPulseBlockMessage`.
   - `examples/SAR/include/sar/RangeCompressionNode.hpp` still uses `SarPulseBlockMessage -> SarPulseBlockMessage`.
   - `examples/SAR/include/sar/AzimuthTileSplitNode.hpp` still consumes `SarPulseBlockMessage` and only then emits `SarAccelControlToken`.
   - Pre-GPU stages remain message-contract based, not token-contract continuous through source/DSP.

## Non-blocking issues
1. PR2 added useful verification tests, but they mostly assert config shape and sidecar/timing behavior at split handoff, not proof that source/window/compression are token-typed.
   - `examples/SAR/test/test_sar_pr2_token_contract.cpp`
2. Build and tests are green, which supports stability for current behavior.
   - Full ctest lane passed (5/5).

## Suggested fixes
1. Convert source and DSP stage node contracts to token form for the definitive path.
   - Source emits `SarAccelControlToken`.
   - Range window/compression consume and emit `SarAccelControlToken`.
   - Split becomes token-to-token (or is removed/repurposed if redundant).
2. Update definitive topology wiring and impacted tests to reflect true token continuity before H2D.
3. Keep current PR2 test coverage, and add explicit compile-time/type-level assertions that source/window/compression signatures are token-based.

## Acceptance criteria check
1. Canonical definitive topology uses token contract through source and DSP-to-GPU handoff: NOT SATISFIED.
2. PR2 compiles and tests pass without compatibility shims: SATISFIED.
