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


## RF/SIGNAL-PROCESSING REVIEW UPDATE

The existing graph architecture is retained. The corrections below tighten the RF and DSP assumptions without changing the node topology.

### 1. Full 64-Frequency Channelization At 500 Msps Is Not Automatically Valid

The current table has 64 RF metadata centers at 8 MHz spacing. The full edge-to-edge center span from index 0 to index 63 is:

```text
(63 - 0) * 8 MHz = 504 MHz
```

A complex sampled stream at 500 Msps has only a 500 MHz Nyquist-width interval. Therefore, all 64 centers cannot be represented as simultaneously active, alias-free complex-baseband channels with nonzero occupied bandwidth. This is true even before CFO, filter transition width, and guard requirements are considered.

The selectable interior range `[1, 62]` spans:

```text
(62 - 1) * 8 MHz = 488 MHz
```

That interior set may fit into a 500 Msps complex-baseband fixture only if:

```text
occupied_bandwidth_hz + 2 * max_abs_cfo_hz + filter_transition_guard_hz <= 12 MHz
```

and only if the edge/guard channels `0` and `63` are not treated as simultaneously realized occupied channels.

Roadmap correction:

```text
At 500 Msps, the first channelized fixture may instantiate logical metadata for all 64 indices, but physical/sample-domain channelization must be limited to a validated capture subset unless the occupied bandwidth and guard analysis proves the selected channel set fits inside the complex Nyquist interval. Reserved indices 0 and 63 may be metadata/guard entries, but they must not require occupied sampled channels in the 500 Msps fixture.
```

If the plan wants true simultaneous full-table channelization, it must choose a higher fixture sample rate or retuned capture windows. A safe rule is:

```text
required_sample_rate_hz >
    (max_center_offset_hz - min_center_offset_hz)
  + occupied_bandwidth_hz
  + 2 * max_abs_cfo_hz
  + filter_transition_guard_hz
```

### 2. 8 MHz Spacing Is Plausible For A Fixture But Still Requires Occupied-Bandwidth Proof

The move from 4 MHz to 8 MHz spacing is directionally correct for a 5 Mbps CPSM waveform. However, the roadmap must not claim production-grade adjacent-channel separation until the CPSM occupied bandwidth is measured or bounded.

For `h = 1/2` full-response rectangular CPM, the instantaneous frequency deviation scale is approximately:

```text
delta_f = h / (2 * T_b)
        = 0.5 / (2 * 200 ns)
        = 1.25 MHz
```

This does not by itself define occupied bandwidth. Burst envelope truncation, rectangular frequency pulse shape, receiver filter shape, and spectral mask convention all affect the usable bandwidth. PRs should therefore add either a deterministic spectral-occupancy measurement fixture or continue labeling the 8 MHz grid as fixture-grade only.

Required test/analysis addition:

```text
Generate a representative CPSM pulse train and estimate occupied bandwidth under a declared threshold, such as 99% power bandwidth or -40 dBc bandwidth. Use that result only as fixture validation unless a real RF spectral mask is later adopted.
```

### 3. Channelizer Count Must Distinguish Logical Frequency Entries From Realized Sample-Domain Channels

The current roadmap says channel count equals configured frequency count. That is acceptable as a logical GraphX contract, but it becomes RF-incorrect if interpreted as “all 64 RF channels are physically present in one 500 Msps complex-baseband sampled stream.”

Roadmap correction:

```text
Configured frequency count = protocol/logical table size.
Realized channel count = number of frequency entries included in the current capture/sub-band/channelizer configuration.
For PR11-PR14, the planner must explicitly state whether ChannelizerNode instantiates all logical entries, only the active/capture subset, or metadata-only guard entries.
```

The graph topology does not change. The channelizer node simply needs a configuration distinction between:

```text
protocol_frequency_table[]
capture_frequency_subset[]
metadata_only_guard_indices[]
```

### 4. Detector And Viterbi Boundary Remains Correct

The detector should emit timing/frequency hypotheses and dehopped complex evidence. It should not become an undocumented CPSM decoder. The CPSM branch metric and Viterbi/MLSE stages remain the owners of sequence decisions. This separation is architecturally correct and should be preserved.

### 5. Rectangular Pulse Envelope Is Deterministic But RF-Noisy

The current fixture uses rectangular active pulse gating. That is acceptable for deterministic CI, but it creates spectral splatter at pulse boundaries. The roadmap should explicitly state that raised-cosine or other amplitude tapering is future work and may change occupied bandwidth, detector thresholds, and truth-comparison tolerances.

PR1-PR9 should keep the rectangular envelope for reproducibility, but must avoid RF-performance claims based on it.

### 6. Synchronization Assumption Is Acceptable But Must Be Visible In Outputs

`message_start_sample = 0` and known slot timing are valid PR1 assumptions. Diagnostics should include:

```text
synchronization_mode = known_start_sample
message_start_sample = 0
pulse_period_samples = 6500
slot_alignment = integer_sample_aligned
```

