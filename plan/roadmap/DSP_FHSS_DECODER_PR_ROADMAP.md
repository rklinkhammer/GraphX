# DSP FHSS Decoder PR Roadmap

Scope: planning only. Do not implement code from this document directly. Each PR must compile and test independently, keep the first lane CPU-only and deterministic, preserve complex IQ evidence for decoding, and avoid forcing the modem/receiver architecture through generic PDWs.

This roadmap targets a deterministic GraphX FHSS DSP fixture where one pulse carries one full `uint32_t`. The protocol uses 128 RF metadata frequencies starting at 1 GHz with 4 MHz spacing, a 16-pulse hop-only preamble, four active frequencies derived from the preamble pattern, and messages of at most 256 pulses including preamble. Fixture IQ must use baseband or offset frequencies because absolute 1 GHz RF cannot be directly sampled at `500 Msps` without aliasing.

## Decided Baseline

- Sample rate: `500 Msps`.
- Bit rate: `5 Mbps`.
- Symbol timing: `100 samples/symbol`, `32 symbols/pulse`, `3200` pulse samples, `3300` gap samples, `6500` samples per pulse period.
- Modulation: binary CPSM using `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`.
- Initial CPSM assumptions: `h=1/2`, rectangular full-response phase pulse, initial phase `0`, continuity inside each pulse only, terminal phase unconstrained unless checked by the decoder PR.
- Preamble detection: hop-only. Preamble word values are fixture consistency / secondary validation only.
- Payload/body frequency rule: randomly select from the four active preamble frequencies using a deterministic seed.
- PR1 synchronization: `message_start_sample = 0`; pulse-start acquisition is future work.
- PR1 overlap policy: reject and report overlapped messages as unsupported.
- Diagnostics: final schema may be deferred, but PR1+ must expose at least `pulse_count`, `rejected_count`, `global_start_sample`, `frequency_index`, `confidence`, `viterbi_path_metric`, `decoded_value`, `preamble_lock`, and `truth_mismatch_count` when relevant.

## Remaining Planning Decisions

- Frequency representation: decide whether `frequency_index` is canonical with derived frequencies, or whether configs store both and validate consistency. PR1 should prefer canonical index plus `FHSSFrequencyMapEntry` validation.
- Future synchronization: decide when to add pulse-start acquisition beyond `message_start_sample = 0`.
- Doppler/noise policy: decide which config and diagnostic fields belong in PR1 even though impairments are disabled by default.
- Error model: define status values for unknown frequency, low confidence, bad word decode, missing preamble, invalid timing config, unsupported overlap, unsupported Doppler/noise, and overlength message.
- CPSM estimator details: pin down exact rectangular `q(t)` normalization/support, matched filter, Viterbi/MLSE state model, terminal phase policy, and confidence metric.
- Bandwidth/channelizer feasibility: validate 5 Mbps CPSM spectral occupancy against 4 MHz channel spacing and choose channelizer filter width before claiming channelizer separation.
- Detector/decoder metric handoff: decide whether detectors emit only timing/frequency candidates or pass reusable likelihood state downstream.

## Target Graph

Longer-term channelized shape:

```text
FHSSSyntheticIqSourceNode
  -> ChannelizerNode
      -> PerChannelPulseDetectorNode[]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

PR1-friendly CPU shape without a real channelizer:

```text
FHSSSyntheticIqSourceNode
  -> CorrelatorBankDetectorNode
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

## PR1: FHSS Protocol Types, Frequency Map, And Fixture Schema

Purpose:

- Remove protocol ambiguity before implementing signal generation or decoding.
- Add the canonical type/config model for FHSS truth pulses, detected pulses, pulse candidates, messages, frequency-map entries, and decode configuration.
- Lock fixture validation for the 128-frequency table, four active preamble frequencies, 16-pulse preamble, 256-pulse message limit, `500 Msps` timing, and baseband/offset IQ frequency mapping.

Files to touch:

- `libdsp/include/dsp/` for FHSS protocol/type headers, or a new narrow `libdsp/include/dsp/fhss/` area if that matches implementation conventions.
- `libdsp/test` or `libgraph/test/unit` for protocol validation tests, depending on current ownership.
- `libdsp/CMakeLists.txt` and relevant test CMake wiring.
- `docs/dsp/` only if the implementation PR includes a small protocol note.

Files to delete:

- None.

Tests to add:

- Frequency table derives exactly 128 RF metadata frequencies from 1 GHz at 4 MHz spacing.
- `FHSSFrequencyMapEntry` rejects sampled absolute 1 GHz RF at `500 Msps` unless downconversion/aliasing is explicitly modeled.
- Active preamble set validation requires exactly four distinct frequency indices.
- Preamble pattern validation requires exactly 16 pulses.
- Message length validation rejects more than 256 pulses including preamble.
- Timing validation proves `500 Msps / 5 Mbps = 100`, `N_pulse = 3200`, `N_gap = 3300`, and `N_period = 6500`.
- Deterministic RNG seed produces stable payload/body frequency selections from the four active frequencies.
- Identical preamble frequencies require identical preamble word values in generated truth fixtures.

Tests to delete:

- None.

Acceptance criteria:

- The protocol/type model compiles independently.
- Invalid protocol configs fail loudly with diagnosable errors.
- The RF metadata table and IQ offset map are distinct and validated.
- No node runtime, graph execution, Metal/GPU work, or real RF capture support is added.

Risks:

- Placing FHSS types in the wrong library boundary could leak example-specific modem assumptions into generic graph code.
- `std::optional` in public structs may require consistent include/API treatment.

Rollback plan:

- Remove the FHSS type headers, fixture schema helpers, tests, and CMake wiring.

CI-safe or local-only:

- CI-safe.

## PR2: Deterministic FHSS CPSM Synthetic IQ Generator

Purpose:

- Add a CPU deterministic synthetic IQ generator that emits complex IQ and exact truth metadata for the FHSS fixture.
- Encode each `uint32_t` into one 32-symbol binary CPSM pulse using the selected `500 Msps` timing.
- Preserve truth metadata solely for validation.

Files to touch:

- New FHSS generator headers/sources under `libdsp/include/dsp` and `libdsp/src/dsp`, or equivalent FHSS subdirectory.
- `libdsp/test` or `libgraph/test/unit` for generator tests.
- `libdsp/CMakeLists.txt`.
- Optional small fixture config under `libdsp/config/`.

Files to delete:

- None.

Tests to add:

- Generator emits the expected number of samples for a known preamble and payload.
- Every truth pulse has exact `global_start_sample`, duration `3200`, frequency index, RF metadata frequency, IQ offset frequency, value, and preamble flag.
- Payload/body frequencies are deterministic random selections from the four active preamble frequencies.
- Generated preamble truth satisfies identical-frequency/identical-word consistency.
- CPSM output has constant envelope inside pulse for the noise-free fixture.
- Phase continuity holds according to the selected rectangular full-response `q(t)`.
- Gap samples are zero or documented idle samples.
- Overlap fixture is rejected for PR1.

Tests to delete:

- None.

Acceptance criteria:

- Generator is deterministic and CI-safe.
- No decoder or graph runtime integration is required yet.
- Generated IQ carries the pulse value in complex phase evidence, not only metadata.
- Noise, Doppler, and multipath are disabled by default but config/diagnostic extension points are identified.

Risks:

- The exact rectangular `q(t)` normalization must be implemented consistently with the later decoder.
- Large sample counts can make fixtures heavy; keep PR2 fixtures tiny.

Rollback plan:

- Remove the generator source/header, configs, tests, and CMake wiring.

CI-safe or local-only:

- CI-safe.

## PR3: Detected-Pulse Metadata And Pulse Merge/Association

Purpose:

- Add the detection metadata and association boundary before decoding.
- Normalize detections from per-channel/per-frequency detectors into one global sample-time domain.
- Add `FHSSPulseMergeNode` semantics for sorting, duplicate rejection, collision detection, unsupported-overlap reporting, and ordered candidate emission.

