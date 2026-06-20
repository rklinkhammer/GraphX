# DSP FHSS Decoder PR Roadmap

Scope: planning only. Do not implement code from this document directly. Each PR must compile and test independently, keep the first lane CPU-only and deterministic, preserve complex IQ evidence for decoding, and avoid forcing the modem/receiver architecture through generic PDWs.

This roadmap targets a deterministic GraphX FHSS DSP fixture where one pulse carries one full `uint32_t`. The protocol uses 64 RF metadata frequencies starting at 1 GHz with 8 MHz spacing, a 16-pulse hop-only preamble, four active frequencies derived from the preamble pattern, and messages of at most 256 pulses including preamble. Selectable frequency indices are `[1, 62]`; indices `0` and `63` are reserved edge/guard entries and must never be selected. Fixture IQ must use baseband or offset frequencies because absolute 1 GHz RF cannot be directly sampled at `500 Msps` without aliasing.


## RF Correctness Summary

The current graph architecture is retained. The main RF/signal-processing corrections are constraints on how that graph is configured and validated:

- treat the 1 GHz hop table as RF metadata, not directly sampled fixture IQ;
- operate the deterministic fixture on complex baseband/IF offsets;
- validate offset frequencies against Nyquist, occupied bandwidth, and CFO guard bands;
- do not claim that all 64 RF centers fit alias-free into one 500 Msps complex-baseband fixture while preserving the 8 MHz spacing;
- do not claim production channelizer separation until 5 Mbps CPSM occupied bandwidth and channel filter requirements are defined;
- keep detector timing/frequency evidence separate from CPSM branch metric/Viterbi sequence estimation;
- preserve global sample-time mapping across any channelizer, detector, or merge boundary.

## Decided Baseline

- Sample rate: `500 Msps`.
- Bit rate: `5 Mbps`.
- Symbol timing: `100 samples/symbol`, `32 symbols/pulse`, `3200` pulse samples, `3300` gap samples, `6500` samples per pulse period.
- Frequency table: 64 RF metadata entries; selectable active indices are `[1, 62]`; reserved edge indices `0` and `63` are invalid for preamble and payload selection.
- Modulation: binary CPSM using `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`.
- Initial CPSM assumptions: `h=1/2`, rectangular full-response phase pulse, initial phase `0`, continuity inside each pulse only, terminal phase unconstrained unless checked by the decoder PR.
- RF realism caveat: 5 Mbps CPSM on 8 MHz-spaced channels requires occupied-bandwidth/channel-filter validation before any production-like channelizer claim.
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
- Bandwidth/channelizer feasibility: validate 5 Mbps CPSM spectral occupancy against 8 MHz channel spacing and choose channelizer filter width before claiming channelizer separation.
- Detector/decoder metric handoff: decide whether detectors emit only timing/frequency candidates or pass reusable likelihood state downstream.


## RF And Signal-Processing Correctness Addendum

The graph architecture is intentionally unchanged. The following items are additional correctness constraints and planning decisions that must be carried through the existing graph and PR sequence.

### RF Metadata Versus Simulated IQ Frequencies

Absolute RF hop frequencies are protocol metadata. The fixture IQ path must operate on complex baseband or IF-offset frequencies. A sampled complex IQ stream at `500 Msps` has a nominal Nyquist interval of `[-250 MHz, +250 MHz)`, so directly synthesizing `1 GHz + 8 MHz * index` into the fixture IQ aliases unless an explicit downconversion model is part of the simulation.

Required model:

```cpp
struct FHSSFrequencyMapEntry {
    uint32_t index;
    double rf_frequency_hz;        // 1 GHz + index * 8 MHz
    double iq_offset_frequency_hz; // CI-safe complex baseband/IF offset
};
```

Validation rules:

```text
rf_frequency_hz = base_frequency_hz + index * frequency_spacing_hz
abs(iq_offset_frequency_hz) + occupied_bandwidth_hz / 2 + max_abs_cfo_hz < sample_rate_hz / 2
all active iq_offset_frequency_hz values are distinct modulo sample_rate_hz
```

Important consequence:

```text
64 frequencies at 8 MHz spacing span 504 MHz between lowest and highest center frequencies.
That full RF table cannot be represented as one alias-free 500 Msps complex-baseband fixture while preserving all absolute spacings at once.
```