Future acquisition can be added later without changing the graph.

### 7. Missing Decisions To Add To The Roadmap

The following decisions remain needed before RF/channelizer claims can be strengthened:

1. Define the exact occupied-bandwidth metric: 99% power, -X dBc, or explicit spectral mask.
2. Decide whether PR11-PR14 channelization realizes all 64 entries, only `[1,62]`, only the active four, or a capture subset.
3. Decide whether guard indices `0` and `63` are metadata-only, instantiated empty channels, or actual filtered edge channels.
4. Define channel filter passband, transition width, stopband attenuation, and group delay.
5. Define `capture_center_frequency_hz` and `iq_center_frequency_hz` semantics for offset generation.
6. Define whether IQ offsets are allowed to be negative, how they are ordered, and how channel id maps to frequency index.
7. Define whether rectangular burst edges are acceptable for all deterministic tests or whether an optional tapered-envelope fixture is added later.
8. Define whether terminal CPM phase is ignored, reported, or used as a weak consistency check.
9. Define the maximum expected CFO/Doppler guard even while impairments are disabled.
10. Define the minimum channelizer feasibility test that prevents accidental production RF claims.

## Decided Baseline

- Sample rate: `500 Msps`.
- Bit rate: `5 Mbps`.
- Symbol timing: `100 samples/symbol`, `32 symbols/pulse`, `3200` pulse samples, `3300` gap samples, `6500` samples per pulse period.
- Frequency table: 64 RF metadata entries; selectable active indices are `[1, 62]`; reserved edge indices `0` and `63` are invalid for preamble and payload selection.
- Channelization invariant: the receiver/channelizer model must keep one
  logical frequency identity per configured FHSS frequency entry. At `500 Msps`,
  this must not be misread as a claim that all 64 RF centers are physically
  realized as simultaneous alias-free sampled channels. Reserved edge indices
  `0` and `63` may exist as guard/metadata entries, but they remain invalid for
  transmitted preamble or payload/body pulses. Any realized channelizer must
  explicitly state its capture subset and guard treatment.
- Modulation: binary CPSM using `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`.
- Initial CPSM assumptions: `h=1/2`, rectangular full-response phase pulse, initial phase `0`, continuity inside each pulse only, terminal phase unconstrained unless checked by the decoder PR.
- RF realism caveat: 5 Mbps CPSM on 8 MHz-spaced channels requires occupied-bandwidth/channel-filter validation before any production-like channelizer claim.
- Preamble detection: hop-only. Preamble word values are fixture consistency / secondary validation only.
- Current PR8 payload/body frequency rule: randomly select from the four active preamble frequencies using a deterministic seed.
- Planned source/message rule: replace random payload selection with explicit message definitions. Each configured message must specify its transmit time and every pulse's frequency index and `uint32_t` value.
- PR1 synchronization: `message_start_sample = 0`; pulse-start acquisition is future work.
- PR1 overlap policy: reject and report overlapped messages as unsupported.
- Diagnostics: final schema may be deferred, but PR1+ must expose at least `pulse_count`, `rejected_count`, `global_start_sample`, `frequency_index`, `confidence`, `viterbi_path_metric`, `decoded_value`, `preamble_lock`, and `truth_mismatch_count` when relevant.

## Consolidated Deferred Work Backlog

The following items are deferred from the deterministic PR1-through-PR9 lane.
They are collected here so later PRs can promote them deliberately rather than
rediscovering them from scattered caveats.

### Channelizer And Per-Channel Detector Path

- Refactor the IQ source before channelizer work so tests can specify complete
  message schedules rather than relying on implicit random payload frequency
  selection.
- Define an explicit `FHSSDownconverterNode` between the IQ source and
  channelizer. The node may be a validated passthrough when the source IQ frame
  already matches the channelizer input frame, but the graph must not hide that
  frequency-frame assumption inside the channelizer.
- Define a `ChannelizerNode` contract that takes sampled complex IQ and emits
  one channel packet per configured FHSS frequency, preserving global sample
  time, decimation factor, filter group delay, RF metadata frequency, IQ offset
  frequency, frequency index, and channel id.
- Define `PerChannelPulseDetectorNode` input/output contracts. The detector
  should consume one channel stream and emit timing/frequency pulse evidence
  for that channel only.
- PR11+ channelizer work must preserve the invariant that channel count equals
  configured frequency count. The four active preamble-derived frequencies are
  a transmit/message active set, not a limit on receiver channel topology.
- Define duplicate/collision semantics when two channel detectors emit
  candidates for the same global pulse slot.
- Keep CPSM branch metric/Viterbi as the owner of symbol sequence evidence.
  Per-channel detectors may estimate timing/frequency and dehopped evidence,
  but must not become undocumented word decoders.

### RF And Spectral Feasibility

- Validate 5 Mbps CPSM occupied bandwidth against 8 MHz hop spacing.
- Choose channel filter passband width, transition bandwidth, stopband
  attenuation, group delay, adjacent-channel rejection, and sample-rate/decimation
  policy before making channelizer separation claims.