Files to touch:

- FHSS detected-pulse and candidate headers.
- `FHSSPulseMergeNode` header/source.
- Test files for global timing, sorting, duplicate rejection, collision/overlap policy, and candidate emission.
- Plugin/CMake files only if this PR exposes the merge node through plugins.

Files to delete:

- None.

Tests to add:

- `global_start_sample = input_packet_global_start_sample + local_start_offset`.
- Detected pulses from multiple channels sort by `global_start_sample`.
- Duplicate detections on the same frequency and overlapping window retain the higher-confidence/SNR pulse.
- Cross-frequency collisions are reported according to the PR1 unsupported-overlap policy.
- Provisional slot index uses `global_start_sample / N_period`.
- Final slot index uses `(global_start_sample - message_epoch_sample) / N_period` when epoch is known.
- Missing or inconsistent global timing metadata is rejected.

Tests to delete:

- None.

Acceptance criteria:

- `FHSSPulseMergeNode` emits an ordered `FHSSPulseCandidate` stream with complex sample references/payload preserved.
- Channel-local timing alone is never accepted for association.
- PR1 overlap behavior is deterministic and diagnosable.
- No CPSM word decoding is added in this PR.

Risks:

- If current GraphX node contracts make variable fan-in or stream merging awkward, the PR may need a DSP contract update proposal rather than adapters.
- Candidate sample ownership/lifetime must be defined cleanly.

Rollback plan:

- Remove merge node, detected-pulse metadata additions, tests, and plugin/CMake wiring.

CI-safe or local-only:

- CI-safe.

## PR4: Correlator-Bank Detector And Candidate Extraction

Purpose:

- Add the PR1 detector path without a real channelizer.
- Start from the four active frequencies derived from the preamble pattern.
- Emit timing/frequency candidates and detected-pulse metadata, but leave full CPSM sequence evidence to branch metric and Viterbi nodes.

Files to touch:

- `CorrelatorBankDetectorNode` or equivalent detector headers/sources.
- Detector plugin/CMake wiring if graph-loaded in this PR.
- Tests for frequency ranking, timing, global metadata, confidence, and handoff to merge.

Files to delete:

- None.

Tests to add:

- Known-slot detector uses `message_start_sample = 0` and `N_period = 6500`.
- Detector ranks the correct active frequency among the four active offsets.
- Detector does not scan all 128 frequencies in PR1.
- Detector emits `FHSSDetectedPulse` with RF metadata frequency, IQ offset frequency, frequency error, CFO placeholder/estimate, SNR/confidence, detector id, packet sequence, and global timing.
- Detector does not require preamble decoding.
- Detector rejects unsupported overlapped messages.

Tests to delete:

- None.

Acceptance criteria:

- Detector output is sufficient for `FHSSPulseMergeNode` and later CPSM decoding.
- Detector does not rely on magnitude-only `MagnitudePacket` for word recovery.
- Detector does not duplicate full Viterbi/MLSE work unless a reusable metric handoff contract is explicitly implemented.

Risks:

- Constant-envelope CPSM means raw mixed-down energy cannot choose hop frequency; detector must use coherent phase/channelizer/correlation evidence.
- The metric chosen here may need adjustment after PR5 branch metrics are implemented.

Rollback plan:

- Remove detector node, tests, plugin/CMake wiring, and any config snippets.

CI-safe or local-only:

- CI-safe.

## PR5: CPSM Branch Metric And Viterbi/MLSE Decoder

Purpose:

- Implement efficient CPSM sequence estimation for one pulse without brute force over `2^32` symbol sequences.
- Add `CPSMBranchMetricNode` and `CPSMViterbiDecoderNode` around the decided binary CPM model.
- Pin down estimator details needed by later pulse-word decoding.

Files to touch:

- New CPSM branch metric and Viterbi/MLSE headers/sources.
- Unit tests for branch metrics, trellis transitions, path metrics, terminal phase policy, and confidence.
- CMake/plugin wiring if graph-loaded in this PR.
- Documentation note if useful for estimator assumptions.

