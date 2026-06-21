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
- Use only four active transmitted frequencies derived from the 16-pulse preamble pattern unless the named PR explicitly changes message/source configuration.
- PR1-PR9 payload/body frequencies use deterministic random selections from the four active preamble frequencies. PR10+ replaces this with explicit configured message pulse schedules.
- Treat absolute RF frequencies as metadata; fixture IQ must use baseband/IF offset frequencies and explicit IQ reference-frame metadata.
- PR11+ channelized work must preserve the invariant that the receiver/channelizer model has one logical GraphX output channel per configured FHSS frequency entry; the four active transmitted frequencies are not a receiver channel-count limit.
- PR12B+ requires `ChannelizerNode` to expose exactly 64 GraphX output ports for the 64-entry FHSS table. Output port `N` maps to frequency index/channel id `N`. A vector/list/stream sidecar, aggregate packet, fanout payload, or single edge carrying multiple channel packets does not satisfy this invariant.
- PR11+ channelized work must include an explicit `FHSSDownconverterNode` before `ChannelizerNode`; the downconverter may be validated passthrough when source and channelizer IQ reference frames already match.
- Reject unsupported overlapped messages in PR1 behavior.
- Keep Doppler, noise, multipath, Metal/GPU, PDW diagnostics, overlap-aware separation, and pulse-start acquisition out of scope unless the named PR explicitly includes them.
- After PR7, FHSS `...Node` names mean real GraphX nodes using GraphX edge packet contracts. The earlier PR1-PR7 helper/pseudo-node scaffolding must be replaced and removed by PR7A-PR7D before PR8 graph JSON/runtime work.
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
- Add FHSSPreambleDetectorNode, FHSSMessageAssemblerNode, and FHSSMessageSinkNode as temporary pre-GraphX scaffolding only; PR7A-PR7D must replace them with real dynamically loadable GraphX nodes before PR8.
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

## PR7A: FHSS GraphX Edge Contracts And Accel-Ready Data Model

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7A from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS GraphX Edge Contracts And Accel-Ready Data Model.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR1 through PR7 helper/scaffold behavior must already exist.

Scope:
- Define canonical GraphX FHSS edge packet/contract types for synthetic IQ output, detected pulse/candidate evidence, CPSM metrics/symbol decisions, decoded pulse words, assembled messages, and diagnostics.
- Make complex IQ evidence ownership/reference semantics explicit for GraphX edges.
- Preserve global sample time, RF metadata frequency, IQ offset frequency, and future sample-time mapping fields in the packet model.
- Document and test the future accelerator-token/sidecar compatibility boundary without adding Metal/GPU execution.
- Ensure decoder decision contracts do not depend on truth metadata.
- Add focused compile/runtime contract tests.