Indices `0` and `63` are reserved edge/guard metadata entries. The valid selectable set for active preamble and payload/body hopping is `[1, 62]`.

Therefore PR1 may operate only on the four active preamble-derived frequencies. A future full-table detector over the selectable interior frequency set must use one of these approaches:

* a higher fixture sample rate;
* a downconverted sub-band selection model;
* a bank of retuned captures/windows;
* a sparse active-frequency scheduler;
* or an explicit aliasing model, if that is really intended.

Do not silently map all 64 RF frequencies into one 500 Msps baseband span while pretending it is an alias-free RF capture.

### 8 MHz Hop Spacing Versus 5 Mbps CPSM Occupancy

The roadmap uses 5 Mbps CPSM and 8 MHz hop spacing. That is a potential adjacent-channel issue. The graph may remain unchanged, but the plan must not claim clean channelizer separation until the occupied bandwidth and filter design are validated.

Required planning decision:

```text
Define occupied_bandwidth_hz for the selected CPSM phase pulse and envelope, or mark it as a fixture-only unresolved RF-realism item.
```

For PR1, the detector may use coherent known-frequency correlation over four configured offsets in a noise-free fixture. That can pass deterministic tests even when a production channelizer would require more careful pulse shaping and adjacent-channel rejection.

Future channelizer planning must specify:

* channel spacing;
* channel filter passband width;
* transition bandwidth;
* stopband attenuation;
* group delay;
* timing alignment from channelized samples back to global input sample time;
* adjacent-channel rejection requirements;
* whether active channels are 8 MHz-spaced, more widely spaced in the fixture, or intentionally stress adjacent-channel behavior.

### CPM/CPSM Trellis And Estimator Boundary

The roadmap requires CPSM demodulation by branch metrics plus Viterbi/MLSE, but the PR must pin down the trellis before implementation.

For the initial recommended binary full-response rectangular CPM fixture:

```text
h = 1/2
L = 1 full-response phase pulse
initial_phase = 0
phase state = accumulated CPM phase modulo 2*pi
continuity inside pulse only
terminal phase = unconstrained unless explicitly checked
```

The planner must derive the finite-state trellis for this exact model and include a small reduced-length brute-force oracle test to verify the Viterbi implementation. The implementation must not enumerate `2^32` full pulse words.

### Detector Versus Decoder Evidence Ownership

The detector may estimate timing and frequency, but CPSM sequence recovery belongs to the branch metric/Viterbi decoder unless a reusable likelihood handoff is explicitly designed.

Required decision:

```text
Detector output = timing/frequency candidate + dehopped complex evidence.
CPSMBranchMetricNode owns sequence likelihood calculation.
CPSMViterbiDecoderNode owns symbol sequence decision.
```

If the detector computes a coherent metric to rank frequencies, that metric may be reported as detection confidence, but it must not become an undocumented partial decoder.

### Global Sample-Time And Rate Changes

The graph combines detections from multiple channels. All channelizer or correlator outputs must preserve a mapping back to the original input sample index.

Required metadata for any channelized or decimated future path:

```cpp
struct SampleTimeMap {
    uint64_t input_global_start_sample;
    uint64_t output_start_sample;
    uint32_t decimation_factor;
    int64_t group_delay_input_samples;
    double sample_rate_hz;
};
```

For PR1 without decimation, `decimation_factor = 1` and `group_delay_input_samples = 0`. Future channelizers must compensate group delay before `FHSSPulseMergeNode` sorts or assigns slot indices.

### Synchronization Boundary

PR1 assumes:

```text
message_start_sample = 0
pulse starts are aligned to N_period = 6500 samples
```

That is acceptable for the deterministic fixture, but it must be visible in diagnostics and config. Pulse-start acquisition, fractional timing error, and message-start search are future work.

### Frequency Error, Doppler, And Phase Drift

Noise and Doppler are disabled in PR1, but field names and diagnostics should leave room for:

* carrier frequency offset in Hz;
* phase slope in rad/sample;
* residual CFO after dehopping;
* symbol timing error;
* confidence degradation under noise;
* rejection reason for unsupported impairments.

Do not add noisy/Doppler tests to PR1 unless they are deterministic and disabled by default in the main CI fixture.

## Target Graph