- Decide whether future full selectable-frequency coverage uses a higher sample
  rate, retuned sub-band windows, sparse active-frequency scheduling, or an
  explicit aliasing/downconversion model. Whatever strategy is chosen, it must
  retain one logical channel per configured FHSS frequency.
- Preserve the rule that absolute 1 GHz RF frequencies are metadata while
  fixture IQ uses baseband/IF offsets. The downconverter translates between
  declared IQ reference frames; it must not directly mix against absolute
  1 GHz RF at `500 Msps`.

### Synchronization And Timing

- Add source-message transmit time semantics before receiver acquisition work.
  A message should be placed by `transmit_start_sample` or an equivalent
  validated `transmit_start_seconds` converted to global samples.
- Add pulse-start acquisition beyond `message_start_sample = 0`.
- Add fractional timing error handling and message-start search.
- Define how channelizer group delay is compensated before pulse merge,
  provisional slot indexing, final slot indexing, and preamble lock.

### Source Message Configuration

- Replace `payload_values` plus `payload_random_seed` with an explicit
  `messages[]` schema.
- Each message must specify a stable `message_id`, a transmit time, and an
  ordered pulse list.
- Each pulse must specify `frequency_index`, `value`, and whether it belongs to
  the preamble or body/payload.
- The source should support zero messages with an explicit idle output policy:
  prefer `idle_mode = "zero"` as the deterministic default; optionally add
  deterministic noise later with a separate seed and impairment policy.
- A zero-message source still needs an explicit output duration, such as
  `idle_duration_samples` or the repository's equivalent source packet length.
- Multiple scheduled messages must be rejected if their pulse windows overlap
  until overlap-aware separation is implemented.
- IQ offset frequencies should be derived from supplied RF frequencies and an
  explicit downconversion/capture center, for example:

```text
rf_frequency_hz = rf_frequency_table_hz[frequency_index]
iq_offset_frequency_hz = rf_frequency_hz - iq_center_frequency_hz
```

- Validation must still enforce Nyquist, occupied-bandwidth, and CFO guards for
  all active pulse offsets.

### Impairments And Estimator Robustness

- Add deterministic noise, Doppler, CFO, phase drift, multipath, and symbol
  timing error fixtures only after the noise-free channelized lane is stable.
- Define confidence degradation and diagnostics under impairments.
- Decide when unsupported-impairment rejection changes into supported
  impairment estimation.

### Message Association And Overlap

- Keep PR1-through-PR9 overlap behavior as deterministic rejection.
- Add overlap-aware pulse association and message separation in a later PR only
  after channelized pulse timing and collision diagnostics are stable.

### Error Model And Diagnostics

- Define stable status values for unknown frequency, low confidence, bad word
  decode, missing preamble, invalid timing config, unsupported overlap,
  unsupported Doppler/noise, channelizer rejection, overlength message, and
  truth mismatch.
- Promote the current minimum diagnostics into a stable schema only after the
  channelizer/per-channel detector contracts settle.
- Keep optional PDW diagnostics separate from canonical decoder output.

### Acceleration

- Keep the FHSS lane CPU-only until channelized CPU correctness is locked.
- Future Metal/GPU acceleration must preserve the PR7A semantic sidecar model
  and `graph::gpu::accel::ControlToken<...>` edge contracts.


## RF And Signal-Processing Correctness Addendum

The following items are additional correctness constraints and planning
decisions that apply to the current PR1-through-PR9 fixture graph and must be
carried forward into the PR11+ channelizer/per-channel detector sequence.

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

The roadmap now has two graph shapes:

- **Current PR8 CPU fixture graph:** uses `FHSSCorrelatorBankDetectorNode` as a
  deterministic, known-slot, four-active-frequency stand-in for channelized
  detection.
- **Longer-term channelized graph:** introduces `FHSSDownconverterNode`,
  `ChannelizerNode`, and per-frequency/per-channel pulse detectors, then keeps
  the existing merge, CPSM decode, pulse-word, preamble, assembler, and sink
  contracts downstream.

The migration should preserve the downstream PR7A packet contracts. The
channelizer/per-channel detector path replaces only the detector front end:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
      -> PerChannelPulseDetectorNode[frequency 0]
      -> PerChannelPulseDetectorNode[frequency 1]
      -> ...
      -> PerChannelPulseDetectorNode[frequency N-1]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

The invariant is that `N` equals the number of configured FHSS frequency
entries. With the full 64-entry table, `N = 64`; indices `0` and `63` are still
reserved for transmission even if guard/metadata channels are instantiated.

Longer-term channelized shape once full selectable-frequency receiver coverage
is explicitly planned:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
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
  -> FHSSCorrelatorBankDetectorNode
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
- Interior selectable-set validation documents the remaining 12 MHz Nyquist margin for `[1,62]` and requires `occupied_bandwidth_hz + 2*CFO + filter_guard <= 12 MHz` before claiming simultaneous interior-set channelization.

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

