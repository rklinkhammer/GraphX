# DSP FHSS Decoder PR Agents

Use these prompts with:

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`
- `plan/prompt examples/fhss.md`

Global constraints for every PR:

- Implement or verify exactly the named PR.
- Keep the first FHSS lane CPU-only, deterministic, and CI-safe.
- Preserve complex IQ evidence through decoding.
- Do not use magnitude-only DFT/FFT output as the canonical decoder input.
- Do not claim production RF compatibility or external waveform compatibility.
- Use `500 Msps`, `5 Mbps`, `100 samples/symbol`, `3200` pulse samples, `3300` gap samples, and `6500` samples per pulse period.
- Use a 64-entry RF metadata table starting at 1 GHz with 8 MHz spacing.
- Treat frequency indices `0` and `63` as reserved edge/guard entries; selectable active indices are `[1, 62]`.
- Use only four active frequencies derived from the 16-pulse preamble pattern.
- Payload/body frequencies must be deterministic random selections from the four active preamble frequencies.
- Treat absolute RF frequencies as metadata; fixture IQ must use baseband/IF offset frequencies.
- Reject unsupported overlapped messages in PR1 behavior.
- Keep Doppler, noise, multipath, real channelizer, Metal/GPU, PDW diagnostics, and pulse-start acquisition out of scope unless the named PR explicitly includes them.
- Stop after the requested implementer or verifier report.

---

## PR1: FHSS Protocol Types, Frequency Map, And Fixture Schema

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Protocol Types, Frequency Map, And Fixture Schema.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Scope:
- Add FHSS protocol/type/config models for truth pulses, detected pulses, pulse candidates, messages, frequency-map entries, and decode configuration.
- Add validation for the 64-entry RF metadata table: 1 GHz base, 8 MHz spacing, indices [0, 63].
- Enforce selectable active frequency indices [1, 62]; reject reserved edge indices 0 and 63 for preamble and payload/body selection.
- Enforce exactly four active preamble frequencies, exactly 16 preamble pulses, and at most 256 total pulses including preamble.
- Add timing helpers/validation for 500 Msps, 5 Mbps, 100 samples/symbol, 3200 pulse samples, 3300 gap samples, and 6500 samples/period.
- Model RF metadata frequency separately from IQ offset frequency.
- Validate IQ offsets against Nyquist, occupied-bandwidth guard, and max CFO guard fields.
- Add deterministic RNG configuration for payload/body selection from the four active frequencies.
- Add focused protocol validation tests and CMake wiring.

Do not add signal generation.
Do not add detector, decoder, graph runtime, plugin runtime, Metal/GPU, real RF capture, channelizer, Doppler/noise behavior, or message assembly.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR1.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Protocol Types, Frequency Map, And Fixture Schema.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- FHSS protocol/type/config model exists and compiles independently.
- Frequency table derives exactly 64 RF metadata entries from 1 GHz at 8 MHz spacing.
- Validation rejects frequency indices outside [0, 63].
- Validation rejects reserved edge indices 0 and 63 for active preamble and payload/body selection.
- Active preamble validation requires exactly four distinct selectable indices.
- Preamble validation requires exactly 16 pulses.
- Message length validation rejects more than 256 pulses including preamble.
- Timing validation proves 500 Msps / 5 Mbps = 100, N_pulse = 3200, N_gap = 3300, and N_period = 6500.
- RF metadata frequency and IQ offset frequency are distinct and validated.
- Full-table validation documents that all 64 RF centers cannot be represented alias-free in one 500 Msps complex-baseband span while preserving 8 MHz spacing.
- No generator, detector, decoder, graph runtime, Metal/GPU, real RF capture, channelizer, or message assembly was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR1.md.
```

---

## PR2: Deterministic FHSS CPSM Synthetic IQ Generator

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Deterministic FHSS CPSM Synthetic IQ Generator.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR1 FHSS protocol types, timing helpers, frequency map, and validation must already exist.