All boxes in the target graph are **GraphX nodes**, not helper classes with
node-like names. GraphX node means a repository-consistent runtime node with
declared GraphX input/output types, plugin/provider registration when graph
loaded, JSON/topology compatibility when applicable, and tests that exercise
the GraphX node contract rather than directly calling private algorithm
helpers.

The first FHSS lane remains CPU-only through this roadmap, but its edge data
types must be designed so a future Metal/GPU lane can use GraphX accelerator
contracts without changing the FHSS semantic model. Any edge that may later
cross a host/device boundary must have an explicit packet/sidecar boundary,
complex-evidence ownership model, and future `AccelControlToken` compatibility
story. Do not encode future GPU identity in raw pointers, local offsets, or
ad-hoc helper structs.

The PR1-PR7 helper implementations are now considered **pre-GraphX
scaffolding**. They may be used as temporary algorithm references while
creating real GraphX nodes, but they must not remain as the canonical FHSS
node implementation. After PR7 and before the end-to-end graph PR, the plan
must replace every previous pseudo-node with a GraphX node and rewrite tests to
use GraphX node contracts. The old pre-GraphX pseudo-node classes and direct
helper-node tests must be removed; backward compatibility is not required.

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
- Lock fixture validation for the 64-frequency table, four active preamble frequencies selected only from indices `[1, 62]`, 16-pulse preamble, 256-pulse message limit, `500 Msps` timing, and baseband/offset IQ frequency mapping.

Files to touch:

- `libdsp/include/dsp/` for FHSS protocol/type headers, or a new narrow `libdsp/include/dsp/fhss/` area if that matches implementation conventions.
- `libdsp/test` or `libgraph/test/unit` for protocol validation tests, depending on current ownership.
- `libdsp/CMakeLists.txt` and relevant test CMake wiring.
- `docs/dsp/` only if the implementation PR includes a small protocol note.

Files to delete:

- None.

Tests to add:

- Frequency table derives exactly 64 RF metadata frequencies from 1 GHz at 8 MHz spacing.
- `FHSSFrequencyMapEntry` rejects sampled absolute 1 GHz RF at `500 Msps` unless downconversion/aliasing is explicitly modeled.
- Active preamble set validation requires exactly four distinct frequency indices.
- Active preamble set validation rejects reserved edge indices `0` and `63`.
- Frequency index validation rejects values outside `[0, 63]`.
- Preamble pattern validation requires exactly 16 pulses.
- Message length validation rejects more than 256 pulses including preamble.
- Timing validation proves `500 Msps / 5 Mbps = 100`, `N_pulse = 3200`, `N_gap = 3300`, and `N_period = 6500`.
- Deterministic RNG seed produces stable payload/body frequency selections from the four active frequencies, all of which must be selectable indices in `[1, 62]`.
- Identical preamble frequencies require identical preamble word values in generated truth fixtures.
- IQ offset validation rejects any active offset whose occupied-bandwidth guard would exceed the `500 Msps` Nyquist interval.
- Full-table validation documents that all 64 RF centers cannot be represented alias-free in one `500 Msps` complex-baseband span while preserving 8 MHz spacing.

Tests to delete:

- None.

Acceptance criteria:

- The protocol/type model compiles independently.
- Invalid protocol configs fail loudly with diagnosable errors.
- The RF metadata table and IQ offset map are distinct and validated.
- The first fixture uses only the four active offsets for IQ generation/detection; those active offsets must correspond to selectable indices `[1, 62]`, and all 64 RF frequencies remain metadata unless a future all-band model is explicitly added.
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
- Generator never selects reserved edge indices `0` or `63` for preamble or payload/body pulses.
- Generated preamble truth satisfies identical-frequency/identical-word consistency.
- CPSM output has constant envelope inside pulse for the noise-free fixture.
- Phase continuity holds according to the selected rectangular full-response `q(t)`.
- Gap samples are zero or documented idle samples.
- Generated IQ uses `iq_offset_frequency_hz`, not `rf_frequency_hz`, in the complex exponential.
- Phase, frequency, and sample-time metadata remain consistent with the RF frequency index.
- Overlap fixture is rejected for PR1.

Tests to delete:

- None.

Acceptance criteria:

- Generator is deterministic and CI-safe.
- No decoder or graph runtime integration is required yet.
- Generated IQ carries the pulse value in complex phase evidence, not only metadata.
- The generator documents fixture-only spectral realism and does not claim 8 MHz channel separability without occupied-bandwidth validation.
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
- Any future decimated/channelized input must carry sample-time mapping fields sufficient to recover original input global sample indices.