- `FHSSPreambleDetectorNode`, `FHSSMessageAssemblerNode`, and `FHSSMessageSinkNode` headers/sources as temporary pre-GraphX scaffolding only; PR7A-PR7D must replace them with real dynamically loadable GraphX nodes before PR8.
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

## PR7D: Split FHSS GraphX Nodes Into Per-Node Files And Plugins

Purpose:

- Replace the temporary unified FHSS GraphX node header with one header/source pair per FHSS GraphX node.
- Add every FHSS GraphX node to the repository plugin system so each node can be dynamically loaded.
- Keep PR7A edge contracts, PR7B token-wrapped GraphX node ports, and PR7C guardrails intact.

Files to touch:

- FHSS GraphX node headers/sources for one-file-per-node layout:
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
- Shared FHSS GraphX node utility header/source only for common token/metadata conversion helpers; it must not define node classes.
- FHSS plugin source files and plugin/provider CMake wiring for every FHSS GraphX node.
- Unit tests for per-node include boundaries, plugin registration/loading, and existing GraphX node behavior.

Files to delete:

- The unified `FHSSGraphXNodes.hpp` node-definition file after all node classes are split into their own headers/sources.
- Any tests or includes that depend on the unified node-definition header as the canonical node definition.

Tests to add:

- Guardrail test that no unified FHSS GraphX node definition header/source exists.
- Guardrail test that each FHSS GraphX node has its own header and source file.
- Guardrail test that shared FHSS node utility files do not define `...Node` classes.
- Plugin/provider registration tests for every FHSS GraphX node.
- Dynamic loading tests proving each FHSS node can be resolved through the plugin system.
- Existing GraphX node token-contract and behavior tests updated to include per-node headers rather than the deleted unified header.

Tests to delete:

- Tests that include the unified FHSS GraphX node definition header as the canonical node source.

Acceptance criteria:

- Every FHSS GraphX node is implemented in its own header/source file.
- No unified FHSS node-definition file remains.
- Every FHSS GraphX node is registered with the plugin system and can be dynamically loaded.
- All FHSS GraphX node edges remain `graph::gpu::accel::ControlToken<...>` types carrying PR7A packet sidecars.
- PR7C guardrails still pass and prevent pre-GraphX pseudo-node regression.
- No graph JSON end-to-end executor wiring is required yet.

Risks:

- Splitting a header-only implementation into source files may expose missing explicit instantiations, include cycles, or plugin linkage issues.
- Plugin naming and provider registration must align with existing repository conventions to avoid PR8 graph-config churn.

Rollback plan:

- Restore the PR7C unified-header state only if per-node plugin registration cannot be completed safely.

CI-safe or local-only:

- CI-safe.

## PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

Purpose:

- Wire the PR1-friendly CPU graph through existing GraphX JSON config, plugin loading, graph builder, executor, and completion conventions.
- Exercise synthetic IQ through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, preamble lock, message assembly, and truth comparison.
- Emit minimum deterministic diagnostics.
- Use only real dynamically loadable GraphX FHSS nodes and GraphX FHSS edge packet contracts introduced by PR7A-PR7D.

Files to touch:

- `libdsp/config/fhss_cpsm_fixture_500msps.json` or a repository-consistent config path.
- `libgraph/test/unit` or `examples/DSP/test` for end-to-end graph/runtime tests.
- Optional `examples/DSP/src/main.cpp` extension or new FHSS demo runner only if repository conventions favor it.

Files to delete:

- None.

Tests to add:

- Graph config loads the FHSS nodes through the PR7D plugin/provider path.
- Graph config uses only real GraphX FHSS nodes from PR7B-PR7D; no pre-GraphX pseudo-node helpers are referenced.
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
- Graph config names must match the PR7D plugin/provider registrations.

Rollback plan:

- Remove FHSS config, plugins, end-to-end tests, and optional runner changes.

CI-safe or local-only:

- CI-safe.

## PR9: Documentation And Truth-In-Labeling Guardrails

Purpose:

- Document the FHSS fixture lane, protocol limits, CPSM assumptions, baseband/offset frequency mapping, unsupported overlap policy, and future boundaries.
- Add guardrails against mislabeling the fixture as production RF, external waveform compatibility, GPU/Metal acceleration, or magnitude-only decoding.
- Document that PR7A-PR7D replaced the earlier pre-GraphX pseudo-node scaffolding and that the canonical FHSS implementation uses per-node, dynamically loadable GraphX nodes and GraphX edge contracts.

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

## PR10: Explicit FHSS IQ Source Message Schedule And Frequency Mapping

Purpose:

- Replace the PR8 source fixture's implicit single-message/random-payload model
  with an explicit configured message schedule.
- Let `FHSSSyntheticIqSourceNode` generate IQ from a set of configured messages,
  each with a transmit time and a complete ordered pulse list.