Do not convert helper pseudo-nodes into runtime nodes in this PR.
Do not add graph JSON, plugin runtime wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR7A.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7A from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS GraphX Edge Contracts And Accel-Ready Data Model.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Canonical GraphX FHSS edge packet/contract types exist for every target graph edge.
- Complex IQ evidence ownership/reference semantics are explicit and suitable for GraphX edges.
- Global sample time, RF metadata frequency, IQ offset frequency, and sample-time mapping fields survive the packet model.
- Future accelerator-token/sidecar compatibility is documented/tested without adding GPU execution.
- Decoder decision contracts do not depend on truth metadata.
- No helper pseudo-node is made canonical by the new contracts.
- No graph JSON, plugin runtime wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR7A.md.
```

---

## PR7B: Replace FHSS Pseudo-Nodes With Real GraphX Nodes

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7B from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Replace FHSS Pseudo-Nodes With Real GraphX Nodes.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR7A GraphX FHSS edge packet/contract types must already exist.
- PR1 through PR7 helper/scaffold tests identify the behavior that must be preserved through GraphX node tests.

Scope:
- Replace every FHSS `...Node` pseudo-node/helper class with a repository-consistent real GraphX node.
- Implement GraphX nodes for the target CPU lane: synthetic IQ source, correlator-bank detector, pulse merge/candidate boundary, CPSM branch metric, CPSM Viterbi, pulse-word decoder, preamble detector, message assembler, and message sink.
- Use PR7A GraphX FHSS edge packet/contract types for all node inputs and outputs.
- Require every FHSS GraphX node input/output edge data type to be a `graph::gpu::accel::ControlToken<...>` template type, following the existing `DspIqH2DNode` and `CpuSpectrumDftNode` pattern.
- Carry PR7A FHSS packet contracts as token sidecars/payloads, either as `graph::gpu::accel::ControlToken<PR7A_PACKET_TYPE>` or, where repository-consistent, `graph::gpu::accel::ControlToken<graph::message::Message>` containing the PR7A packet. Raw PR7A packet types must not appear as GraphX node port types.
- Rewrite tests so FHSS node behavior is exercised through the GraphX node API and packet contracts, not direct calls to old helper `Node` classes.
- Add compile-time/type-contract tests proving every FHSS node input/output port type is token-wrapped and accel-ready.
- Preserve PR1-PR7 deterministic behavior through the new GraphX node tests.
- Add plugin/provider registration tests for any node exposed through the plugin path in this PR.
- Delete old public pre-GraphX pseudo-node headers/classes and direct pseudo-node tests once equivalent GraphX node coverage exists.

Do not keep compatibility shims for old pseudo-node APIs.
Do not add graph JSON end-to-end executor wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR7B.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7B from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Replace FHSS Pseudo-Nodes With Real GraphX Nodes.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Every FHSS `...Node` named component in the target graph is a real GraphX node.
- GraphX nodes use PR7A edge packet/contract types for inputs and outputs.
- Every FHSS GraphX node input/output edge data type is a `graph::gpu::accel::ControlToken<...>` template type, following the `DspIqH2DNode` / `CpuSpectrumDftNode` pattern.
- Raw PR7A FHSS packet types are not exposed directly as GraphX node port types.
- Type-contract tests prove PR7A FHSS packet contracts are carried as token sidecars/payloads and preserve FHSS semantic metadata independently from future accelerator transport state.
- Old public pre-GraphX pseudo-node headers/classes are deleted or renamed into private non-node algorithm kernels.
- No compatibility shim preserves the old pseudo-node API.
- FHSS tests exercise GraphX node APIs and packet contracts, not direct old helper `Node` calls.
- PR1-PR7 behavior remains covered through GraphX node tests.
- Plugin/provider registration tests exist for nodes exposed through plugins.
- No graph JSON end-to-end executor wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR7B.md.
```

---

## PR7C: Remove Pre-GraphX FHSS Node Scaffolding And Guard Against Regression

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7C from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Remove Pre-GraphX FHSS Node Scaffolding And Guard Against Regression.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR7B real GraphX FHSS nodes and rewritten GraphX node tests must already exist.

Scope:
- Delete all remaining public FHSS helper/pseudo-node types with `Node` names that are not real GraphX nodes.
- Delete all remaining tests that directly exercise old pseudo-node APIs.
- Add guardrail tests preventing new public FHSS pseudo-node headers/classes from returning.
- Add guardrail tests ensuring FHSS tests do not include deleted pseudo-node headers.
- Keep the GraphX node test suite passing after deletion.

Do not preserve backward compatibility for old pseudo-node APIs.
Do not add graph JSON end-to-end executor wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR7C.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7C from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Remove Pre-GraphX FHSS Node Scaffolding And Guard Against Regression.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- No public FHSS `...Node` class/header remains unless it is a real GraphX node.
- No tests directly include or call deleted pre-GraphX pseudo-node APIs.
- Guardrail tests prevent pseudo-node scaffolding from returning.
- GraphX node tests remain the canonical FHSS node tests and pass.
- No backward compatibility shim for old pseudo-node APIs remains.
- No graph JSON end-to-end executor wiring, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR7C.md.
```

---

## PR7D: Split FHSS GraphX Nodes Into Per-Node Files And Plugins

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7D from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Split FHSS GraphX Nodes Into Per-Node Files And Plugins.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR7A GraphX FHSS edge packet/contract types must already exist.
- PR7B real GraphX FHSS nodes with `graph::gpu::accel::ControlToken<...>` ports must already exist.
- PR7C guardrails preventing pre-GraphX pseudo-node regression must already exist.

Scope:
- Split the temporary unified FHSS GraphX node header into one header/source pair per FHSS GraphX node:
  - `FHSSSyntheticIqSourceNode`
  - `FHSSCorrelatorBankDetectorNode`
  - `FHSSPulseMergeNode`
  - `FHSSPulseCandidateNode`
  - `CPSMBranchMetricNode`
  - `CPSMViterbiDecoderNode`
  - `FHSSPulseWordDecoderNode`
  - `FHSSPreambleDetectorNode`
  - `FHSSMessageAssemblerNode`
  - `FHSSMessageSinkNode`
- Move shared token/metadata conversion helpers into a repository-consistent FHSS GraphX node utility header/source that defines no `...Node` classes.
- Delete the unified FHSS GraphX node-definition header after per-node headers/sources replace it.
- Add each FHSS GraphX node to the repository plugin/provider system for dynamic loading.
- Add plugin/provider registration and dynamic-loading tests for every FHSS GraphX node.
- Update existing FHSS GraphX node tests to include per-node headers rather than the deleted unified header.
- Keep all FHSS node edge data types as `graph::gpu::accel::ControlToken<...>` carrying PR7A packet sidecars.
- Keep PR7C guardrails passing.

Do not add graph JSON end-to-end executor wiring, real channelizer, Metal/GPU execution, Doppler/noise behavior, overlap-aware separation, or production RF claims.
Do not preserve the unified FHSS node-definition header as a compatibility shim.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR7D.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7D from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Split FHSS GraphX Nodes Into Per-Node Files And Plugins.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Each FHSS GraphX node has its own header and source file.
- The unified FHSS GraphX node-definition header/source was deleted and is not preserved as a compatibility shim.
- Shared FHSS GraphX node utility files define no `...Node` classes.
- Every FHSS GraphX node remains a real GraphX node and every node edge remains a `graph::gpu::accel::ControlToken<...>` carrying PR7A packet sidecars.
- Every FHSS GraphX node is registered with the plugin/provider system.
- Dynamic-loading tests prove every FHSS GraphX node can be resolved through the plugin system.
- Existing FHSS GraphX node tests include per-node headers, not the deleted unified node-definition header.
- PR7C guardrails still pass.
- No graph JSON end-to-end executor wiring, real channelizer, Metal/GPU execution, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR7D.md.
```