Files to delete:

- None.

Tests to add:

- Rectangular full-response `q(t)` makes `theta(t)` continuous.
- Trellis state is accumulated CPM phase modulo `2*pi`.
- Known generated pulse decodes to the expected 32-symbol sequence.
- Viterbi/MLSE result matches a brute-force oracle on very small reduced fixtures, not full 32-symbol pulses.
- Terminal phase unconstrained or checked according to the implemented policy.
- Path metric separation produces a stable confidence metric.
- Magnitude-only inputs are rejected or impossible by type.

Tests to delete:

- None.

Acceptance criteria:

- No `2^32` brute-force pulse decode path exists.
- Estimator assumptions are explicit: `h=1/2`, rectangular full-response, initial phase `0`, continuity inside each pulse only.
- The branch metric and Viterbi nodes compile and test independently of message assembly.

Risks:

- Deriving a minimal-state estimator for rectangular CPM is the hardest technical item in the roadmap.
- Confidence metric may need refinement once noise/Doppler are introduced.

Rollback plan:

- Remove CPSM branch metric/Viterbi nodes, tests, and CMake/plugin wiring.

CI-safe or local-only:

- CI-safe.

## PR6: FHSS Pulse Word Decoder

Purpose:

- Convert decoded CPSM symbol decisions into one `uint32_t` per pulse.
- Preserve pulse timing/frequency metadata and attach decoded value/confidence.

Files to touch:

- `FHSSPulseWordDecoderNode` headers/sources.
- Unit tests for MSB-first mapping, metadata preservation, confidence propagation, and bad decode reporting.
- CMake/plugin wiring if graph-loaded in this PR.

Files to delete:

- None.

Tests to add:

- `+1 -> 0`, `-1 -> 1` mapping.
- MSB-first `value = (value << 1) | bit` for all 32 symbols.
- Known symbol vectors recover expected `uint32_t` values.
- Pulse metadata survives from candidate through decoded pulse.
- Low-confidence or invalid Viterbi output produces a diagnosable error/status.

Tests to delete:

- None.

Acceptance criteria:

- One decoded pulse yields exactly one `uint32_t`.
- No byte/nibble/symbol-fragment message model is introduced.
- Decoder consumes complex-derived CPSM decisions, not truth metadata.

Risks:

- Bit ordering must remain aligned with generator truth fixtures.

Rollback plan:

- Remove pulse-word decoder node, tests, and wiring.

CI-safe or local-only:

- CI-safe.

## PR7: Hop-Only Preamble Detector And Message Assembler

Purpose:

- Add message-layer behavior: hop-only preamble lock, active-set validation, payload assembly, max length enforcement, and truth comparison hooks.
- Keep preamble word values as fixture consistency / secondary validation only.

Files to touch:

- `FHSSPreambleDetectorNode`, `FHSSMessageAssemblerNode`, and `FHSSMessageSinkNode` headers/sources.
- Unit tests for preamble lock, active set, payload validation, length checks, and truth comparison.
- Plugin/CMake wiring if graph-loaded in this PR.

Files to delete:

- None.

Tests to add:

- Hop-only preamble lock over 16 pulses.
- Word mismatches do not prevent preamble lock, but identical preamble frequencies must have identical fixture word values.
- Active set after lock has exactly four frequencies.
- Payload frequencies outside active set are rejected.
- Total message length over 256 pulses including preamble is rejected.
- Missing preamble is rejected.
- Truth comparator reports mismatched start, duration, frequency, and value.
- Minimum diagnostics include `pulse_count`, `rejected_count`, `preamble_lock`, and `truth_mismatch_count`.

Tests to delete:

- None.

Acceptance criteria:

- Message assembly operates on globally ordered decoded pulses.
- Preamble detection is hop-only by design.
- The sink/report gives enough information for CI failures to be actionable.

Risks:

- Overlapped messages are unsupported in PR1; tests must assert deterministic rejection rather than accidental assembly.

Rollback plan:

- Remove message-layer nodes, tests, configs, and wiring.