- Derive `iq_offset_frequency_hz` from supplied RF frequency values and an
  explicit IQ/downconversion center instead of requiring every active offset to
  be hard-coded independently.
- Define deterministic idle output behavior when no messages are configured.

Files to touch:

- `libdsp/include/dsp/fhss/FHSSProtocol.hpp` or equivalent protocol/config
  header for message schedule types.
- `libdsp/include/dsp/fhss/FHSSSyntheticIqGenerator.hpp`.
- `libdsp/include/dsp/fhss/FHSSSyntheticIqSourceNode.hpp`.
- `libdsp/include/dsp/fhss/FHSSGraphXConfig.hpp`.
- `libdsp/config/fhss_cpsm_fixture_500msps.json`.
- FHSS generator/source/config tests under `libgraph/test/unit`.
- `docs/dsp/fhss_decoder.md` for the explicit message schedule schema.

Files to delete:

- Remove or deprecate source config fields that exist only for implicit random
  payload selection once equivalent explicit-message tests exist:
  `payload_values`, `payload_random_seed`, and
  `payload_random_deterministic`.

Tests to add:

- Source config accepts `messages[]` with multiple messages.
- Each message has a stable `message_id`.
- Each message has `transmit_start_sample` or a validated equivalent transmit
  time converted to global samples.
- Each pulse explicitly supplies `frequency_index`, `value`, and preamble/body
  role.
- Generated truth metadata matches configured message id, transmit time,
  global pulse start, duration, frequency index, RF metadata frequency, derived
  IQ offset frequency, value, and preamble flag.
- Gaps before, between, and after messages are filled according to the explicit
  idle policy.
- Payload/body pulse frequencies are not selected randomly.
- The source rejects reserved indices `0` and `63` in any message pulse.
- The source rejects overlength messages above 256 pulses including preamble.
- The source rejects overlapping scheduled messages in PR10.
- The source validates identical-frequency preamble word consistency.
- IQ offsets are derived as `rf_frequency_hz - iq_center_frequency_hz` and
  checked against Nyquist, occupied-bandwidth, and CFO guards.
- No-message config emits deterministic idle samples according to explicit
  `idle_mode` and explicit output duration; default should be zero/NULL
  complex samples, not random noise.
- Tests prove any random-noise idle mode, if added, is deterministic and kept
  separate from Doppler/noise impairment support.

Tests to delete:

- Tests whose only purpose is deterministic random payload frequency selection,
  after explicit message schedule coverage replaces them.

Acceptance criteria:

- `FHSSSyntheticIqSourceNode` can be configured through the existing GraphX
  `Configure` path with a full message schedule.
- A message completely specifies all transmitted pulse frequency indices and
  pulse values and has a stable `message_id`.
- The source no longer needs a random seed to select payload/body frequencies.
- Frequency metadata can be supplied as RF table values plus an IQ center; IQ
  offsets are derived and validated.
- Messages can start at nonzero transmit times.
- Multiple messages are allowed when their scheduled sample windows do not
  overlap.
- Zero-message source behavior is explicit and deterministic, including the
  number of idle samples emitted.
- Existing PR8 end-to-end fixture behavior remains covered by rewriting the
  fixture config into the new explicit-message schema.
- No receiver acquisition, overlap support, channelizer implementation,
  Doppler/noise behavior, Metal/GPU execution, or production RF claim is added.

Risks:

- Updating the source schema will require matching updates to detector,
  assembler truth generation, docs, and PR8 graph JSON tests.
- Supporting multiple non-overlapping messages may expose assumptions in the
  current one-message assembler; keep overlap unsupported unless a later PR
  adds association/tracking.

Rollback plan:

- Restore the PR8 source config schema and deterministic-random payload tests.

CI-safe or local-only:

- CI-safe.

## PR11: FHSS Channelizer And Per-Channel Edge Contracts

Purpose:

- Define the GraphX packet contracts needed to replace the PR8
  correlator-bank front end with an explicit downconverter, channelizer, and
  per-channel pulse detectors.
- Define the `FHSSDownconverterNode` edge contract between synthetic/source IQ
  and channelizer input. Passthrough is allowed when the input and output IQ
  reference frames match, but the passthrough must be explicit and validated.
- Preserve global sample-time mapping through channelization before any
  channelizer implementation is added.
- Make the channel-per-frequency invariant explicit: every configured FHSS
  frequency entry has exactly one logical channel contract.
- Keep transmit active-set validation separate from receiver channel topology;
  the four active preamble-derived frequencies do not limit channel count.

Files to touch:

- `libdsp/include/dsp/fhss/` for channelized FHSS packet/metadata contracts.
- FHSS GraphX packet contract tests under `libgraph/test/unit`.
- `docs/dsp/fhss_decoder.md` if a short architecture note is needed.

Files to delete:

- None.

Tests to add:

- Compile-time tests that channelizer input/output and per-channel detector
  input/output edge types are `graph::gpu::accel::ControlToken<...>` sidecars.