---

## PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR8 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR1 through PR7 must already exist as validated behavior.
- PR7A through PR7D must already have replaced the pre-GraphX pseudo-node scaffolding with real dynamically loadable GraphX FHSS nodes and edge contracts.

Scope:
- Add a repository-consistent FHSS CPSM fixture graph JSON such as libdsp/config/fhss_cpsm_fixture_500msps.json.
- Wire synthetic IQ through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, hop-only preamble lock, message assembly, and truth comparison.
- Load FHSS nodes through the PR7D plugin/provider path.
- Use only real GraphX FHSS nodes and PR7A edge packet/contract types; do not reference deleted pre-GraphX pseudo-node helpers or the deleted unified FHSS node-definition header.
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
- FHSS graph config loads nodes through the PR7D plugin/provider path.
- FHSS graph config uses only real GraphX FHSS nodes and PR7A edge packet/contract types.
- No deleted pre-GraphX pseudo-node helper or deleted unified FHSS node-definition header is referenced by config, plugins, tests, or runtime wiring.
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
- PR7A through PR7D should already have replaced pre-GraphX pseudo-node scaffolding with real dynamically loadable GraphX FHSS nodes, or docs must clearly identify that as incomplete.

Scope:
- Add docs/dsp/fhss_decoder.md or a repository-consistent FHSS DSP doc.
- Document the deterministic FHSS fixture lane, protocol limits, CPSM assumptions, baseband/offset frequency mapping, unsupported overlap policy, and future boundaries.
- Explain the 64-entry RF metadata table, selectable indices [1, 62], reserved edge indices 0 and 63, 16-pulse hop-only preamble, four active frequencies, and 256-pulse maximum.
- Explain that 1 GHz RF frequencies are metadata and fixture IQ uses baseband/IF offsets at 500 Msps.
- Explain that magnitude-only DFT/FFT output is not the canonical decoder input.
- Explain CPU-only PR1-through-PR8 behavior and future boundaries for channelizer, Doppler/noise, overlap support, Metal acceleration, and optional PDW diagnostics.
- Explain that the canonical FHSS implementation uses GraphX nodes and GraphX edge packet contracts, not the deleted pre-GraphX pseudo-node scaffolding.
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
- Docs/config identify GraphX nodes and GraphX edge contracts as the canonical FHSS model and do not describe deleted pre-GraphX pseudo-nodes as current.
- Docs state that magnitude-only DFT/FFT output is not the canonical decoder input.
- Docs state that overlap is unsupported in PR1 behavior.
- Docs capture future boundaries for channelizer implementation, Doppler/noise, overlap support, Metal acceleration, and optional PDW diagnostics.
- README changes, if any, are limited to DSP example indexing.
- Guardrail tests exist where repository conventions support them.
- No production RF, external waveform compatibility, GPU/Metal acceleration, channelizer separation, Doppler/noise support, overlap support, or canonical PDW diagnostic claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR9.md.
```

---

## PR10: Explicit FHSS IQ Source Message Schedule And Frequency Mapping

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR10 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Explicit FHSS IQ Source Message Schedule And Frequency Mapping.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR8 end-to-end fixture behavior and PR7A-PR7D GraphX node contracts should already exist.

Scope:
- Replace the PR8 source fixture's implicit single-message/random-payload model with an explicit configured `messages[]` schedule.
- Add message schedule protocol/config types with stable `message_id`, transmit time, and ordered pulse list.
- Let each message specify `transmit_start_sample` or a validated equivalent transmit time converted to global samples.
- Require every configured pulse to explicitly provide `frequency_index`, `uint32_t value`, and preamble/body role.
- Remove source dependence on `payload_random_seed`, `payload_random_deterministic`, and random payload/body frequency selection once equivalent explicit-message tests exist.
- Derive `iq_offset_frequency_hz` from supplied RF table entries and explicit `iq_center_frequency_hz`.
- Validate all derived IQ offsets against Nyquist, occupied-bandwidth guard, and max CFO guard fields.
- Support zero configured messages with explicit deterministic idle behavior and explicit output duration; default idle mode should be zero/NULL complex samples.
- Fill gaps before, between, and after scheduled messages according to the explicit idle policy.
- Reject reserved indices 0 and 63 in any transmitted preamble/body pulse.
- Reject messages over 256 pulses including preamble.
- Reject overlapping scheduled messages in PR10.
- Preserve identical-frequency preamble word consistency.
- Rewrite the PR8 fixture config into the new explicit-message schema.
- Update docs for the explicit source message schedule.
- Add focused config/source/generator tests and CMake wiring.

Do not add receiver acquisition, overlap-aware separation, channelizer implementation, downconverter implementation, Doppler/noise behavior, Metal/GPU, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR10.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR10 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Explicit FHSS IQ Source Message Schedule And Frequency Mapping.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Source config accepts `messages[]` with multiple messages.
- Each message has a stable `message_id`.
- Each message has `transmit_start_sample` or validated equivalent transmit time converted to global samples.
- Every pulse explicitly supplies `frequency_index`, `value`, and preamble/body role.
- Generated truth metadata matches configured message id, transmit time, global pulse start, duration, frequency index, RF metadata frequency, derived IQ offset frequency, value, and preamble flag.
- Payload/body pulse frequencies are not selected randomly.
- Removed/deprecated random payload fields are no longer required by the source path.
- Source rejects reserved indices 0 and 63 in any transmitted pulse.
- Source rejects overlength messages above 256 pulses including preamble.
- Source rejects overlapping scheduled messages.
- Source validates identical-frequency preamble word consistency.
- IQ offsets are derived as `rf_frequency_hz - iq_center_frequency_hz` and checked against Nyquist, occupied-bandwidth, and CFO guards.
- Zero-message config emits deterministic idle samples according to explicit idle mode and explicit output duration.
- Existing deterministic PR8 fixture behavior remains covered through the new explicit-message schema.
- No receiver acquisition, overlap-aware separation, channelizer implementation, downconverter implementation, Doppler/noise behavior, Metal/GPU, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR10.md.
```