Scope:
- Add a CPU deterministic synthetic IQ generator for the FHSS fixture.
- Encode each uint32_t as one 32-symbol binary CPSM pulse at 5 Mbps and 500 Msps.
- Use h = 1/2, rectangular full-response phase pulse, initial phase 0, and continuity inside each pulse only unless PR1 config names a stricter policy.
- Emit complex IQ using iq_offset_frequency_hz, not rf_frequency_hz, in the complex exponential.
- Emit exact truth metadata for global_start_sample, duration 3200, frequency index, RF metadata frequency, IQ offset frequency, value, and preamble flag.
- Generate payload/body frequencies as deterministic random selections from the four active preamble frequencies.
- Never select reserved edge frequency indices 0 or 63 for preamble or payload/body pulses.
- Enforce identical preamble frequencies having identical preamble word values.
- Keep noise, Doppler, multipath, and overlap disabled/rejected by default.
- Add focused generator tests and CMake wiring.

Do not add detector, decoder, message assembler, graph runtime integration, channelizer, Metal/GPU, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR2.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Deterministic FHSS CPSM Synthetic IQ Generator.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Generator emits the expected sample count for known preamble and payload fixtures.
- Every truth pulse has exact global timing, duration 3200, frequency index, RF metadata frequency, IQ offset frequency, value, and preamble flag.
- Payload/body frequency selection is deterministic from the configured seed and uses only the four active preamble frequencies.
- Generator rejects or never selects reserved edge indices 0 and 63.
- Generated preamble truth enforces identical-frequency/identical-word consistency.
- CPSM output has constant envelope inside noise-free pulses.
- Phase continuity matches the selected rectangular full-response q(t) policy.
- Gap samples are zero or explicitly documented idle samples.
- Generated IQ uses iq_offset_frequency_hz, not rf_frequency_hz.
- Overlap fixtures are rejected for PR1 behavior.
- No detector, decoder, graph runtime integration, channelizer, Metal/GPU, Doppler/noise behavior, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR2.md.
```

---

## PR3: Detected-Pulse Metadata And Pulse Merge/Association

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Detected-Pulse Metadata And Pulse Merge/Association.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR1 FHSS protocol types and timing constants must already exist.

Scope:
- Add detected-pulse and pulse-candidate metadata sufficient for association, ordering, and message assembly before preamble lock.
- Include shared global sample timing: global_start_sample = input_packet_global_start_sample + local_start_offset.
- Add channel-local timing fields only as secondary metadata; channel-local offsets alone must not be accepted for association.
- Add sample-time mapping fields sufficient for future decimated/channelized inputs.
- Add FHSSPulseMergeNode semantics for sorting by global_start_sample, duplicate rejection, collision/overlap reporting, provisional slot index, final slot index, and ordered FHSSPulseCandidate emission.
- Preserve complex sample references/payload through the merge boundary.
- Add focused tests for global timing, sorting, duplicate rejection, collision/overlap policy, slot indexing, metadata validation, and candidate emission.
- Add plugin/CMake wiring only if the PR exposes the merge node through plugins.

Do not add a detector, CPSM decoder, word decoder, preamble detector, message assembler, graph runtime lane, Metal/GPU, or channelizer implementation.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR3.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Detected-Pulse Metadata And Pulse Merge/Association.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- FHSSDetectedPulse/FHSSPulseCandidate metadata includes global timing, channel timing, channel id, frequency index, RF metadata frequency, IQ offset frequency, estimated center frequency, frequency error, amplitude/power/SNR/noise floor, phase/CFO fields, bandwidth, confidence, detector id, and packet sequence where appropriate.
- global_start_sample is derived from input_packet_global_start_sample plus local_start_offset.
- Detected pulses from multiple channels sort by global_start_sample.
- Duplicate detections on the same frequency/window retain the higher-confidence/SNR pulse.
- Cross-frequency collisions follow the PR1 unsupported-overlap policy.
- Provisional slot index uses global_start_sample / N_period.
- Final slot index uses (global_start_sample - message_epoch_sample) / N_period when epoch is known.
- Missing or inconsistent global timing metadata is rejected.
- Complex evidence is preserved through the candidate stream.
- No detector, CPSM decoder, word decoder, preamble detector, message assembler, graph runtime lane, Metal/GPU, or channelizer implementation was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR3.md.
```

---

