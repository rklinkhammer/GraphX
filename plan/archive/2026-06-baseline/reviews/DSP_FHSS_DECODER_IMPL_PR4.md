# DSP FHSS Decoder PR4 Implementer Report

## PR

PR4 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: Correlator-Bank Detector And Candidate Extraction.

## Files Changed

- `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetector.hpp`
  - Added PR1 CPU-only correlator-bank detector helper.
  - Added detector config, per-frequency score reporting, and detection result types.
  - Validates `message_start_sample = 0`, four active selectable frequencies, preamble membership, IQ offset guards, and unsupported-overlap policy.
  - Evaluates only the configured four active frequencies.
  - Uses `iq_offset_frequency_hz` for dehopping, never absolute RF metadata frequency.
  - Emits `FHSSLocalPulseDetection` metadata suitable for `FHSSPulseMergeNode`.
  - Emits dehopped complex evidence for downstream CPSM branch metrics.
- `libgraph/test/unit/test_fhss_correlator_bank_detector.cpp`
  - Added focused PR4 detector tests.

## Files Deleted

- None.

## Tests Added

- `FHSSCorrelatorBankDetectorTest.RejectsReservedActiveFrequencyConfig`
- `FHSSCorrelatorBankDetectorTest.RequiresKnownPr1MessageStartSample`
- `FHSSCorrelatorBankDetectorTest.RanksCorrectActiveFrequencyPerKnownSlot`
- `FHSSCorrelatorBankDetectorTest.EmitsMetadataAndGlobalTimingForMerge`
- `FHSSCorrelatorBankDetectorTest.UsesIqOffsetNotRfFrequencyInComplexExponential`
- `FHSSCorrelatorBankDetectorTest.HandoffToPulseMergePreservesCandidateEvidence`
- `FHSSCorrelatorBankDetectorTest.RejectsUnsupportedOverlapConfiguration`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSCorrelatorBankDetectorTest.*'
```

Result: passed, 7 tests.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*'
```

Result: passed, 37 tests.

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure
```

Result: passed.

## Implementation Notes

- The detector is a pure DSP helper class, not a GraphX runtime node or plugin.
- It assumes PR1 known-slot timing: `message_start_sample = 0` and `N_period = 6500`.
- It chooses the active frequency per slot using a phase-coherence score after dehopping each configured active IQ offset.
- It reports `evaluated_frequency_count` so tests can verify PR1 does not scan the full 64-entry table.
- It emits local detections with RF metadata frequency, IQ offset frequency, estimated center frequency, zero CFO/frequency-error placeholders, SNR/confidence, detector id, packet sequence, global-timing map, and dehopped complex evidence.
- Full CPSM sequence evidence and Viterbi/MLSE remain owned by PR5.

## Scope Guardrails

- No real channelizer was added.
- No preamble decoding, word decoding, CPSM Viterbi/MLSE, or message assembly was added.
- No graph runtime lane, plugin runtime, Metal/GPU, Doppler/noise behavior, or RF performance claim was added.
- The detector does not use magnitude-only `MagnitudePacket` output for word recovery.

## Remaining Follow-Up Work

- PR5 can consume the dehopped complex evidence for CPSM branch metrics and Viterbi/MLSE.
- PR8 can decide whether this helper is wrapped as a runtime graph node/plugin for the end-to-end JSON lane.