---

## PR11: FHSS Channelizer And Per-Channel Edge Contracts

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR11 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Channelizer And Per-Channel Edge Contracts.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR7A GraphX FHSS edge packet/contract types must already exist.
- PR10 explicit source message schedule should exist, or any missing pieces must be named accurately in the report.

Scope:
- Define GraphX packet/contract types for the source-to-downconverter, downconverter-to-channelizer, channelizer-to-per-channel-detector, and per-channel-detector-to-merge edges.
- Define an `FHSSDownconvertedIqPacket` or repository-consistent equivalent for IQ entering the channelizer.
- Define `FHSSChannelizedIqPacket` or equivalent for per-frequency channel IQ.
- Define `FHSSPerChannelPulseEvidencePacket` or equivalent for per-channel detector output.
- Make downconverter metadata explicit: input IQ center/reference frequency, output/channelizer center/reference frequency, translation frequency, passthrough flag, phase convention, sample rate, and preserved global sample origin.
- Make channelizer metadata explicit: channel id, frequency index, RF metadata frequency, IQ offset frequency, channel sample rate, decimation factor, filter group delay, and input global sample origin.
- Enforce the channel-per-frequency invariant in contracts: channel count equals configured FHSS frequency count.
- Permit reserved indices 0 and 63 as receiver guard/metadata channels while keeping them invalid for transmitted preamble/body selection.
- Preserve complex IQ evidence and sample-time mapping through all contracts.
- Ensure every new GraphX edge type is token-wrapped with `graph::gpu::accel::ControlToken<...>`.
- Add focused compile/runtime contract tests.

