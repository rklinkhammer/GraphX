# DSP FHSS Decoder PR4 Verifier Report

## PR

PR4 from `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`: Correlator-Bank Detector And Candidate Extraction.

## Verdict

Pass.

## Findings

No blocking or non-blocking PR4 issues found.

## Required Checks

- Detector uses known-slot `message_start_sample = 0` and `N_period = 6500`: pass.
  - `ValidateCorrelatorBankDetectorConfig` rejects non-zero `message_start_sample`.
  - Detection slot starts use `slot * timing.pulse_period_samples`.
  - Covered by `FHSSCorrelatorBankDetectorTest.RequiresKnownPr1MessageStartSample`.
- Detector ranks the correct active frequency among the four active offsets: pass.
  - Covered by `FHSSCorrelatorBankDetectorTest.RanksCorrectActiveFrequencyPerKnownSlot`.
- Detector configuration rejects reserved edge frequency indices `0` and `63`: pass.
  - Reuses PR1 active-frequency validation.
  - Covered by `FHSSCorrelatorBankDetectorTest.RejectsReservedActiveFrequencyConfig`.
- Detector uses `iq_offset_frequency_hz` and never directly mixes against absolute 1 GHz RF metadata: pass.
  - `DehopPulseSamples` receives `FHSSFrequencyMapEntry::iq_offset_frequency_hz`.
  - Covered by `FHSSCorrelatorBankDetectorTest.UsesIqOffsetForDehoppedEvidence`.
- Detector does not scan the full 64-entry frequency table in PR1: pass.
  - Detector iterates only `decode_config.active_frequency_indices`.
  - `evaluated_frequency_count` is asserted as `4`.
- Detector emits required `FHSSDetectedPulse` metadata and global sample timing: pass.
  - Emits `FHSSLocalPulseDetection` with RF metadata frequency, IQ offset frequency, estimated center frequency, frequency error placeholder, CFO placeholder, SNR/confidence, detector id, packet sequence, and sample-time map.
  - Merge normalization converts it to `FHSSDetectedPulse`.
  - Covered by `FHSSCorrelatorBankDetectorTest.EmitsMetadataAndGlobalTimingForMerge`.
- Detector does not require preamble decoding: pass.
  - Detector uses the configured active set/preamble validation only and does not decode preamble words.
- Detector rejects unsupported overlapped messages: pass.
  - `allow_overlap` is rejected.
  - Covered by `FHSSCorrelatorBankDetectorTest.RejectsUnsupportedOverlapConfiguration`.
- Detector output is sufficient for `FHSSPulseMergeNode` and later CPSM decoding: pass.
  - Emits `FHSSLocalPulseDetection` plus dehopped complex evidence.
  - Covered by `FHSSCorrelatorBankDetectorTest.HandoffToPulseMergePreservesCandidateEvidence`.
- Detector does not rely on magnitude-only `MagnitudePacket` for word recovery: pass.
  - Implementation operates on `std::vector<std::complex<double>>` samples and emits dehopped complex evidence.
- Detector does not duplicate full Viterbi/MLSE work unless a reusable metric handoff contract is explicitly implemented: pass.
  - Implementation computes a lightweight phase-coherence detector score only; no CPSM sequence estimator or word decoder exists in PR4.
- No real channelizer, preamble detector, word decoder, message assembler, Metal/GPU, Doppler/noise behavior, or RF performance claim was added: pass.
  - Scope scan found only FHSS protocol/generator/merge/detector helper headers and unit tests.
  - No FHSS decoder, message assembler, plugin, Metal/GPU, or channelizer symbols were found.

## Tests Run

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: passed, no work to do.

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

## Residual Risk

- `FHSSCorrelatorBankDetectorNode` is currently a pure DSP helper, not a GraphX runtime plugin/node. That matches PR4 scope; PR8 still needs to wire the executable graph lane.
- The detector score is intentionally lightweight fixture detection. PR5 remains responsible for CPSM branch metrics and Viterbi/MLSE sequence estimation.