## PR4: Correlator-Bank Detector And Candidate Extraction

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Correlator-Bank Detector And Candidate Extraction.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR1 FHSS protocol/timing/frequency validation must already exist.
- PR3 detected-pulse metadata and pulse merge/candidate contracts must already exist.

Scope:
- Add the PR1 correlator-bank detector path without a real channelizer.
- Start only from the four active frequencies derived from the preamble pattern.
- Reject active frequency configuration containing reserved edge indices 0 or 63.
- Use message_start_sample = 0 and N_period = 6500 for known-slot PR1 detection.
- Use offset frequencies and never directly mix against absolute 1 GHz RF metadata in the 500 Msps fixture.
- Emit FHSSDetectedPulse metadata with RF metadata frequency, IQ offset frequency, frequency error, CFO placeholder/estimate, SNR/confidence, detector id, packet sequence, and global timing.
- Emit timing/frequency candidate evidence plus dehopped complex samples for downstream CPSM branch metrics.
- Leave full CPSM sequence evidence to PR5 branch metric/Viterbi.
- Add tests for frequency ranking, timing, global metadata, confidence, reserved-index rejection, offset-frequency use, and handoff to merge.

Do not scan the full 64-entry table in PR1.
Do not add a real channelizer.
Do not decode preamble or words.
Do not implement CPSM Viterbi/MLSE, message assembly, Metal/GPU, Doppler/noise behavior, or RF performance claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR4.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Correlator-Bank Detector And Candidate Extraction.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Detector uses known-slot message_start_sample = 0 and N_period = 6500.
- Detector ranks the correct active frequency among the four active offsets.
- Detector configuration rejects reserved edge frequency indices 0 and 63.
- Detector uses iq_offset_frequency_hz and never directly mixes against absolute 1 GHz RF metadata.
- Detector does not scan the full 64-entry frequency table in PR1.
- Detector emits required FHSSDetectedPulse metadata and global sample timing.
- Detector does not require preamble decoding.
- Detector rejects unsupported overlapped messages.
- Detector output is sufficient for FHSSPulseMergeNode and later CPSM decoding.
- Detector does not rely on magnitude-only MagnitudePacket for word recovery.
- Detector does not duplicate full Viterbi/MLSE work unless a reusable metric handoff contract is explicitly implemented.
- No real channelizer, preamble detector, word decoder, message assembler, Metal/GPU, Doppler/noise behavior, or RF performance claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR4.md.
```

---

## PR5: CPSM Branch Metric And Viterbi/MLSE Decoder

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: CPSM Branch Metric And Viterbi/MLSE Decoder.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR2 generator must define the CPSM fixture waveform.
- PR4 detector/candidate evidence must provide complex dehopped samples or equivalent complex pulse evidence.

Scope:
- Add CPSMBranchMetricNode and CPSMViterbiDecoderNode for one pulse.
- Implement efficient binary CPSM sequence estimation without brute force over 2^32 full pulse words.
- Pin down the rectangular full-response q(t) normalization/support used by both generator and decoder.
- Use h = 1/2, binary symbols a[k] in {-1,+1}, initial phase 0, phase state as accumulated CPM phase modulo 2*pi, continuity inside each pulse only, and explicit terminal phase policy.
- Define branch metric equations, matched-filter/integrate-and-dump/normalization assumptions, trellis state count, transition table, path metric, and confidence metric.
- Add reduced-length brute-force oracle tests only for small fixtures to verify Viterbi/MLSE behavior.
- Add tests for continuous theta(t), trellis transitions, known generated pulse decode, path metric separation, terminal phase policy, and magnitude-only input rejection/type impossibility.

Do not enumerate 2^32 pulse words.
Do not use truth metadata for sequence decisions.
Do not add pulse-word uint32_t mapping, preamble detection, message assembly, graph runtime lane, channelizer, Metal/GPU, or Doppler/noise behavior.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR5.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: CPSM Branch Metric And Viterbi/MLSE Decoder.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- CPSMBranchMetricNode and CPSMViterbiDecoderNode exist or equivalent scoped implementation exists.
- No 2^32 brute-force pulse decode path exists.
- Estimator assumptions are explicit and match the fixture: h = 1/2, rectangular full-response, initial phase 0, continuity inside each pulse only.
- q(t), theta(t), branch metric equations, trellis state count, transition table, terminal phase policy, and confidence metric are defined.
- Tests prove rectangular full-response q(t) makes theta(t) continuous.
- Tests prove trellis state is accumulated CPM phase modulo 2*pi.
- Tests prove known generated pulses decode to expected 32-symbol decisions.
- Viterbi/MLSE matches a brute-force oracle on reduced fixtures only.
- Magnitude-only inputs are rejected or impossible by type.
- Decoder decisions come from complex IQ evidence, not truth metadata.
- No pulse-word uint32_t mapping, preamble detection, message assembly, graph runtime lane, channelizer, Metal/GPU, or Doppler/noise behavior was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR5.md.
```