Do not implement downconverter DSP, channelizer DSP, per-channel detector DSP, graph JSON, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR11.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR11 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS Channelizer And Per-Channel Edge Contracts.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- `FHSSDownconvertedIqPacket` or equivalent exists for IQ entering the channelizer.
- `FHSSChannelizedIqPacket` or equivalent exists for per-channel IQ.
- `FHSSPerChannelPulseEvidencePacket` or equivalent exists for per-channel detector output.
- Downconverter contract states passthrough versus frequency translation and preserves source global sample timing.
- Downconverter contract includes input center/reference, output/channelizer center/reference, translation frequency, passthrough flag, phase convention, sample rate, and global sample origin.
- Channelizer contract includes channel id, frequency index, RF metadata frequency, IQ offset frequency, channel sample rate, decimation factor, group delay, and input global sample origin.
- Contract states channel count equals configured frequency count.
- Channel ids map one-to-one with configured frequency indices.
- Reserved indices 0 and 63 can exist as receiver channels while remaining invalid for transmitted preamble/body selection.
- Complex IQ evidence and global sample-time mapping survive the packet contracts.
- New GraphX edge types are `graph::gpu::accel::ControlToken<...>` token-wrapped.
- No downconverter DSP, channelizer DSP, detector DSP, graph JSON, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR11.md.
```

---

## PR12: FHSS DownconverterNode And Frequency-Parallel CPU ChannelizerNode

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR12 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS DownconverterNode And Frequency-Parallel CPU ChannelizerNode.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR11 downconverter/channelizer edge contracts must already exist.
- PR7B-PR7D real GraphX node and plugin/provider conventions must already exist.

Scope:
- Add a real GraphX `FHSSDownconverterNode`.
- Add a real GraphX `ChannelizerNode`.
- Use PR11 packet contracts and `graph::gpu::accel::ControlToken<...>` port types for all node inputs/outputs.
- Implement downconverter validated passthrough when source and channelizer IQ reference frames match.
- Implement downconverter frequency translation by declared IQ offset delta when reference frames differ.
- Ensure downconverter never mixes by absolute 1 GHz RF metadata at 500 Msps.
- Reject implicit frequency-frame mismatches.
- Implement CPU channelizer output with one logical channel packet per configured FHSS frequency index.
- Preserve RF metadata frequency, IQ offset frequency, channel id, channel sample rate, decimation factor, group delay, and input global sample origin in every channel packet.
- Allow reserved indices 0 and 63 as receiver guard/metadata channels while rejecting them as transmitted active/pulse frequencies.
- Reject duplicate configured frequency indices and duplicate channel ids.
- Add plugin/provider registration if these nodes are dynamically loadable in this PR.
- Add focused GraphX node API, type-contract, registration, passthrough, frequency-translation, and channel-count tests.

Do not replace the PR8 graph yet unless the roadmap explicitly wires it in a later PR.
Do not add per-channel pulse detector implementation, graph JSON end-to-end executor wiring, real RF capture, production channelizer claims, Metal/GPU, Doppler/noise behavior, or overlap-aware separation.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR12.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR12 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: FHSS DownconverterNode And Frequency-Parallel CPU ChannelizerNode.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- `FHSSDownconverterNode` exists as a real GraphX node.
- `ChannelizerNode` exists as a real GraphX node.
- All node ports use `graph::gpu::accel::ControlToken<...>` carrying PR11 packet contracts.
- Downconverter config declares source IQ center/reference, channelizer center/reference, translation frequency, and passthrough mode.
- Downconverter passthrough preserves complex samples and global timing exactly when reference frames match.
- Downconverter frequency translation mixes by declared IQ offset delta, not absolute 1 GHz RF metadata.
- Downconverter rejects implicit frequency-frame mismatches.
- Channelizer emits one channel packet per configured frequency index.
- Channelizer enforces channel count equals configured frequency count.
- Reserved indices 0 and 63 may exist as receiver channels but are rejected as transmitted active/pulse frequencies.
- Duplicate configured frequency indices or duplicate channel ids are rejected.
- Channel packets preserve RF metadata frequency, IQ offset frequency, channel id, channel sample rate, decimation factor, group delay, and input global sample origin.
- Plugin/provider registration tests exist if nodes are exposed through plugins.
- No per-channel detector implementation, graph JSON end-to-end executor wiring, real RF capture, production channelizer claim, Metal/GPU, Doppler/noise behavior, or overlap-aware separation was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR12.md.
```