Tests to delete:

- None.

Acceptance criteria:

- `FHSSPulseMergeNode` emits an ordered `FHSSPulseCandidate` stream with complex sample references/payload preserved.
- Channel-local timing alone is never accepted for association.
- Sample-time mapping is explicit even when PR1 uses `decimation_factor = 1` and zero group delay.
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
- Detector uses offset frequencies and never directly mixes against absolute 1 GHz RF metadata in the `500 Msps` fixture.
- Detector does not scan the full 64-entry frequency table in PR1.
- Detector active frequency configuration rejects reserved edge indices `0` and `63`.
- Detector emits `FHSSDetectedPulse` with RF metadata frequency, IQ offset frequency, frequency error, CFO placeholder/estimate, SNR/confidence, detector id, packet sequence, and global timing.
- Detector does not require preamble decoding.
- Detector rejects unsupported overlapped messages.

Tests to delete:

- None.

Acceptance criteria:

- Detector output is sufficient for `FHSSPulseMergeNode` and later CPSM decoding.
- Detector does not rely on magnitude-only `MagnitudePacket` for word recovery.
- Detector does not duplicate full Viterbi/MLSE work unless a reusable metric handoff contract is explicitly implemented.
- Detector emits timing/frequency candidate evidence plus dehopped complex samples; CPSM branch metrics remain owned by PR5 unless the roadmap explicitly revises that boundary.

Risks:

- Constant-envelope CPSM means raw mixed-down energy cannot choose hop frequency; detector must use coherent phase/channelizer/correlation evidence.
- 8 MHz channel spacing with 5 Mbps CPSM may make simple channelizer separation unrealistic without filter/occupied-bandwidth analysis; PR4 must label this as fixture detection, not RF performance.
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
- Trellis state count and transition table are derived from `h=1/2`, binary symbols, and the selected full-response rectangular phase pulse.
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
- Branch metric equations include any matched-filter, integrate-and-dump, whitening, or normalization assumptions used by the MLSE.
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

- `FHSSPreambleDetectorNode`, `FHSSMessageAssemblerNode`, and `FHSSMessageSinkNode` headers/sources as temporary pre-GraphX scaffolding only; PR7A-PR7C must replace them with real GraphX nodes before PR8.
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

## PR7A: FHSS GraphX Edge Contracts And Accel-Ready Data Model

Purpose:

- Define the canonical GraphX data contracts for the FHSS lane before runtime nodes are wired.
- Replace ambiguous helper/pseudo-node payloads with GraphX edge packet types that can be used by CPU nodes now and mapped to accelerator-token sidecars later.
- Make ownership and lifetime of complex IQ evidence explicit across GraphX edges.

Files to touch:

- `libdsp/include/dsp/fhss/` for FHSS GraphX edge packet and sidecar types.
- `libgraph/test/unit` or `libdsp/test` for compile-time and runtime contract tests.
- CMake wiring only as required by the new tests.

Files to delete:

- None in this PR unless a pre-GraphX type is already fully replaced by the new edge contract.

Tests to add:

- GraphX packet types exist for synthetic IQ output, detected pulse/candidate evidence, CPSM metrics/symbol decisions, decoded pulse words, assembled messages, and diagnostics.
- Complex IQ evidence uses explicit ownership/reference semantics suitable for GraphX edges, not local-only helper assumptions.
- Global sample time, RF metadata frequency, IQ offset frequency, and future sample-time mapping survive through the packet model.
- Future accelerator compatibility is documented in type names/comments/tests: CPU payloads and future accel-token sidecars carry the same FHSS semantic metadata.
- No GraphX edge contract depends on truth metadata for decoder decisions.

Tests to delete:

- None.

Acceptance criteria:

- The canonical FHSS edge data model is GraphX-owned and does not depend on pseudo-node helper structs as the public contract.
- Each target graph edge has a named GraphX packet/contract type.
- The model is CPU-only today but does not block a future `AccelControlToken`/sidecar implementation.
- No runtime nodes are required yet.

Risks:

- Choosing packet boundaries too narrowly could force churn when PR7B converts helpers into GraphX nodes.
- Over-generalizing for future GPU work could make the deterministic CPU lane harder to review.

Rollback plan:

- Remove the new GraphX FHSS edge packet headers and tests.

CI-safe or local-only:

- CI-safe.

## PR7B: Replace FHSS Pseudo-Nodes With Real GraphX Nodes

Purpose:

- Replace every PR1-PR7 pseudo-node/helper class that used a `Node` suffix with repository-consistent GraphX runtime nodes.
- Preserve the validated FHSS algorithms while moving the public execution surface to GraphX node contracts.
- Require every FHSS GraphX node input/output edge to use `graph::gpu::accel::ControlToken<...>` typed payloads, following the existing `DspIqH2DNode` and `CpuSpectrumDftNode` edge model.
- Keep the lane CPU-only, deterministic, and CI-safe.

Files to touch:

- FHSS GraphX node headers/sources for:
  - `FHSSSyntheticIqSourceNode`
  - `CorrelatorBankDetectorNode`
  - `FHSSPulseMergeNode`
  - `FHSSPulseCandidateNode` if still needed as a distinct graph boundary
  - `CPSMBranchMetricNode`
  - `CPSMViterbiDecoderNode`
  - `FHSSPulseWordDecoderNode`
  - `FHSSPreambleDetectorNode`
  - `FHSSMessageAssemblerNode`
  - `FHSSMessageSinkNode`
- Existing FHSS helper headers may be renamed into non-node algorithm kernels only if they are private/internal and no longer expose old pseudo-node names.
- Plugin/provider CMake files if the nodes are graph-loadable in this PR.
- Unit tests rewritten to instantiate and execute the GraphX nodes through their GraphX input/output packet contracts.

Files to delete:

- Old public pre-GraphX pseudo-node headers/classes that used `Node` names but were not GraphX nodes.
- Direct pseudo-node unit tests that bypass GraphX node contracts.

Tests to add:

- Each FHSS GraphX node accepts and emits the PR7A GraphX packet types wrapped in `graph::gpu::accel::ControlToken<...>`.
- Compile-time/type-contract tests prove every FHSS node port type is a `graph::gpu::accel::ControlToken<PR7A_PACKET_TYPE>` or, where repository-consistent, `graph::gpu::accel::ControlToken<graph::message::Message>` carrying a PR7A packet sidecar. Raw PR7A packet edges are not acceptable.
- Node tests execute via the repository's standard GraphX node API, not direct calls to old helper `Node` classes.
- GraphX node outputs preserve the behavior previously verified by PR1-PR7: protocol validation, deterministic generation, detection, merge, CPSM symbol decisions, word decode, hop-only preamble lock, assembly, and diagnostics.
- Plugin/provider registration tests exist for any node exposed through the plugin path in this PR.
- No test includes or calls deleted pseudo-node classes.

Tests to delete:

- PR1-PR7 direct helper-node tests after equivalent GraphX node tests exist.

Acceptance criteria:

- Every `...Node` named FHSS component in the target graph is a real GraphX node.
- Every FHSS GraphX node edge data type is a `graph::gpu::accel::ControlToken` template type; raw FHSS packet payloads must not be exposed as GraphX node input/output port types.
- PR7A packet contracts are carried as token sidecars/payloads so that FHSS semantic metadata is preserved independently from future accelerator transport state.
- Previous pre-GraphX pseudo-nodes are removed from the code base.
- There is no compatibility shim preserving the old pseudo-node API.
- All FHSS behavior remains covered through GraphX node tests.
- The lane remains CPU-only and does not introduce Metal/GPU execution.

Risks:

- This is a broad architectural correction and may touch many tests.
- GraphX node APIs may expose awkward fan-in/streaming boundaries that require small contract adjustments from PR7A.

Rollback plan:

- Revert the GraphX node conversion and restore the last verified PR7 helper state if the conversion cannot be completed safely.

CI-safe or local-only:

- CI-safe.

## PR7C: Remove Pre-GraphX FHSS Node Scaffolding And Guard Against Regression

Purpose:

- Complete the cleanup by deleting all remaining pre-GraphX pseudo-node scaffolding and adding guardrails so it does not return.
- Ensure the code base has one canonical FHSS node model: GraphX nodes using PR7A GraphX edge contracts.

Files to touch:

- FHSS headers/sources to remove leftover pseudo-node types.
- Tests and docs that still reference helper `...Node` classes.
- Guardrail tests or repository scans for forbidden pseudo-node patterns.