CI-safe or local-only:

- CI-safe.

## PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

Purpose:

- Wire the PR1-friendly CPU graph through existing GraphX JSON config, plugin loading, graph builder, executor, and completion conventions.
- Exercise synthetic IQ through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, preamble lock, message assembly, and truth comparison.
- Emit minimum deterministic diagnostics.

Files to touch:

- `libdsp/config/fhss_cpsm_fixture_500msps.json` or a repository-consistent config path.
- FHSS plugin CMake files and plugin source files for the new nodes.
- `libgraph/test/unit` or `examples/DSP/test` for end-to-end graph/runtime tests.
- Optional `examples/DSP/src/main.cpp` extension or new FHSS demo runner only if repository conventions favor it.

Files to delete:

- None.

Tests to add:

- Graph config loads the FHSS nodes through the existing plugin/provider path.
- Executor runs the full deterministic fixture to completion.
- Decoded pulses match truth for start, duration, frequency index, and value.
- Assembled message locks on hop-only preamble and validates active set.
- Diagnostics contain required minimum fields.
- Graph is CPU-only and does not use Metal/GPU nodes.

Tests to delete:

- None.

Acceptance criteria:

- Full deterministic FHSS lane passes in CI.
- The graph preserves complex IQ through word recovery.
- No real RF capture, external dataset, GPU/Metal, or production RF claim is introduced.

Risks:

- Runtime completion can be flaky if source/consumer lifecycle is not tightly bounded.
- Plugin type names and template-specific node registration need careful matching.

Rollback plan:

- Remove FHSS config, plugins, end-to-end tests, and optional runner changes.

CI-safe or local-only:

- CI-safe.

## PR9: Documentation And Truth-In-Labeling Guardrails

Purpose:

- Document the FHSS fixture lane, protocol limits, CPSM assumptions, baseband/offset frequency mapping, unsupported overlap policy, and future boundaries.
- Add guardrails against mislabeling the fixture as production RF, external waveform compatibility, GPU/Metal acceleration, or magnitude-only decoding.

Files to touch:

- `docs/dsp/fhss_decoder.md` or equivalent.
- `README.md` only if DSP examples are indexed there.
- Documentation/guardrail tests where existing conventions support them.

Files to delete:

- None.

Tests to add:

- Guardrail test that FHSS docs mention baseband/offset frequencies and do not claim direct 1 GHz RF sampling at `500 Msps`.
- Guardrail test that docs/config identify CPU-only PR1 behavior.
- Guardrail test that magnitude-only DFT is not the canonical decoder input.
- Guardrail test that overlap is unsupported in PR1.

Tests to delete:

- None.

Acceptance criteria:

- Users can understand how to build/run the deterministic lane.
- Docs explain what is fixture-only versus future production-like work.
- Docs capture future boundaries: channelizer implementation, Doppler/noise, overlap support, Metal acceleration, optional PDW diagnostics.

Risks:

- Documentation may drift if PR8 config/runner names change.

Rollback plan:

- Remove FHSS docs and guardrail tests.

CI-safe or local-only:

- CI-safe.

## Future Boundary: Channelizer, Doppler/Noise, Metal, And PDW Diagnostics

Purpose:

- Capture future work without pulling it into the first deterministic FHSS lane.

Files to touch:

- None in this roadmap.

Files to delete:

- None.

Tests to add:

- None in this roadmap.

Tests to delete:

- None.

Acceptance criteria:

- Future work remains explicitly out of scope for PR1 through PR9.
- Candidate future work is limited to:
  - real channelizer topology and filter-width validation;
  - Doppler/noise/phase-offset impairments and estimator robustness;
  - pulse-start acquisition beyond `message_start_sample = 0`;
  - overlap-aware message separation;
  - Metal acceleration after CPU correctness is locked;
  - optional PDW diagnostics that do not become the canonical decoder input.

Risks:

- Future performance or RF realism pressure could blur the deterministic fixture boundary.

Rollback plan:

- None; this is a planning boundary.

CI-safe or local-only:

- Not applicable.