- Contract tests for channel id, frequency index, RF metadata frequency, IQ
  offset frequency, channel sample rate, decimation factor, group delay, and
  input global sample origin.
- Contract tests for downconverter input center/reference frequency,
  output/channelizer center frequency, translation frequency, passthrough flag,
  phase convention, sample rate, and preserved global sample origin.
- Tests proving global sample time can be reconstructed after channelization.
- Tests proving channel ids map one-to-one with configured frequency indices.
- Tests proving reserved indices `0` and `63` can exist as receiver channels
  while remaining invalid for transmitted preamble/body selection.

Tests to delete:

- None.

Acceptance criteria:

- `FHSSChannelizedIqPacket` or equivalent contract exists for per-channel IQ.
- `FHSSDownconvertedIqPacket` or equivalent contract exists for IQ entering the
  channelizer.
- `FHSSPerChannelPulseEvidencePacket` or equivalent contract exists for
  per-channel detector output.
- The downconverter contract states whether the operation is passthrough or
  frequency translation and preserves source global sample timing.
- The contract states that channel count equals configured frequency count.
- Contracts preserve complex IQ evidence and global sample-time mapping.
- Contracts do not expose raw FHSS packet payloads as GraphX node port types.
- No channelizer implementation, detector implementation, graph JSON, Metal/GPU
  execution, Doppler/noise behavior, or production RF claim is added.

Risks:

- Over-generalizing the channelizer contract could make the CPU lane harder to
  review.
- Under-specifying group delay or decimation could make later pulse merge
  timing ambiguous.

Rollback plan:

- Remove the new packet contracts and tests.

CI-safe or local-only:

- CI-safe.

## PR12: FHSS DownconverterNode And Frequency-Parallel CPU ChannelizerNode

Purpose:

- Add a CPU `FHSSDownconverterNode` that converts source IQ into the
  channelizer reference frame, or validates explicit passthrough when no
  frequency translation is required.
- Add a CPU `ChannelizerNode` for the deterministic FHSS fixture using one
  logical output channel per configured FHSS frequency.
- Produce per-channel complex IQ packets with explicit sample-time mapping for
  later per-channel pulse detectors.
- Keep the implementation fixture-grade and do not claim production channelizer
  separation.

Files to touch:

- `libdsp/include/dsp/fhss/FHSSDownconverterNode.hpp` or repository-consistent
  name.
- `libdsp/src/dsp/FHSSDownconverterNode.cpp`.
- `libdsp/include/dsp/fhss/ChannelizerNode.hpp` or repository-consistent name.
- `libdsp/src/dsp/ChannelizerNode.cpp`.
- FHSS plugin/provider wiring if the node is dynamically loadable in this PR.
- FHSS GraphX node tests and guardrails.
- `docs/dsp/fhss_decoder.md` for the fixture-only channelizer note if needed.

Files to delete:

- None.

Tests to add:

- Downconverter config declares source IQ center/reference, channelizer
  center/reference, translation frequency, and passthrough mode.
- Downconverter passthrough mode preserves complex samples and global timing
  exactly when source and channelizer reference frames match.
- Downconverter frequency-translation mode mixes by the declared IQ offset
  delta, not by absolute 1 GHz RF metadata.
- Downconverter rejects implicit frequency-frame mismatches.
- Channelizer config may include reserved indices `0` and `63` as receiver
  channels, but rejects them as transmitted active/pulse frequencies.
- Channelizer emits one channel packet per configured frequency index.
- Channelizer rejects duplicate configured frequency indices or duplicate
  channel ids.
- Channel packets preserve RF metadata frequency, IQ offset frequency, channel
  id, channel sample rate, decimation factor, group delay, and input global
  sample origin.
- Channelized complex evidence remains usable by downstream per-channel pulse
  detection.
- Type/registration tests prove `ChannelizerNode` is a real GraphX node with
  token-wrapped edges.
- Type/registration tests prove `FHSSDownconverterNode` is a real GraphX node
  with token-wrapped edges.

Tests to delete:

- None.

Acceptance criteria:

- `FHSSDownconverterNode` exists as a real GraphX node and is plugin-loadable if
  exposed through the plugin path.
- The downconverter emits PR11 downconverted IQ packet contracts and makes
  passthrough versus translation explicit.
- `ChannelizerNode` exists as a real GraphX node and is plugin-loadable if
  exposed through the plugin path.
- The node uses PR11 channelized packet contracts.
- The node enforces a documented mapping between configured frequency entries and realized channel outputs. If the fixture realizes fewer than all logical entries at `500 Msps`, the node must expose the capture subset and metadata-only guard treatment explicitly.
- The first implementation may still use fixture-safe IQ offsets, but it must
  not collapse multiple frequencies into one channel.
- The node does not replace the PR8 graph yet unless explicitly wired in a
  later PR.
- No real RF capture, production channelizer claim, Metal/GPU execution,
  Doppler/noise behavior, or overlap-aware separation is added.

Risks:

- A simple mixer/filter/downsample path may not represent production
  channelizer quality. Label it as fixture-grade until occupied bandwidth and
  filter requirements are validated.
- Incorrect group-delay reporting would break merge timing later.

Rollback plan:

- Remove the downconverter/channelizer nodes, plugin registration, and tests.

CI-safe or local-only:

- CI-safe.

## PR13: PerChannelPulseDetectorNode And Merge Handoff

Purpose:

- Add `PerChannelPulseDetectorNode` for one channelized IQ stream.
- Replace the correlator-bank detector's monolithic active-frequency ranking
  with per-channel pulse candidate extraction while leaving CPSM sequence
  estimation downstream.
- Emit the same semantic detected-pulse/candidate evidence needed by
  `FHSSPulseMergeNode`.

Files to touch:

- `libdsp/include/dsp/fhss/PerChannelPulseDetectorNode.hpp` or
  repository-consistent name.
- `libdsp/src/dsp/PerChannelPulseDetectorNode.cpp`.
- FHSS plugin/provider wiring if exposed through the plugin path.
- FHSS GraphX node tests and guardrails.

Files to delete:

- None.

Tests to add:

- Detector consumes a single channel packet and emits detected pulse metadata
  in shared global sample time.
- Detector channel metadata identifies exactly one configured frequency index.
- Detector preserves/dehops complex evidence for CPSM branch metrics.
- Detector reports frequency index, RF metadata frequency, IQ offset frequency,
  estimated center frequency, CFO/frequency error placeholder or estimate,
  SNR/confidence, detector id, packet sequence, and channel id.
- Detector output merges cleanly through `FHSSPulseMergeNode`.
- Detector does not decode words, preamble, or CPSM symbol sequences.
- Type/registration tests prove `PerChannelPulseDetectorNode` is a real GraphX
  node with token-wrapped edges.

Tests to delete:

- None.

Acceptance criteria:

- Per-channel detector output is compatible with `FHSSPulseMergeNode`.
- Per-channel detector preserves complex evidence and global sample-time
  mapping.
- Detector does not scan across frequencies; it uses the single frequency index
  and channel metadata supplied by `ChannelizerNode`.
- No CPSM Viterbi/MLSE duplication, word decode, preamble detection, message
  assembly, Metal/GPU execution, Doppler/noise behavior, or overlap-aware
  separation is added.

Risks:

- Pulse timing in channelized samples can drift if group delay and decimation
  are not handled precisely.
- Detector confidence may need later refinement under noise/Doppler.

Rollback plan:

- Remove the per-channel detector node, plugin registration, and tests.

CI-safe or local-only:

- CI-safe.

## PR14: Channelized FHSS Graph JSON And Executor Test

Purpose:

- Add an alternate end-to-end FHSS graph JSON that uses
  `FHSSDownconverterNode -> ChannelizerNode -> PerChannelPulseDetectorNode[] -> FHSSPulseMergeNode`.
- Keep the PR8 correlator-bank fixture graph available as a reference until the
  channelized lane has equivalent coverage.
- Prove the channelized CPU lane decodes the deterministic fixture to the same
  message truth while keeping one logical channel per configured frequency.

Files to touch:

- `libdsp/config/` for a channelized FHSS fixture graph JSON.
- `libgraph/test/unit` for channelized executor tests.
- `docs/dsp/fhss_decoder.md` for the new graph command/test reference.

Files to delete:

- None.

Tests to add:

- Graph config loads `FHSSDownconverterNode`, `ChannelizerNode`, and one
  `PerChannelPulseDetectorNode` instance per configured frequency through the
  plugin/provider path.
- Graph config wires source IQ through the downconverter before channelization,
  even when the downconverter is configured as validated passthrough.
- Test config proves the detector node count equals the configured frequency
  count.
- Executor runs the channelized deterministic fixture to completion.
- Decoded pulses match truth for start, duration, frequency index, and value.
- Message locks on hop-only preamble and validates the active set.
- Diagnostics include channelizer sample-time mapping, channel ids, group
  delay/decimation, downconverter passthrough/translation state,
  synchronization assumption, and unsupported-overlap and
  unsupported-impairment rejection.
- Test proves the graph remains CPU-only and uses no Metal/GPU nodes.

Tests to delete:

- None initially.

Acceptance criteria:

- Channelized CPU graph passes in CI and preserves complex IQ through word
  recovery.
- PR8 correlator-bank graph and PR14 channelized graph produce equivalent
  decoded message truth on the deterministic fixture.
- The graph does not claim production channelizer separation or that the full
  64-entry RF table is alias-free in one 500 Msps complex-baseband capture.
- No real RF capture, external dataset, Metal/GPU execution, Doppler/noise
  behavior, overlap-aware separation, or canonical PDW diagnostic path is added.

Risks:

- Runtime graph fan-out/fan-in for one detector per configured frequency may
  expose GraphX scheduling or completion assumptions.
- Equivalent decode may require careful channelizer timing compensation.

Rollback plan:

- Remove the channelized graph JSON and executor tests; keep PR11-PR13 contracts
  and nodes if independently valid.

CI-safe or local-only:

- CI-safe.

## PR15: Channelized Lane Promotion And Correlator-Bank Deprecation Plan

Purpose:

- Decide when the channelized graph becomes the primary FHSS fixture graph.
- Mark the correlator-bank detector graph as a PR1 compatibility/reference
  topology or remove it if the channelized lane fully replaces it.
- Consolidate documentation and guardrails around the selected canonical graph.

Files to touch:

- `docs/dsp/fhss_decoder.md`.
- `libdsp/config/` graph config naming or aliases.
- FHSS executor and guardrail tests.
- Roadmap/review notes if the correlator-bank graph is retained as reference.

Files to delete:

- Optional: the PR8 correlator-bank graph config and dedicated tests only if the
  channelized graph is explicitly promoted and equivalent coverage exists.

Tests to add:

- Guardrail test identifying the canonical FHSS graph config.
- Regression test proving no doc/config labels the correlator-bank detector as
  production-like channelization.
- If retained, test that the correlator-bank graph is documented as a reference
  fixture path only.

Tests to delete:

- Optional correlator-bank-only executor tests after equivalent channelized
  coverage exists.

Acceptance criteria:

- The roadmap and docs clearly identify the canonical FHSS graph shape.
- No ambiguity remains between fixture correlator-bank detection and the
  longer-term channelizer/per-channel detector topology.
- CI still covers at least one full deterministic FHSS executor lane.

Risks:

- Removing the correlator-bank graph too early could make regressions harder to
  diagnose.
- Keeping both graphs indefinitely could split attention and confuse users.

Rollback plan:

- Restore the PR8 correlator-bank graph as the canonical fixture graph.

CI-safe or local-only:

- CI-safe.

## PR16: RF Feasibility, Full Selectable-Frequency Strategy, And Impairment Plan

Purpose:

- Resolve the deferred RF feasibility items before expanding receiver coverage
  claims or adding impairments.
- Choose the strategy for scanning/selecting from the interior `[1, 62]`
  frequency set without pretending all 64 RF centers fit alias-free in one
  500 Msps baseband capture.
- Preserve the invariant that a receiver configuration has one logical channel
  per configured frequency, even if physical capture must use higher sample
  rate, retuned sub-band windows, or explicit downconversion to realize those
  channels.
- Define the testable plan for Doppler/noise/CFO/phase drift and optional PDW
  diagnostics without implementing them in the channelized CPU fixture PRs.

Files to touch:

- `plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md`.
- `docs/dsp/fhss_decoder.md`.
- Optional analysis notes under `plan/reviews/` or `docs/dsp/`.

Files to delete:

- None.

Tests to add:

- Documentation/guardrail tests only if new claims are introduced.
- Optional offline spectral-analysis fixture tests if a deterministic
  occupied-bandwidth estimator is added.

Tests to delete:

- None.

Acceptance criteria:

- The plan defines occupied-bandwidth/channel-filter requirements or explicitly
  keeps them unresolved with no production channelizer claim.
- The plan chooses higher sample rate, retuned sub-band windows, sparse active
  scheduling, or explicit alias/downconversion modeling for future full
  selectable-frequency coverage.
- The plan defines which impairment diagnostics and status values become
  canonical before implementation.
- Optional PDW diagnostics remain non-canonical unless a later PR explicitly
  changes the decoder contract.

Risks:

- RF-realism analysis could force changes to fixture defaults or channel
  spacing assumptions.
- Premature full-table RF realism claims could obscure the simpler
  frequency-parallel message decoding goal.

Rollback plan:

- Revert to fixture-safe configured frequencies while preserving the
  channel-per-frequency invariant.

CI-safe or local-only:

- CI-safe for docs/guardrails; local-only if external RF analysis tooling is
  introduced.

## Future Boundary After The Channelized CPU Lane

Purpose:

- Capture work that remains beyond the PR11-through-PR16 channelized CPU
  migration path.

Files to touch:

- None in this roadmap.

Files to delete:

- None.

Tests to add:

- None in this roadmap.

Tests to delete:

- None.

Acceptance criteria:

- The PR11-through-PR16 sequence moves toward a CPU channelizer and per-channel
  pulse detector without adding production RF claims.
- Work that remains explicitly deferred after that sequence includes:
  - Doppler/noise/phase-offset impairments and estimator robustness;
  - pulse-start acquisition beyond `message_start_sample = 0`, unless promoted
    earlier by a dedicated synchronization PR;
  - overlap-aware message separation;
  - Metal acceleration after channelized CPU correctness is locked;
  - optional PDW diagnostics that do not become the canonical decoder input
    unless a later PR explicitly changes the decoder contract.

Risks:

- Future performance or RF realism pressure could blur the deterministic fixture boundary.

Rollback plan:

- None; this is a planning boundary.

CI-safe or local-only:

- Not applicable.