---

## PR12B: Correct Channelizer Graph Shape To 64 Output Ports

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR12B from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Correct Channelizer Graph Shape To 64 Output Ports.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR12 `FHSSDownconverterNode` and `ChannelizerNode` should already exist, or any missing pieces must be named accurately in the report.
- PR11 `FHSSChannelizedIqPacket` and token-ready GraphX packet contracts must already exist.

Scope:
- Correct `ChannelizerNode` so the channel-per-frequency invariant is represented by GraphX output ports, not by an aggregate sidecar payload.
- Require `ChannelizerNode` to expose exactly 64 GraphX output ports for the 64-entry FHSS frequency table.
- Require every `ChannelizerNode` output port to be `graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>`.
- Require output port `N` to emit the channel packet for frequency index `N` and channel id `N`.
- Preserve reserved receiver guard/metadata output ports 0 and 63 while keeping indices 0 and 63 invalid for transmitted preamble/body pulses.
- Remove or de-canonicalize any `FHSSChannelizedIqStreamPacket`, `FHSSChannelizedIqStreamToken`, vector/list stream sidecar, fanout payload, or other aggregate single-edge channelizer output contract.
- Rewrite tests that previously accepted one edge carrying 64 channel packets.
- Add compile-time tests proving exactly 64 output ports and representative output port types for ports 0, 1, 62, and 63.
- Add runtime tests proving output port `N` emits metadata with `frequency_index == N` and `channel_id == N`.
- Add guardrail tests preventing aggregate channelizer output contracts from returning as canonical GraphX node port types.
- Preserve PR12 downconverter passthrough and declared frequency-translation behavior.
- Keep plugin/provider dynamic loading for the corrected `ChannelizerNode`.

Do not add per-channel pulse detector implementation, graph JSON end-to-end executor wiring, real RF capture, production channelizer claims, Metal/GPU, Doppler/noise behavior, or overlap-aware separation.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR12B.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR12B from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Correct Channelizer Graph Shape To 64 Output Ports.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- `ChannelizerNode` exposes exactly 64 GraphX output ports.
- Every `ChannelizerNode` output port type is `graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>`.
- Output port `N` maps to frequency index `N` and channel id `N`.
- Ports 0 and 63 exist as receiver guard/metadata channels while indices 0 and 63 remain invalid transmitted preamble/body frequencies.
- No `FHSSChannelizedIqStreamPacket`, `FHSSChannelizedIqStreamToken`, vector/list stream sidecar, fanout payload, or aggregate single-edge channelizer output is canonical or used as a `ChannelizerNode` GraphX output port type.
- Tests prove representative ports 0, 1, 62, and 63 are token-wrapped `FHSSChannelizedIqPacket` outputs.
- Tests prove the corrected `ChannelizerNode` remains a real GraphX node and remains plugin/provider loadable.
- PR12 downconverter passthrough and declared frequency-translation behavior remains covered.
- PR13 can instantiate one `PerChannelPulseDetectorNode` per channelizer output port when PR13 is implemented.
- No per-channel pulse detector implementation, graph JSON end-to-end executor wiring, real RF capture, production channelizer claim, Metal/GPU, Doppler/noise behavior, or overlap-aware separation was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR12B.md.
```

---

## PR13: PerChannelPulseDetectorNode And Merge Handoff

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR13 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: PerChannelPulseDetectorNode And Merge Handoff.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR11 channelized/per-channel packet contracts must already exist.
- PR12B ChannelizerNode must expose exactly 64 output ports, one per configured frequency.
- PR3/PR7A pulse merge/candidate contracts must already exist.

Scope:
- Add a real GraphX `PerChannelPulseDetectorNode` for one channelized IQ stream from one `ChannelizerNode` output port.
- Use PR11 packet contracts and `graph::gpu::accel::ControlToken<...>` port types.
- Consume exactly one `ControlToken<FHSSChannelizedIqPacket>` and emit detected pulse metadata in shared global sample time.
- Use the single frequency index and channel metadata supplied by `ChannelizerNode`; do not scan across frequencies.
- Preserve/dehop complex evidence for downstream CPSM branch metrics.
- Emit frequency index, RF metadata frequency, IQ offset frequency, estimated center frequency, CFO/frequency error placeholder or estimate, SNR/confidence, detector id, packet sequence, channel id, and global timing.
- Ensure output merges cleanly through `FHSSPulseMergeNode`.
- Add plugin/provider registration if exposed through the plugin path.
- Add focused GraphX node API, type-contract, registration, timing, metadata, confidence, and merge-handoff tests.

Do not decode words, preamble, or CPSM symbol sequences.
Do not add CPSM Viterbi/MLSE duplication, message assembly, graph JSON end-to-end executor wiring, Metal/GPU, Doppler/noise behavior, or overlap-aware separation.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR13.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR13 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: PerChannelPulseDetectorNode And Merge Handoff.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- `PerChannelPulseDetectorNode` exists as a real GraphX node.
- All node ports use `graph::gpu::accel::ControlToken<...>` carrying PR11 packet contracts.
- Detector consumes a single channel packet and emits detected pulse metadata in shared global sample time.
- Detector channel metadata identifies exactly one configured frequency index.
- Detector does not scan across frequencies.
- Detector preserves/dehops complex evidence for CPSM branch metrics.
- Detector reports frequency index, RF metadata frequency, IQ offset frequency, estimated center frequency, CFO/frequency error, SNR/confidence, detector id, packet sequence, channel id, and global timing.
- Detector output is compatible with `FHSSPulseMergeNode`.
- Plugin/provider registration tests exist if exposed through plugins.
- Detector does not decode words, preamble, or CPSM symbol sequences.
- No CPSM Viterbi/MLSE duplication, message assembly, graph JSON executor wiring, Metal/GPU, Doppler/noise behavior, or overlap-aware separation was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR13.md.
```