---

## PR6: FHSS Pulse Word Decoder

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Pulse Word Decoder.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR5 CPSM symbol decisions/confidence must already exist.

Scope:
- Add FHSSPulseWordDecoderNode.
- Convert exactly 32 decoded CPSM symbol decisions into one uint32_t per pulse.
- Use +1 -> 0 and -1 -> 1 bit mapping.
- Use MSB-first value assembly: value = (value << 1) | bit.
- Preserve pulse timing/frequency metadata and attach decoded value/confidence/status.
- Report low-confidence or invalid Viterbi output with diagnosable status.
- Add tests for bit mapping, MSB-first assembly, known vectors, metadata preservation, confidence propagation, and bad decode reporting.

Do not introduce byte/nibble/symbol-fragment message models.
Do not decode from truth metadata.
Do not add preamble detection, message assembly, graph runtime lane, channelizer, Metal/GPU, or Doppler/noise behavior.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR6.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Pulse Word Decoder.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- FHSSPulseWordDecoderNode exists or equivalent scoped implementation exists.
- +1 maps to bit 0 and -1 maps to bit 1.
- Exactly 32 symbols produce exactly one uint32_t.
- MSB-first assembly uses value = (value << 1) | bit.
- Known symbol vectors recover expected uint32_t values.
- Pulse timing/frequency metadata survives through decoded pulse output.
- Low-confidence or invalid Viterbi output produces diagnosable error/status.
- Decoder consumes complex-derived CPSM decisions, not truth metadata.
- No byte/nibble/symbol-fragment model, preamble detector, message assembler, graph runtime lane, channelizer, Metal/GPU, or Doppler/noise behavior was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR6.md.
```

---

## PR7: Hop-Only Preamble Detector And Message Assembler

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Hop-Only Preamble Detector And Message Assembler.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR6 decoded pulse words with timing/frequency metadata must already exist.

Scope:
- Add FHSSPreambleDetectorNode, FHSSMessageAssemblerNode, and FHSSMessageSinkNode or repository-consistent equivalents.
- Implement hop-only preamble lock over exactly 16 pulses.
- Treat preamble word values as fixture consistency / secondary validation only.
- Enforce active set after lock as exactly four selectable frequencies.
- Reject payload frequencies outside the active set.
- Reject total message length over 256 pulses including preamble.
- Reject missing preamble.
- Preserve globally ordered decoded pulse behavior.
- Add truth comparison hooks for start, duration, frequency, and value mismatches.
- Add minimum diagnostics: pulse_count, rejected_count, preamble_lock, and truth_mismatch_count.
- Add unit tests for preamble lock, active set, payload validation, length checks, missing preamble, truth comparison, and PR1 overlap rejection.

Do not make word mismatches prevent hop-only preamble lock.
Do not add graph runtime integration, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR7.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Hop-Only Preamble Detector And Message Assembler.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Preamble lock is hop-only over exactly 16 pulses.
- Word mismatches do not prevent preamble lock.
- Identical preamble frequencies still require identical fixture word values.
- Active set after lock has exactly four selectable frequencies.
- Payload frequencies outside active set are rejected.
- Total message length over 256 pulses including preamble is rejected.
- Missing preamble is rejected.
- Message assembly operates on globally ordered decoded pulses.
- Truth comparator reports mismatched start, duration, frequency, and value.
- Diagnostics include at least pulse_count, rejected_count, preamble_lock, and truth_mismatch_count.
- Overlapped messages remain unsupported and are rejected deterministically.
- No graph runtime integration, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR7.md.
```

---

## PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR8 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR1 through PR7 must already exist.

Scope:
- Add a repository-consistent FHSS CPSM fixture graph JSON such as libdsp/config/fhss_cpsm_fixture_500msps.json.
- Wire synthetic IQ through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, hop-only preamble lock, message assembly, and truth comparison.
- Register/load FHSS nodes through the existing plugin/provider path.
- Add an end-to-end executor test that runs the deterministic CPU fixture to completion.
- Verify decoded pulses match truth for start, duration, frequency index, and value.
- Verify assembled message locks on hop-only preamble and validates active set.
- Emit minimum deterministic diagnostics: pulse_count, rejected_count, global_start_sample, frequency_index, confidence, viterbi_path_metric, decoded_value, preamble_lock, truth_mismatch_count, RF metadata frequency, IQ offset frequency, sample-time mapping, synchronization assumption, and unsupported-overlap/unsupported-impairment rejection when relevant.

Do not add real RF capture, external datasets, real channelizer topology, Metal/GPU, production RF claims, Doppler/noise behavior, or optional PDW diagnostics as canonical output.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR8.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR8 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- FHSS graph config loads nodes through the existing plugin/provider path.
- Executor runs the full deterministic fixture to completion.
- Synthetic IQ flows through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, preamble lock, message assembly, and truth comparison.
- Decoded pulses match truth for start, duration, frequency index, and value.
- Assembled message locks on hop-only preamble and validates the four-frequency active set.
- Diagnostics contain all required minimum fields from the roadmap.
- Graph is CPU-only and does not use Metal/GPU nodes.
- Complex IQ evidence is preserved through word recovery.
- No real RF capture, external dataset, real channelizer topology, Doppler/noise behavior, production RF claim, or canonical PDW diagnostic path was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR8.md.
```

---

## PR9: Documentation And Truth-In-Labeling Guardrails

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR9 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Documentation And Truth-In-Labeling Guardrails.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR8 end-to-end FHSS lane should already exist, or docs must name any missing implementation pieces accurately.

Scope:
- Add docs/dsp/fhss_decoder.md or a repository-consistent FHSS DSP doc.
- Document the deterministic FHSS fixture lane, protocol limits, CPSM assumptions, baseband/offset frequency mapping, unsupported overlap policy, and future boundaries.
- Explain the 64-entry RF metadata table, selectable indices [1, 62], reserved edge indices 0 and 63, 16-pulse hop-only preamble, four active frequencies, and 256-pulse maximum.
- Explain that 1 GHz RF frequencies are metadata and fixture IQ uses baseband/IF offsets at 500 Msps.
- Explain that magnitude-only DFT/FFT output is not the canonical decoder input.
- Explain CPU-only PR1-through-PR8 behavior and future boundaries for channelizer, Doppler/noise, overlap support, Metal acceleration, and optional PDW diagnostics.
- Update README only if DSP examples are indexed there.
- Add guardrail tests where existing conventions support them.

Do not claim production RF compatibility, external waveform compatibility, direct 1 GHz RF sampling at 500 Msps, GPU/Metal acceleration, channelizer separation, Doppler/noise support, overlap support, or PDW diagnostics as canonical decoder output.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR9.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR9 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Documentation And Truth-In-Labeling Guardrails.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- FHSS docs explain how to build/run the deterministic lane.
- Docs describe fixture-only behavior versus future production-like work.
- Docs mention baseband/offset frequencies and do not claim direct 1 GHz RF sampling at 500 Msps.
- Docs describe the 64-entry RF metadata table, selectable indices [1, 62], and reserved edge indices 0 and 63.
- Docs/config identify CPU-only behavior.
- Docs state that magnitude-only DFT/FFT output is not the canonical decoder input.
- Docs state that overlap is unsupported in PR1 behavior.
- Docs capture future boundaries for channelizer implementation, Doppler/noise, overlap support, Metal acceleration, and optional PDW diagnostics.
- README changes, if any, are limited to DSP example indexing.
- Guardrail tests exist where repository conventions support them.
- No production RF, external waveform compatibility, GPU/Metal acceleration, channelizer separation, Doppler/noise support, overlap support, or canonical PDW diagnostic claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR9.md.
```