Files to delete:

- Any remaining public FHSS helper headers/classes with `Node` names that are not GraphX nodes.
- Any tests that directly exercise deleted pseudo-node APIs.
- Any reports/docs that describe the deleted pseudo-nodes as canonical.

Tests to add:

- Guardrail test that FHSS public `...Node` classes inherit/implement the repository's GraphX node contract or are registered GraphX plugin nodes.
- Guardrail test that no `libdsp/include/dsp/fhss/*Node*.hpp` pseudo-node headers remain unless they are real GraphX nodes.
- Guardrail test that FHSS tests do not include deleted pre-GraphX pseudo-node headers.
- GraphX node test suite still passes after deletion.

Tests to delete:

- All remaining tests that directly call old pseudo-node APIs.

Acceptance criteria:

- The old pre-GraphX pseudo-node implementation is gone from the code base.
- GraphX node tests are the only canonical FHSS node tests.
- The target graph names correspond to real GraphX nodes only.
- No backward compatibility is preserved for the old pseudo-node APIs.

Risks:

- Report files under `plan/reviews/` may still mention old pseudo-node names; guardrails should focus on source/test contracts unless the PR explicitly updates historical reports.

Rollback plan:

- Restore deleted files only if a real GraphX node replacement is missing.

CI-safe or local-only:

- CI-safe.

## PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

Purpose:

- Wire the PR1-friendly CPU graph through existing GraphX JSON config, plugin loading, graph builder, executor, and completion conventions.
- Exercise synthetic IQ through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, preamble lock, message assembly, and truth comparison.
- Emit minimum deterministic diagnostics.
- Use only real GraphX FHSS nodes and GraphX FHSS edge packet contracts introduced by PR7A-PR7C.

Files to touch:

- `libdsp/config/fhss_cpsm_fixture_500msps.json` or a repository-consistent config path.
- FHSS plugin CMake files and plugin source files for the new nodes.
- `libgraph/test/unit` or `examples/DSP/test` for end-to-end graph/runtime tests.
- Optional `examples/DSP/src/main.cpp` extension or new FHSS demo runner only if repository conventions favor it.

Files to delete:

- None.

Tests to add:

- Graph config loads the FHSS nodes through the existing plugin/provider path.
- Graph config uses only real GraphX FHSS nodes from PR7B/PR7C; no pre-GraphX pseudo-node helpers are referenced.
- Executor runs the full deterministic fixture to completion.
- Decoded pulses match truth for start, duration, frequency index, and value.
- Assembled message locks on hop-only preamble and validates active set.
- Diagnostics contain required minimum fields.
- Diagnostics also identify RF metadata frequency, IQ offset frequency, sample-time mapping, synchronization assumption, and any unsupported-overlap/unsupported-impairment rejection.
- Graph is CPU-only and does not use Metal/GPU nodes.

Tests to delete:

- None.

Acceptance criteria:

- Full deterministic FHSS lane passes in CI.
- The graph preserves complex IQ through word recovery.
- All graph edges use the PR7A GraphX FHSS edge packet contracts.
- No deleted pre-GraphX pseudo-node class is used by config, plugins, tests, or runtime wiring.
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
- Document that PR7A-PR7C replaced the earlier pre-GraphX pseudo-node scaffolding and that the canonical FHSS implementation uses GraphX nodes and GraphX edge contracts.

Files to touch:

- `docs/dsp/fhss_decoder.md` or equivalent.
- `README.md` only if DSP examples are indexed there.
- Documentation/guardrail tests where existing conventions support them.

Files to delete:

- None.

Tests to add:

- Guardrail test that FHSS docs mention baseband/offset frequencies and do not claim direct 1 GHz RF sampling at `500 Msps`.
- Guardrail test that docs/config identify CPU-only PR1 behavior.
- Guardrail test that docs/config identify GraphX nodes as the canonical FHSS node model and do not describe deleted pre-GraphX pseudo-nodes as current.
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

- Future work remains explicitly out of scope for PR1 through PR9, including the PR7A-PR7C GraphX node correction gate.
- Candidate future work is limited to:
  - real channelizer topology and filter-width validation;
  - occupied-bandwidth measurement/estimation for the selected CPSM pulse shape;
  - explicit full-table/selectable-frequency scanning strategy if preserving 8 MHz spacing at 500 Msps is insufficient;
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