---

## PR14: Channelized FHSS Graph JSON And Executor Test

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR14 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Channelized FHSS Graph JSON And Executor Test.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisites:
- PR10 explicit message source schedule must already exist.
- PR12 FHSSDownconverterNode and ChannelizerNode must already exist.
- PR13 PerChannelPulseDetectorNode must already exist.
- PR7A-PR7D and PR8 downstream GraphX FHSS nodes/contracts must already exist.

Scope:
- Add an alternate channelized FHSS graph JSON using:
  `FHSSSyntheticIqSourceNode -> FHSSDownconverterNode -> ChannelizerNode -> PerChannelPulseDetectorNode[] -> FHSSPulseMergeNode -> FHSSPulseCandidateNode -> CPSMBranchMetricNode -> CPSMViterbiDecoderNode -> FHSSPulseWordDecoderNode -> FHSSPreambleDetectorNode -> FHSSMessageAssemblerNode -> FHSSMessageSinkNode`.
- Load all FHSS nodes through the plugin/provider path.
- Wire source IQ through the downconverter before channelization, even when configured as validated passthrough.
- Instantiate one `PerChannelPulseDetectorNode` per configured frequency.
- Prove detector node count equals configured frequency count.
- Run the channelized deterministic fixture to completion through GraphExecutorBuilder/repository-consistent GraphX executor methods.
- Verify decoded pulses match truth for start, duration, frequency index, and value.
- Verify assembled message locks on hop-only preamble and validates the four-frequency active transmit set.
- Emit diagnostics for channelizer sample-time mapping, channel ids, group delay/decimation, downconverter passthrough/translation state, synchronization assumption, unsupported overlap, and unsupported impairments.
- Keep PR8 correlator-bank fixture graph available as a reference unless explicitly removed by a later PR.
- Add focused graph config, plugin-loading, executor, truth-match, and diagnostics tests.

Do not invent new GraphX adaptors or accessors.
Do not add real RF capture, production channelizer separation claims, external datasets, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostics.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR14.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR14 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Channelized FHSS Graph JSON And Executor Test.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Channelized graph config uses `FHSSSyntheticIqSourceNode -> FHSSDownconverterNode -> ChannelizerNode -> PerChannelPulseDetectorNode[] -> FHSSPulseMergeNode`.
- Graph config wires source IQ through the downconverter before channelization, even if passthrough.
- Graph config loads all FHSS nodes through the plugin/provider path.
- Graph config uses real GraphX FHSS nodes and token-wrapped packet contracts only.
- One `PerChannelPulseDetectorNode` instance exists per configured frequency.
- Detector node count equals configured frequency count.
- Executor uses GraphExecutorBuilder/repository-consistent GraphX executor methods and runs the deterministic fixture to completion.
- Decoded pulses match truth for start, duration, frequency index, and value.
- Message locks on hop-only preamble and validates the four-frequency active transmit set.
- Diagnostics include channelizer sample-time mapping, channel ids, group delay/decimation, downconverter passthrough/translation state, synchronization assumption, unsupported-overlap, and unsupported-impairment status.
- PR8 correlator-bank graph remains available as reference unless roadmap explicitly removed it.
- Graph remains CPU-only and uses no Metal/GPU nodes.
- No invented GraphX adaptors/accessors, real RF capture, production channelizer claim, external dataset, Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostic path was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR14.md.
```

---

## PR15: Channelized Lane Promotion And Correlator-Bank Deprecation Plan

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR15 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Channelized Lane Promotion And Correlator-Bank Deprecation Plan.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Prerequisite:
- PR14 channelized graph JSON and executor test should already exist.

Scope:
- Decide and document whether the channelized graph is now the canonical FHSS fixture graph.
- Mark the PR8 correlator-bank detector graph as a compatibility/reference topology or remove it if channelized coverage fully replaces it.
- Consolidate docs, config naming, aliases, and guardrails around the selected canonical graph.
- Add guardrail tests identifying the canonical FHSS graph config.
- Add regression tests proving no doc/config labels the correlator-bank detector as production-like channelization.
- If retained, document and test the correlator-bank graph as a reference fixture path only.
- Keep at least one full deterministic FHSS executor lane covered in CI.

Do not change protocol behavior, add production RF claims, add Metal/GPU, Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostics.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR15.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR15 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: Channelized Lane Promotion And Correlator-Bank Deprecation Plan.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Roadmap/docs/config clearly identify the canonical FHSS graph shape.
- Correlator-bank detector graph is either removed after equivalent channelized coverage or clearly labeled compatibility/reference only.
- Guardrail test identifies the canonical FHSS graph config.
- Regression test proves no doc/config labels the correlator-bank detector as production-like channelization.
- At least one full deterministic FHSS executor lane remains covered in CI.
- No protocol behavior change, production RF claim, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or canonical PDW diagnostic path was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR15.md.
```

---

## PR16: RF Feasibility, Full Selectable-Frequency Strategy, And Impairment Plan

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR16 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: RF Feasibility, Full Selectable-Frequency Strategy, And Impairment Plan.

Use the implementer prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Scope:
- Resolve or explicitly document deferred RF feasibility items before expanding receiver coverage claims or impairments.
- Define occupied-bandwidth/channel-filter requirements for 5 Mbps CPSM on 8 MHz spacing, or explicitly keep them unresolved with no production channelizer claim.
- Choose the future strategy for selectable-frequency coverage without pretending all 64 RF centers fit alias-free in one 500 Msps complex-baseband capture.
- Strategy options may include higher sample rate, retuned sub-band windows, sparse active scheduling, or explicit alias/downconversion modeling.
- Preserve the invariant that receiver configuration has one logical GraphX channel output port per configured frequency.
- Define which impairment diagnostics and status values become canonical before implementing Doppler/noise/CFO/phase drift/multipath behavior.
- Keep optional PDW diagnostics non-canonical unless a later PR explicitly changes the decoder contract.
- Update roadmap/docs and add guardrail tests if new claims are introduced.
- Add optional offline spectral-analysis fixture tests only if a deterministic occupied-bandwidth estimator is added.

Do not implement Doppler/noise/CFO/multipath support, Metal/GPU, overlap-aware separation, external datasets, or production RF claims in this PR.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_FHSS_DECODER_IMPL_PR16.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR16 from plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md: RF Feasibility, Full Selectable-Frequency Strategy, And Impairment Plan.

Use the verifier prompt from: plan/agents/DSP_FHSS_DECODER_PR_AGENTS.md

Required checks:
- Plan/docs define occupied-bandwidth/channel-filter requirements or explicitly keep them unresolved with no production channelizer claim.
- Plan/docs choose or clearly defer the future strategy for selectable-frequency coverage without claiming all 64 RF centers fit alias-free in one 500 Msps complex-baseband capture.
- Plan/docs preserve the invariant that receiver configuration has one logical GraphX channel output port per configured frequency.
- Plan/docs define canonical impairment diagnostics/status values before implementation, or explicitly defer them.
- Optional PDW diagnostics remain non-canonical unless a later PR explicitly changes the decoder contract.
- Guardrail tests exist where new claims are introduced.
- No Doppler/noise/CFO/multipath support, Metal/GPU, overlap-aware separation, external dataset, or production RF claim was implemented.

Stop after verifier report.
Save the report to plan/reviews/DSP_FHSS_DECODER_VERIFY_PR16.md.
```
