Act as PLANNER using `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

# Task

Create a PR-sized roadmap for a GraphX frequency-hopping pulse decoder pipeline and matching synthetic IQ signal generator.

# Goal

Add a deterministic FHSS-style DSP demo/test lane where each pulse carries one full `uint32_t` value.

The system must:

1. Generate synthetic IQ from ground-truth FHSS pulses.
2. Process the IQ through GraphX DSP nodes.
3. Detect pulses and recover pulse timing and hop frequency.
4. Decode pulse words.
5. Detect preamble pulses.
6. Assemble complete messages.
7. Compare decoded results against truth.

This is a modem/receiver architecture, not an EW/PDW architecture.

---

# Core Invariants

One pulse equals one full `uint32_t` value.

Do not model pulses as bytes, nibbles, or symbols unless a later waveform specification requires it.

Message = ordered pulse train.

Preamble = known sequence of 32-bit-valued pulses.

Payload = decoded sequence of 32-bit-valued pulses.

The pulse value must be encoded inside the IQ waveform and recovered from IQ evidence.

The pulse value must not exist solely as truth metadata.

Truth metadata exists only for validation.

# Message Protocol Format

This protocol is an initial deterministic fixture protocol for GraphX testing and validation.

It is not a production RF interoperability claim.

## Frequency Set

The complete frequency set contains 128 frequencies.

Frequencies start at 1 GHz and are spaced every 4 MHz.

Derived frequency table:

```text
frequency_hz(index) =
    1'000'000'000 +
    index * 4'000'000

where index ∈ [0,127]
```

Derived defaults:

```text
frequency_count = 128
base_frequency_hz = 1'000'000'000
frequency_spacing_hz = 4'000'000
```

---

## Active Hop Pattern

Although 128 frequencies exist, only four frequencies are active during a message.

The active hop frequencies are determined by the externally supplied preamble hopping pattern.

Derived default:

```text
active_hop_frequency_count = 4
```

Decision: payload/body pulses may use only the four selected preamble frequencies. There is no defined hop behavior for the message body beyond that active-frequency constraint.

Decision: generated message-body fixtures shall choose randomly from the four selected preamble frequencies. The random selection must be deterministic for CI, using a configured seed.

The receiver active frequencies are derived from the externally supplied preamble hopping pattern.

---

## Message Structure

Messages consist of:

```text
16-pulse preamble
followed by
payload pulses
```

Messages may contain no more than 256 pulses total, including the 16 preamble pulses.

Derived defaults:

```text
preamble_pulse_count = 16
max_message_pulses = 256
```

---

## Pulse Semantics

One pulse equals one complete uint32_t value.

A pulse is never interpreted as:

* a byte;
* a nibble;
* a symbol fragment.

Message semantics:

```text
Message = ordered pulse train

Preamble = ordered sequence of 16 pulses

Payload = ordered sequence of pulses

One pulse = one uint32_t
```

---

## Pulse Timing

Nominal RF pulse width:

```text
pulse_width_us = 6.4
```

Pulse spacing:

```text
pulse_dead_time_us = 6.6
```

Pulse repetition period:

```text
pulse_period_us ≈ 13
```

Derived relationship:

```text
pulse_period_us =
    pulse_width_us +
    pulse_dead_time_us
```

The planner must decide whether pulse spacing is measured:

* edge-to-edge;
* center-to-center;
* start-to-start.

The chosen convention must be documented and used consistently.

---

## Modulation

The protocol shall use:

```text
5 Mbps CPSM modulation
```

Decision: model CPSM as binary Continuous Phase Modulation for this deterministic fixture.

This is not a claim of compatibility with any external RF waveform.

Constants:

```text
R_b = 5'000'000 bits/sec
T_b = 1 / R_b = 200 ns
bits_per_pulse = 32
pulse_width = 32 * T_b = 6.4 us
```

Each pulse carries one `uint32_t` value.

Bit ordering:

```text
MSB first unless explicitly overridden.
```

Information symbols:

```text
a[k] in {-1, +1}
```

Bit mapping:

```text
0 -> +1
1 -> -1
```

Phase trajectory:

```text
theta(t) = 2*pi*h * sum_k a[k] * q(t - k*T_b)

where:

    h is the modulation index
    T_b is the symbol period
    q(t) is the phase pulse
```

Complex baseband signal:

```text
s(t) = A * exp(j * theta(t))
```

The waveform shall have constant envelope and continuous phase.

Initial recommended values:

```text
h = 1/2
phase_pulse_shape = rectangular
R_b = 5'000'000 bits/sec
F_s = 500'000'000 samples/sec
samples_per_symbol = 100
continuity_scope = inside each pulse only
```

Passband / hopped complex IQ equation:

```text
x[n] =
    s_p[n] * exp(j * 2*pi * f_hop_p * n / F_s)

where:

    f_hop_p = available_frequencies_hz[frequency_index_p]
```

If generating complex baseband around the selected hop, set `f_hop_p` as an offset frequency. If generating RF-like sampled IQ, keep the absolute hop frequency but require `F_s` high enough to represent it without aliasing. For GraphX fixture tests, prefer offset/baseband frequencies.

Continuity rule:

```text
Within one pulse:
    phase is continuous according to theta(t).

Across symbol boundaries:
    phase remains continuous according to the selected q(t).

Across pulses:
    phase resets unless continuous_phase_across_pulses = true.
```

The planner must choose and document:

```text
modulation_index
phase_pulse_shape
exact q(t) normalization and support
continuity across pulse boundaries
matched filter
receiver estimator
```

Receiver estimator requirement:

```text
For each candidate pulse and hop frequency:

1. Mix down:

    y[n] = x[n] * exp(-j * 2*pi * f_hop * n / F_s)

2. Estimate the 32 information symbols from complex y[n] using the selected
   CPSM matched filter / estimator.

3. Map symbols back to bits:

    +1 -> 0
    -1 -> 1
```

Word decision rule:

```text
value = 0

for k in 0..31:
    value = (value << 1) | bit_k
```

Confidence metric:

```text
symbol_confidence_k =
    estimator-defined confidence for symbol k

pulse_confidence =
    mean(symbol_confidence_k)
```

Important planning note:

```text
The first implementation shall define CPSM as binary Continuous Phase
Modulation using the theta(t) equation above. The decoder must recover the
uint32_t from complex IQ samples using the selected CPSM estimator, not from
truth metadata and not from magnitude-only FFT output.
```

---

## Pulse Value Encoding

Each pulse carries one uint32_t value.

The planner must define:

* bit ordering;
* symbol ordering;
* most-significant-bit first versus least-significant-bit first;
* whether the pulse contains exactly 32 symbols;
* whether additional synchronization symbols exist;
* whether guard symbols exist.

The initial fixture encoding should be deterministic and easily decoded.

---

## Waveform Parameters

Decision: use `500 Msps` as the deterministic fixture sample rate.

The planner must document:

```text
sample_rate_hz = 500'000'000
samples_per_symbol
symbol_duration
pulse_width_samples
pulse_gap_samples
pulse_period_samples
```

Relationships:

```text
pulse_width_samples =
    total_symbols × samples_per_symbol

pulse_period_samples =
    pulse_width_samples +
    pulse_gap_samples

symbol_duration_seconds =
    samples_per_symbol / sample_rate_hz
```

Timing constraints:

```text
samples_per_symbol = F_s / R_b

Require:

    F_s % R_b == 0

pulse_width_samples =
    32 * samples_per_symbol

pulse_gap_samples =
    round(pulse_gap_seconds * F_s)

pulse_period_samples =
    pulse_width_samples + pulse_gap_samples
```

Recommended fixture defaults:

```text
R_b = 5'000'000
F_s = 500'000'000
samples_per_symbol = 100
bits_per_pulse = 32
pulse_width_samples = 3200
pulse_gap_us = 6.6
pulse_gap_samples = 3300
pulse_period_samples = 6500
```

Derived fixture timing at 500 Msps:

```text
bit_rate_bps = 5'000'000
symbol_duration_us = 0.2
samples_per_symbol = 100
pulse_width_samples = 3200
pulse_gap_samples = 3300
pulse_period_samples = 6500
```

---

## Synchronization Assumptions

Initial deterministic fixture assumptions:

* sample rate is known;
* hop frequencies are known;
* pulse width is known;
* pulse period is known;
* pulse starts are sample aligned;
* no frequency offset;
* no Doppler;
* no multipath;
* no overlapping pulses;
* no dropped pulses.

These assumptions may be relaxed in later protocol revisions.

---

## Noise

The initial protocol is noise free.

Optional deterministic noise and impairments are future extensions.

Possible future impairments:

* AWGN;
* frequency offset;
* timing offset;
* phase offset;
* pulse amplitude variation;
* dropped pulses;
* overlapping pulses.

All impairments shall be disabled by default.

Decision: the plan must consider Doppler effects and noise, especially when the transmitter is moving relative to the receiver. PR1 may keep Doppler/noise disabled for deterministic CI, but node interfaces, estimator assumptions, and diagnostics should leave a clear path for:

* carrier frequency offset from Doppler;
* time-varying phase drift;
* reduced CPSM estimator confidence under noise;
* SNR-dependent detection thresholds.

---

## Truth Metadata

Every generated pulse shall produce truth metadata:

```cpp
struct FHSSTruthPulse {
    uint64_t start_sample;
    uint64_t duration_samples;

    uint32_t frequency_index;
    double frequency_hz;

    uint32_t value;

    bool is_preamble;
};
```

The truth stream exists solely for validation.

The receiver must recover pulse values from IQ and not from truth metadata.

The CPSM waveform is modeled for PR1 as binary Continuous Phase Modulation using the phase trajectory above. That definition fixes:

* symbol duration,
* samples per symbol,
* sample rate requirements,
* pulse width feasibility,
* and the receiver estimator boundary.

The planner still needs to choose the exact modulation index, rectangular phase-pulse normalization/support, matched filter, and receiver estimator.

Important timing consequence:

```text
6.4 us pulse width * 5 Mbps = 32 bits
```

This matches the one-pulse-equals-one-`uint32_t` invariant because CPSM carries one bit per symbol. The selected 500 Msps fixture rate divides evenly into 5 Mbps:

```text
500 Msps / 5 Mbps = 100 samples per bit
6.4 us * 500 Msps = 3200 samples
6.6 us * 500 Msps = 3300 samples
13.0 us * 500 Msps = 6500 samples
```

The planner should use these integral sample counts for the first deterministic fixture lane.

---

# Architectural Conclusions

Use a modem-style receiver architecture.

Do not force the architecture through generic PDWs.

Do not make generic `PDW` the mandatory intermediate.

Keep these concerns separate:

* pulse detection
* pulse candidate formation
* pulse-word decoding
* preamble detection
* message assembly

Support a future optional PDW diagnostic lane, but PDW must not be the canonical decoder input unless justified.

---

# Inputs

Current repository state.

Existing GraphX graph builder, executor, plugin, config, and test patterns.

Existing DSP nodes and demos:

* `libdsp/include/dsp/SineSignalNode.hpp`
* `libdsp/include/dsp/CpuSpectrumDftNode.hpp`
* `libdsp/include/dsp/SpectrumSinkNode.hpp`
* `libdsp/config/dsp_sine_fft_spectrum_256.json`
* `examples/DSP/src/main.cpp`
* `examples/DSP/test/test_dsp_spectrum_demo.cpp`

Existing CPU-versus-Metal DSP timing/report work only as structural reference and not as a requirement.

Although the role file is SAR-named, this task is DSP/FHSS and must not inherit SAR-specific assumptions.

---

# Current Repository Snapshot

The planner must account for the current GraphX DSP state:

* `SineSignalNode<N>` emits `ControlToken<Message>` with an `IqPacket<float, N>` sidecar.
* `CpuSpectrumDftNode<float, N>` consumes token-wrapped IQ sidecars and emits token-wrapped `MagnitudePacket<float, N>` sidecars.
* The existing CPU spectrum lane is direct DFT, despite some legacy `fft` names in paths/config text.
* `MagnitudePacket` intentionally collapses complex IQ to magnitudes and is therefore insufficient for pulse-word recovery.
* DSP plugin coverage exists for sine, CPU spectrum DFT, Metal DFT transfer lanes, and spectrum sinks, but there are no FHSS-specific packet types, nodes, plugins, configs, fixtures, or tests yet.
* Existing GraphX runtime/plugin/config/test conventions should be reused rather than redesigned.

Use `CpuSpectrumDftNode` in new planning text unless discussing legacy names or future FFT work.

---

# Planning Requirements

Do not implement code.

Do not add GPU or Metal work in the first decoder plan.

Do not redesign GraphX runtime contracts.

Do not require real RF captures or external datasets.

Keep tests deterministic and CI-safe.

Use existing GraphX JSON config, plugin loading, graph builder, executor, and test conventions.

Prefer CPU-only first.

Do not claim production RF performance.

Do not force the architecture through generic PDWs.

Each PR must compile and test independently.

---

# Required Type Model

Plan types similar to:

```cpp
struct FHSSPulse {
    uint64_t start_sample;
    uint64_t duration_samples;

    uint32_t frequency_index;
    double frequency_hz;

    uint32_t value;
    bool is_preamble;
    double confidence;
};

struct FHSSTruthPulse {
    uint64_t start_sample;
    uint64_t duration_samples;

    uint32_t frequency_index;
    double frequency_hz;

    uint32_t value;
    bool is_preamble;
};

struct FHSSMessage {
    std::vector<double> available_frequencies_hz;
    std::vector<FHSSPulse> preamble_pulses;
    std::vector<FHSSPulse> payload_pulses;
};

struct FHSSFrequencyMapEntry {
    uint32_t frequency_index;
    double rf_frequency_hz;
    double iq_offset_frequency_hz;
    uint32_t channel_id;
};

struct FHSSDetectedPulse {
    uint64_t global_start_sample;
    uint64_t global_end_sample;
    uint64_t duration_samples;

    uint64_t channel_start_sample;
    uint32_t channel_id;

    uint32_t frequency_index;
    double rf_frequency_hz;
    double iq_offset_frequency_hz;
    double estimated_center_frequency_hz;
    double frequency_error_hz;

    double amplitude;
    double power_db;
    double snr_db;
    double noise_floor_db;

    double phase_at_start_rad;
    double phase_slope_rad_per_sample;
    double cfo_hz;

    double bandwidth_hz;
    double confidence;

    uint64_t detector_id;
    uint64_t packet_sequence;

    uint64_t provisional_slot_index;
    std::optional<uint64_t> slot_index;
};

struct FHSSProtocolConfig {
    double base_frequency_hz;
    double frequency_spacing_hz;
    uint32_t frequency_count;

    uint32_t preamble_pulse_count;
    uint32_t active_hop_frequency_count;
    uint32_t max_message_pulses;

    std::vector<uint32_t> active_frequency_indices;
    std::vector<FHSSFrequencyMapEntry> frequency_map;
    std::vector<uint32_t> preamble_hop_pattern;
    std::vector<uint32_t> preamble_words;

    double pulse_width_us;
    double pulse_dead_time_us;
    double pulse_period_us;

    std::string modulation_name;
    double bit_rate_bps;
    uint32_t bits_per_pulse;
};

struct FHSSDecodeConfig {
    FHSSProtocolConfig protocol;

    std::vector<double> available_frequencies_hz;
    std::vector<uint32_t> active_frequency_indices;

    double frequency_tolerance_hz;

    double sample_rate_hz;
    uint64_t nominal_pulse_width_samples;
    uint64_t nominal_pulse_gap_samples;
    uint64_t nominal_pulse_period_samples;
    uint64_t pulse_width_tolerance_samples;

    std::vector<uint32_t> preamble_words;
    std::vector<uint32_t> preamble_hop_pattern;
};
```

The planner may collapse or rename these structs during implementation, but the roadmap must preserve the protocol fields and validation rules.

Detected pulses from per-frequency or per-channel detectors must carry global sample timing metadata. Channel-local sample offsets are insufficient for cross-channel pulse association.

Use:

```text
global_start_sample =
    input_packet_global_start_sample + local_start_offset

global_end_sample =
    global_start_sample + duration_samples
```

Important fields for association and message assembly:

* `global_start_sample`
* `duration_samples`
* `frequency_index`
* active `channel_id`
* `snr_db` / `confidence`
* `frequency_error_hz`
* `phase_at_start_rad`, `phase_slope_rad_per_sample`, and `cfo_hz`
* provisional or final pulse slot index

Protocol validation should reject:

* a frequency count other than 128 for the first fixture lane
* preamble hop patterns that are not exactly 16 pulses
* active hop sets with anything other than 4 distinct frequency indices
* active frequency indices outside `[0, 127]`
* preamble hop entries outside the active frequency set
* payload pulse frequencies outside the active frequency set
* messages longer than 256 pulses
* timing/sample-rate combinations that deviate from the selected 500 Msps deterministic fixture unless explicitly justified
* duplicate or ambiguous preamble definitions unless explicitly supported

---

# Decoder Intermediate Decision

Planner must decide whether the first decoder intermediate should be:

```cpp
struct FHSSFrequencyObservation
```

or

```cpp
struct FHSSPulseCandidate
```

Prefer `FHSSPulseCandidate` if it preserves:

* timing
* duration
* frequency
* SNR
* confidence
* pulse-value evidence

The planner should explain the decision.

Suggested model:

```cpp
struct FHSSPulseCandidate {
    uint64_t global_start_sample;
    uint64_t global_end_sample;
    uint64_t start_sample;
    uint64_t duration_samples;
    uint64_t provisional_slot_index;
    std::optional<uint64_t> slot_index;

    uint32_t frequency_index;
    double frequency_hz;
    double frequency_error_hz;
    double cfo_hz;

    std::vector<std::complex<float>> baseband_samples;

    double snr_db;
    double confidence;
};
```

The planner may refine this model.

---

# Complex IQ Requirement

The planner must distinguish between:

### Spectral hop detection

DFT/spectrum magnitude or channelizer energy is acceptable.

### Pulse value recovery

Complex IQ or complex channelized samples are required.

Do not rely on magnitude-only spectrum packets to recover the pulse value.

Preserve phase information needed for word decoding.

---

# Required Target Pipeline

Plan a CPU-first receiver similar to:

```text
FHSSSyntheticIqSourceNode
    ->
optional conditioning/windowing or framing adapter
    ->
ChannelizerNode
    ->
PerChannelPulseDetectorNode[]
    ->
FHSSPulseMergeNode
    ->
FHSSPulseCandidateNode
    ->
CPSMBranchMetricNode
    ->
CPSMViterbiDecoderNode
    ->
FHSSPulseWordDecoderNode
    ->
FHSSPreambleDetectorNode
    ->
FHSSMessageAssemblerNode
    ->
FHSSMessageSinkNode
```

If existing DSP node contracts make this awkward, stop and provide an architectural analysis to allow the user to make a decision about next steps. One of those steps may be to update the DSP node contract rather than creating an adapter node. GraphX nodes are evolving.

DFT or channelizer may be implementation details inside hop detection and do not necessarily need to appear as separate visible nodes.

For PR1 without a real channelizer, use the same metadata model with an easier implementation:

```text
IQ stream
    ->
CorrelatorBankDetectorNode
    ->
FHSSPulseMergeNode
    ->
FHSSPulseCandidateNode
    ->
CPSMBranchMetricNode
    ->
CPSMViterbiDecoderNode
    ->
FHSSPulseWordDecoderNode
    ->
FHSSPreambleDetectorNode
    ->
FHSSMessageAssemblerNode
    ->
FHSSMessageSinkNode
```

Conceptual channelizer topology:

```text
IQ stream
  -> ChannelizerNode
      -> Channel[0] -> PerChannelPulseDetectorNode
      -> Channel[1] -> PerChannelPulseDetectorNode
      -> Channel[2] -> PerChannelPulseDetectorNode
      -> Channel[3] -> PerChannelPulseDetectorNode
  -> FHSSPulseMergeNode
  -> CPSM/Viterbi Decoder
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
```

---

# Node Mathematical Specification

The planner report must provide equations for every node in the first CPU fixture pipeline. The following equations are the required mathematical baseline unless the planner explicitly replaces them with a more precise but equivalent formulation.

## Shared Definitions

```text
F_s = 500'000'000 samples/sec
R_b = 5'000'000 bits/sec
T_s = 1 / F_s
T_b = 1 / R_b = 200 ns
M = F_s / R_b = 100 samples/symbol
K = 32 symbols/pulse
N_pulse = K * M = 3200 samples
N_gap = 3300 samples
N_period = N_pulse + N_gap = 6500 samples
h = 1/2 initially recommended
A = 1.0 initially recommended
```

Frequency table:

```text
f_abs[i] = 1'000'000'000 + 4'000'000 * i,  i in [0, 127]
```

For GraphX fixture tests, use baseband or offset frequencies:

```text
f_i = frequency_offset_hz[i]
```

The absolute RF table remains protocol metadata. The fixture IQ shall not directly sample the 1 GHz absolute carrier at `F_s = 500 Msps` because that aliases. A config that uses absolute RF frequencies as sampled complex IQ frequencies must be rejected unless it explicitly models downconversion/aliasing.

Bit and symbol mapping for pulse `p`:

```text
b[p,k] = bit k of value[p], MSB first unless overridden
a[p,k] = +1 if b[p,k] = 0
a[p,k] = -1 if b[p,k] = 1
```

For payload/body pulse `p`, choose frequency index from the four active preamble frequencies using deterministic random selection:

```text
active_set = unique({H_pre[k] | k in [0, 15]})
require |active_set| == 4

r[p] = DeterministicRng(seed).next_uint() mod 4
i[p] = active_set[r[p]]
```

For preamble pulse `p < 16`:

```text
i[p] = H_pre[p]
```

Recommended full-response rectangular CPM frequency pulse and corresponding phase response:

```text
g(t) =
    0,                 t < 0
    1 / (2*T_b),       0 <= t < T_b
    0,                 t >= T_b

q(t) =
    0,                 t < 0
    t / (2*T_b),       0 <= t < T_b
    1/2,               t >= T_b
```

Here "rectangular" means rectangular frequency pulse `g(t)`, whose integral is the ramp-and-hold phase response `q(t)`. The planner may choose another `q(t)`, but it must preserve constant envelope and continuous phase and update the receiver estimator accordingly.

## FHSSSyntheticIqSourceNode

For pulse `p`:

```text
n0[p] = start_sample[p]
i[p] = frequency_index[p]
f_p = f_i[p]
tau_p[n] = (n - n0[p]) / F_s
```

Pulse envelope:

```text
e_p[n] =
    1,  0 <= n - n0[p] < N_pulse
    0,  otherwise
```

CPSM phase trajectory with continuity inside one pulse and reset across pulses:

```text
theta_p[n] =
    2*pi*h * sum_{k=0}^{K-1} a[p,k] * q(tau_p[n] - k*T_b)
```

Complex baseband pulse:

```text
s_p[n] = A * e_p[n] * exp(j * theta_p[n])
```

Hopped complex IQ fixture signal:

```text
x[n] = sum_p s_p[n] * exp(j * 2*pi * f_p * n / F_s)
```

For the first deterministic fixture, pulses do not overlap, so at most one `s_p[n]` is nonzero at any `n`.

Truth output:

```text
truth[p] =
    {start_sample = n0[p],
     duration_samples = N_pulse,
     frequency_index = i[p],
     frequency_hz = f_abs[i[p]],
     value = value[p],
     is_preamble = (p < 16)}
```

## Conditioning / Framing Adapter

If a framing adapter is used, it must preserve complex samples and sample indices:

```text
frame_m[r] = x[n_frame[m] + r],  r in [0, frame_length - 1]
```

If a window is used for detection diagnostics, it is diagnostic only:

```text
x_w[n] = w[n] * x[n]
```

Windowed magnitude outputs must not be the canonical input to word decoding.

## PerChannelPulseDetectorNode / CorrelatorBankDetectorNode

For each candidate frequency `i` in the active set derived from the preamble pattern, mix down:

```text
y_i[n] = x[n] * exp(-j * 2*pi * f_i * n / F_s)
```

Do not use raw mixed-down energy to choose the hop for CPSM:

```text
sum_r |y_i[n0 + r]|^2
```

is independent of candidate frequency for a constant-envelope waveform. Hop detection must use a phase-coherence, spectral, channelizer, or CPM likelihood metric.

Decision: PR1 uses a known message start.

```text
message_start_sample = 0
n0[p] = message_start_sample + p * N_period
```

The detector should emit candidate timing/frequency metadata and must not duplicate the full CPSM branch metric / Viterbi work unless it can pass reusable likelihood state downstream. The default PR1 responsibility split is:

```text
PerChannelPulseDetectorNode / CorrelatorBankDetectorNode:
    timing, channel, frequency, amplitude/SNR/confidence metadata

CPSMBranchMetricNode:
    per-symbol/per-state branch metrics from complex candidate samples

CPSMViterbiDecoderNode:
    MLSE/Viterbi path selection
```

For each candidate active frequency `i` and slot start `n0`, the detector may compute a lightweight coherent score for timing/frequency ranking:

```text
y_i,r = y_i[n0 + r],  r in [0, N_pulse - 1]
coherent_score_i[n0] =
    detector-defined phase/coherence/channelizer score using complex y_i,r
```

The detector score must be sufficient to rank the four active frequencies and find/validate slot starts, but full sequence evidence belongs to `CPSMBranchMetricNode` and `CPSMViterbiDecoderNode` by default. If the detector computes reusable CPM likelihood state, the planner must define the handoff contract and prove it avoids duplicated work.

Estimated hop:

```text
i_hat[p] = argmax_i coherent_score_i[n0[p]]
f_hat[p] = f_abs[i_hat[p]]
```

Candidate confidence may be:

```text
confidence_freq =
    (score_best - score_second_best) / max(abs(score_best), epsilon)
```

For a channelized topology, each detector reports local and global timing:

```text
global_start_sample =
    input_packet_global_start_sample + local_start_offset

global_end_sample =
    global_start_sample + duration_samples

channel_start_sample = local_start_offset
channel_id = detector channel id
```

Before preamble lock or message epoch resolution, compute:

```text
provisional_slot_index =
    global_start_sample / N_period
```

If a message epoch is known later:

```text
slot_index =
    (global_start_sample - message_epoch_sample) / N_period
```

The output `FHSSDetectedPulse` must include:

```text
global_start_sample
global_end_sample
duration_samples = N_pulse
channel_start_sample
channel_id
frequency_index = i_hat[p]
rf_frequency_hz = f_abs[i_hat[p]]
iq_offset_frequency_hz = f_i_hat
estimated_center_frequency_hz
frequency_error_hz
amplitude
power_db
snr_db or likelihood_ratio diagnostic
noise_floor_db
phase_at_start_rad
phase_slope_rad_per_sample
cfo_hz
bandwidth_hz
confidence = confidence_freq
detector_id
packet_sequence
provisional_slot_index
```

The detector does not need to decode or validate the preamble. It may emit pulse candidates and decoded pulse evidence for the message decoder/preamble detector to interpret. Overlapped messages are possible future/advanced cases; PR1 shall reject and report overlap as unsupported.

## FHSSPulseMergeNode

Responsibilities:

```text
consume detected pulses from multiple channel detectors
convert channel-local sample offsets to global sample offsets
sort by global_start_sample
reject duplicate detections
resolve collisions
emit ordered FHSSPulseCandidate stream
```

Input pulses must already carry global timing. The merge node validates:

```text
global_start_sample =
    input_packet_global_start_sample + channel_start_sample

global_end_sample =
    global_start_sample + duration_samples
```

Ordering:

```text
ordered_pulses = sort(detected_pulses, key = global_start_sample)
```

Duplicate rejection for pulses `u` and `v`:

```text
same_frequency =
    u.frequency_index == v.frequency_index

overlap_samples =
    max(0, min(u.global_end_sample, v.global_end_sample)
           - max(u.global_start_sample, v.global_start_sample))

duplicate =
    same_frequency &&
    overlap_samples / min(u.duration_samples, v.duration_samples)
        >= duplicate_overlap_threshold
```

When duplicates are detected, keep the pulse with higher confidence or SNR:

```text
keep = argmax_{p in duplicate_group} (p.confidence, p.snr_db)
```

Collision detection:

```text
collision =
    !same_frequency &&
    overlap_samples > collision_overlap_threshold_samples
```

The PR1 planner must decide whether collisions are rejected, flagged, or emitted as ambiguous candidate groups.

Candidate stream for CPSM decoding:

```text
FHSSPulseCandidate.global_start_sample = detected.global_start_sample
FHSSPulseCandidate.duration_samples = detected.duration_samples
FHSSPulseCandidate.frequency_index = detected.frequency_index
FHSSPulseCandidate.frequency_hz = detected.rf_frequency_hz
FHSSPulseCandidate.baseband_samples = dehopped complex samples for detected pulse
FHSSPulseCandidate.confidence = detected.confidence
```

The merge node is the boundary that normalizes pulses into one shared sample-time domain before CPSM demodulation and message assembly.

## CPSMBranchMetricNode

Given dehopped candidate samples:

```text
y_p[r] = baseband_samples[r],  r in [0, N_pulse - 1]
t_r = r / F_s
```

For binary CPM, branch metrics are computed from the configured trellis model.

PR1 trellis assumptions:

```text
modulation_index = h = 1/2
phase_pulse = rectangular full-response
initial_phase = 0
terminal_phase = unconstrained unless the planner chooses to check it
state = accumulated CPM phase modulo 2*pi
continuity_scope = inside each pulse only
```

For any candidate symbol sequence `a`, synthesize:

```text
theta_a[r] =
    2*pi*h * sum_{k=0}^{K-1} a[k] * q(t_r - k*T_b)

s_a[r] = exp(j * theta_a[r])
```

Sequence metric:

```text
Lambda(a) = Re{ sum_{r=0}^{N_pulse-1} y_p[r] * conj(s_a[r]) }
```

The branch metric node must expose per-state/per-symbol metrics suitable for the selected low-state MLSE/Viterbi implementation. It must not require brute force over all `2^32` pulse words.

## CPSMViterbiDecoderNode

Decoded symbol sequence:

```text
a_hat = argmax_{a in {-1,+1}^K} Lambda(a)
```

The planner should implement the estimator efficiently. For full brute force, `2^32` sequences is not acceptable. With `h = 1/2` and rectangular full-response CPM, the planner must derive a low-state Viterbi/MLSE or equivalent estimator and prove it is equivalent for the deterministic fixture.

## FHSSPulseWordDecoderNode

The pulse word decoder maps the Viterbi/MLSE symbol decisions to bits and then to one `uint32_t`.

Symbol-to-bit mapping:

```text
bit_hat[k] =
    0, if a_hat[k] = +1
    1, if a_hat[k] = -1
```

Word decision:

```text
value_hat = 0
for k in 0..31:
    value_hat = (value_hat << 1) | bit_hat[k]
```

Confidence may be based on metric separation:

```text
pulse_confidence =
    (Lambda_best - Lambda_second_best) / max(abs(Lambda_best), epsilon)
```

If a reduced estimator is used, it must expose an analogous confidence metric.

## FHSSPreambleDetectorNode

Given decoded pulses `d[p]` and configured preamble hop pattern `H_pre[k]`:

```text
match_hop[p,k] = (d[p+k].frequency_index == H_pre[k])
```

Hop-only preamble score:

```text
S_pre[p] =
    sum_{k=0}^{15} I(match_hop[p,k])
```

Decision: preamble detection is hop-only. Preamble word values are not required for preamble lock and must not contribute to `S_pre`.

However, identical preamble frequencies shall have identical word values in generated truth fixtures. This is a fixture consistency and secondary validation rule, not the primary preamble detector criterion.

Preamble lock:

```text
lock_start = p where S_pre[p] == S_required
```

The active set after lock is:

```text
active_set = unique({H_pre[k] | k in [0,15]})
require |active_set| == 4
```

## FHSSMessageAssemblerNode

For locked preamble at pulse index `p0`:

```text
message.preamble = d[p0 : p0 + 16)
message.payload = d[p0 + 16 : p_end)
```

Payload validity:

```text
for every payload pulse d[p]:
    require d[p].frequency_index in active_set
```

Message length:

```text
message_pulse_count = len(message.preamble) + len(message.payload)
require message_pulse_count <= 256
```

Message confidence:

```text
message_confidence =
    mean({d[p].confidence | p in message})
```

## FHSSMessageSinkNode / Truth Comparator

For truth pulse `t[p]` and decoded pulse `d[p]`:

```text
match_start[p] = |d[p].start_sample - t[p].start_sample| <= start_tolerance_samples
match_duration[p] = |d[p].duration_samples - t[p].duration_samples| <= duration_tolerance_samples
match_frequency[p] = d[p].frequency_index == t[p].frequency_index
match_value[p] = d[p].value == t[p].value
```

Pulse match:

```text
match_pulse[p] =
    match_start[p] &&
    match_duration[p] &&
    match_frequency[p] &&
    match_value[p]
```

End-to-end success:

```text
success =
    decoded_message_count == expected_message_count &&
    all_p match_pulse[p]
```

The sink/report must include mismatched pulse indices, expected/actual fields, and confidence diagnostics.

Minimum PR1 diagnostics, even though the final schema may be deferred:

```text
pulse_count
rejected_count
global_start_sample
frequency_index
confidence
viterbi_path_metric
decoded_value
preamble_lock
truth_mismatch_count
```

---

# Plan Correctness Validation

The planner must validate these correctness points before emitting a roadmap:

1. Timing consistency:
   `500 Msps / 5 Mbps = 100 samples/symbol`, `32 * 100 = 3200` pulse samples, `6.6 us * 500 Msps = 3300` gap samples, and total period is `6500` samples.
2. Frequency correctness:
   The protocol’s absolute 1 GHz hop table cannot be directly sampled as RF at `500 Msps` without aliasing. The fixture must use complex baseband/offset hop frequencies, while preserving the absolute table as metadata.
3. CPSM correctness:
   The selected `q(t)` must make `theta(t)` continuous. If rectangular full-response CPM is used, define `q(t)` exactly as above or replace it consistently in generator and decoder.
4. Decoder tractability:
   The plan must not require brute-force search over `2^32` symbol sequences. It must derive a low-state MLSE/Viterbi or equivalent deterministic estimator for the chosen phase pulse.
5. Magnitude-only boundary:
   `CpuSpectrumDftNode`/`MagnitudePacket` may support diagnostics or hop-energy detection, but cannot feed word decoding because CPSM decoding requires complex phase evidence.
6. Message protocol:
   The 16-pulse preamble pattern must resolve exactly four active frequencies; preamble detection is hop-only; identical preamble frequencies must have identical word values in generated truth fixtures; all payload/body pulses must use only that active set; total message length includes the 16 preamble pulses and must be at most 256 pulses.
7. Node split:
   The efficient implementation may combine hop candidate extraction and CPSM sequence estimation internally, but the plan must preserve separate conceptual outputs for candidate evidence, decoded pulse words, preamble lock, assembled message, and truth comparison.
8. Impairment boundary:
   PR1 may disable noise and Doppler for deterministic CI, but the plan must identify how Doppler/frequency offset and noise would affect hop detection, CPSM estimation, confidence, and diagnostics.
9. Overlap boundary:
   Overlapped messages are possible. PR1 rejects and reports overlap as unsupported.
10. Global timing boundary:
   Detected pulses from all channels must be normalized to one global sample-time domain before ordering, CPSM decoding, preamble detection, or message assembly. Channel-local offsets alone are invalid for association.
11. Bandwidth/channelizer boundary:
   A 5 Mbps CPSM signal on 4 MHz-spaced hop channels may not be cleanly separable depending on `q(t)` and filtering. The planner must require spectral-occupancy validation and a channelizer filter-width decision before claiming channelizer separation.

---

# Required Signal Generator Planning

Plan a deterministic synthetic IQ generator.

Inputs:

* available hop frequencies
* active hop frequency indices
* 16-pulse preamble hop pattern
* sample rate
* pulse width
* pulse spacing
* ordered preamble truth pulses
* ordered payload truth pulses
* deterministic RNG seed for message-body frequency selection

The generator shall:

* generate complex IQ
* generate one pulse per `uint32_t`
* enforce the 128-frequency table and 4-frequency active hop set
* enforce 16 preamble pulses
* enforce maximum message length
* choose payload/body pulse frequencies randomly from the four active preamble frequencies using a deterministic seed
* enforce that identical preamble frequencies have identical preamble word values
* emit exact truth metadata
* support later optional noise

Noise must be disabled by default.

---

# Required Fixture Encoding

Planner must explicitly choose and document the initial pulse-value encoding.

The encoding is a deterministic fixture and not a production protocol claim.

Initial encoding must be binary CPSM:

* pulse carrier frequency chosen from hop table
* pulse divided into 32 equal CPSM symbols
* each symbol represents one bit at 5 Mbps
* 32 bits recover exactly one `uint32_t`
* CPSM phase trajectory follows `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`
* modulation index, phase pulse, matched filter, and estimator selected before implementation

Pulse-word recovery shall be performed from complex IQ evidence.

---

# Required Tests

Plan deterministic tests for:

### Generator

* synthetic IQ generation from truth pulses

### Detection

* hop frequency recovery
* frequency index recovery
* pulse start recovery
* pulse duration recovery
* global sample timing recovery from packet global start plus local detector offset
* provisional slot-index assignment before preamble/message epoch lock
* duplicate detection and collision handling through `FHSSPulseMergeNode`
* PR1 overlap rejection/reporting

### Decoder

* exact `uint32_t` pulse value recovery
* branch metric generation
* Viterbi/MLSE path decoding

### Message Layer

* preamble detection
* hop-only preamble lock
* preamble word secondary consistency validation
* payload assembly
* enforcement that only the 4 active preamble frequencies appear after preamble lock
* enforcement of the 256-pulse message limit
* ordering of merged candidates by `global_start_sample`

### Graph Integration

* end-to-end GraphExecutor execution

### Truth Comparison

Compare:

```cpp
FHSSTruthPulse[]
```

against

```cpp
DecodedFHSSPulse[]
```

including:

* start_sample
* duration_samples
* frequency_index
* value

### Protocol Validation

* generated 128-entry frequency table starts at 1 GHz and advances by 4 MHz
* active hopping pattern contains exactly 4 distinct indices
* preamble contains exactly 16 pulses
* payload rejects frequencies outside the active set
* 6.4 us pulse width at 5 Mbps maps to exactly 32 data bits
* 500 Msps yields deterministic pulse/symbol/gap sample counts
* max-message-length handling is deterministic and documented

---

# Negative Tests

Plan tests for:

* unknown hop frequency
* pulse width outside tolerance
* missing preamble
* corrupted pulse value
* empty input
* overlapping pulses if unsupported
* overlapped messages rejected as unsupported in PR1
* duplicate pulse detections from multiple channels
* channel-local pulses without valid global timing metadata
* preamble pattern with more or fewer than 4 active frequencies
* preamble with more or fewer than 16 pulses
* payload pulse on a frequency outside the active preamble set
* message longer than 256 pulses
* frequency index outside the 128-entry hop table
* invalid timing configuration that does not match the selected 500 Msps fixture lane

---

# Documentation

Plan documentation covering:

* one pulse equals one `uint32_t`
* FHSS pulse and message model
* 128-frequency protocol table
* 16-pulse preamble and 4-frequency active hop rule
* 256-pulse maximum message rule
* 6.4 us pulse width, 6.6 us dead time, and 5 Mbps modulation assumptions
* CPSM spectral occupancy and channelizer filter-width assumptions
* any chosen deviation from the 500 Msps fixture assumption
* synthetic fixture encoding
* CPU-only status
* why generic PDW is not the canonical intermediate
* preservation of complex IQ information
* future optional PDW diagnostic lane
* future Metal acceleration boundary

---
Additional theory/code references and required decisions:

- Treat the first fixture waveform as a coherent burst modem problem, not a generic PDW problem.
- Initial encoding shall be binary CPSM:
  - one pulse = one uint32_t
  - one pulse contains 32 equal-duration CPSM symbols
  - each symbol carries one bit at 5 Mbps
  - CPSM phase trajectory follows `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`
  - modulation index, phase pulse shape, matched filter, and receiver estimator must be selected
  - bit ordering must be explicitly chosen, preferably MSB-first
- Initial receiver may assume:
  - sample-rate is known
  - hop frequency set is known
  - pulse width is known
  - slot spacing is known
  - pulse starts are sample-aligned within tolerance
- Initial receiver should avoid claiming general RF synchronization.
- Pulse-value decoding must consume complex IQ or complex channelized/baseband samples.
- Magnitude-only DFT/spectrum output may be used for hop detection or diagnostics, but not as the only input to pulse-value decoding.
- Hop detection starts with the four active frequencies derived from the preamble pattern. The detector does not need to decode or validate the preamble; preamble interpretation belongs to the message decoder/preamble detector.
- Prefer a CPU matched-filter/correlator or CPSM likelihood bank over the four active frequencies for the deterministic first lane because it preserves complex evidence for CPSM symbol estimation.
- Existing GraphX DSP code reference:
  - `CpuSpectrumDftNode` extracts IQ from token sidecar and emits magnitude sidecar while preserving token fields.
  - Therefore FHSS nodes should follow the same token/sidecar style but should not collapse complex IQ into magnitude before word decoding.
- Theory references:
  - CPM/CPSM modem theory: information encoded in a continuous phase trajectory.
  - Matched filtering/correlation: use known symbol timing and hop frequencies to estimate CPSM symbols or sequence likelihood.
  - Channelizer/PFB theory: future scalable implementation for many hop frequencies.
  - Future work may replace the first CPU correlator with FFT/channelizer or Metal acceleration, but the PR roadmap must keep this out of the first implementation lane.
  
---

# Required Gap Assessment

The planner report must identify and categorize architectural and technical gaps before the PR sequence.

At minimum, cover these gaps:

## Architectural Gaps

* No canonical FHSS domain model exists yet for truth pulses, decoded pulses, pulse candidates, messages, preamble state, or decode configuration.
* No explicit `uint32_t`-per-pulse modem contract exists in the repo; the plan must prevent accidental byte/nibble/symbol modeling.
* Existing DSP spectrum packets are magnitude-only and cannot be the canonical input to CPSM, PSK, or other phase-based word decoding.
* No complex intermediate exists for channelized/baseband pulse evidence; the first FHSS lane needs either `FHSSPulseCandidate` with complex samples or an equivalent complex evidence packet.
* No message-layer state machine exists for preamble detection, payload assembly, missing/corrupt pulse handling, or truth comparison.
* No pulse merge/association boundary exists yet for normalizing per-channel detections into a shared global sample-time stream.
* No protocol-validation layer exists for the 128-entry frequency table, 4-frequency active hop subset, 16-pulse preamble, or 256-pulse message limit.
* The exact CPSM modulation index, rectangular phase-pulse normalization/support, matched filter, and receiver estimator are not selected yet.
* No FHSS-specific diagnostics schema exists for reporting detected pulse count, rejected candidates, SNR/confidence, recovered words, preamble lock, or assembly status.
* No clear future boundary exists yet between modem decoding and optional PDW/ESM diagnostics; the plan must keep PDW optional and non-canonical.

## Technical Gaps

* No deterministic FHSS synthetic IQ source exists.
* No fixture schema exists for hop tables, pulse timing, `uint32_t` values, preamble words, payload words, and expected decoded messages.
* No helper exists to derive the 128-hop frequency table from base frequency and spacing while preserving integer-index validation.
* No timing helper exists to derive and validate 500 Msps fixture counts for 5 Mbps symbols, 6.4 us pulses, 6.6 us dead time, and 13 us pulse periods.
* No CPU correlator/matched-filter bank exists for known hop frequencies and symbol timing.
* No CPSM spectral occupancy check or channelizer filter-width decision exists for 4 MHz hop spacing.
* No CPSM branch metric node or Viterbi/MLSE decoder exists.
* No `FHSSPulseMergeNode` exists for sorting, duplicate rejection, collision handling, and global sample-time association.
* No CPSM decoder exists for recovering 32 bits from complex pulse samples.
* No tolerance policy exists for pulse start, duration, frequency, SNR/confidence, or bit/word decisions.
* No FHSS GraphX plugins, JSON configs, CMake wiring, or end-to-end executor tests exist.
* No negative-test fixtures exist for unknown hops, bad widths, missing preambles, corrupted words, empty input, or unsupported overlapping pulses.
* Current DSP spectrum config names still include legacy `fft` wording even though the maintained CPU node is `CpuSpectrumDftNode`; new FHSS planning must avoid adding more truth-in-labeling drift.
* Existing `SineSignalNode` is a tone source, not a burst/message source; it should be treated as a structural reference only.
* Existing Metal DSP work is direct DFT/magnitude-oriented and should stay out of the first FHSS decoder implementation.

## Decision Gaps

The planner must make explicit decisions for:

* `FHSSPulseCandidate` versus lower-level `FHSSFrequencyObservation` as the first receiver intermediate.
* MSB-first versus LSB-first bit ordering inside a 32-symbol pulse.
* Exact rectangular phase-pulse normalization/support and matched filter details for the decided CPSM model.
* Whether PR1 introduces only header/model/test fixtures or also a small pure function generator.
* Whether hop detection and word decoding live in one initial node or two separate nodes; choose the most efficient architecture and provide the analysis supporting that choice.
* Whether PR1 supports a real channelizer topology or implements the same global-timing metadata through a simpler correlator-bank detector.
* CPSM spectral occupancy and channelizer filter-width assumptions for 4 MHz spacing.
* How Doppler/noise considerations affect future estimator thresholds, confidence, and diagnostics while staying disabled by default in deterministic CI.
* The minimum plugin/config surface needed for a runnable graph without overfitting to a single fixture.
* How much of the final diagnostics/report schema can be deferred until node contracts are better defined.

---

# Key Decisions Still Needed

The planner report must surface these decisions near the top and mark each as decided, assumed for PR1, or still open.

Decided:

1. Sample-rate strategy: use `500 Msps` for the deterministic fixture lane.
2. Modulation scope: implement pure binary CPSM using `theta(t)=2*pi*h*sum_k a[k]*q(t-k*T_b)`.
3. Payload/body frequency rule: message-body pulses may use only the four selected preamble frequencies; no additional hop behavior is defined for the body.
4. Node split criterion: choose the most efficient architecture for hop detection and word decoding, and include the architectural analysis supporting the choice.
5. Preamble detection: hop-only. Identical preamble frequencies shall have identical word values in generated truth fixtures.
6. Message length: the 256-pulse maximum includes the 16 preamble pulses.
7. Message-body fixture generation: use deterministic random selection from the four selected preamble frequencies.
8. Detector frequency scope: the detector starts with the four active frequencies derived from the preamble pattern; it does not need to decode the preamble itself.
9. Active-set discovery: active frequencies are derived from the externally supplied preamble pattern.
10. DSP contract direction: propose a DSP contract update if the current token/sidecar model is awkward for this modem lane.
11. Diagnostics/report schema: defer final schema details until nodes are better defined.
12. PR1 synchronization: use `message_start_sample = 0`; pulse-start acquisition is future work.
13. Overlap policy: reject and report overlapped messages as unsupported in PR1.
14. Preamble words: fixture consistency and secondary validation only; not part of hop-only preamble lock.

Still open:

1. Frequency representation: decide whether indexes are canonical and frequencies are derived, or whether configs store both and validate consistency.
2. Receiver synchronization assumptions beyond PR1: decide when/how to add pulse-start acquisition beyond `message_start_sample = 0`.
3. Doppler/noise policy: decide which Doppler/noise fields belong in PR1 configs and diagnostics even if impairments are disabled by default.
4. Error model: decide how to represent unknown frequency, low confidence, bad word decode, missing preamble, invalid timing configuration, overlap unsupported, Doppler/noise unsupported, and overlength message failures.
5. CPSM estimator details: pin down exact rectangular phase-pulse definition, matched filter, Viterbi/MLSE state model, terminal phase policy, and estimator confidence. Initial recommended values are `h=1/2`, rectangular full-response phase pulse, initial phase `0`, terminal phase unconstrained or checked, state as accumulated CPM phase modulo `2*pi`, 5 Mbps, 500 Msps, 100 samples per symbol, and continuity inside each pulse only.
6. Bandwidth/channelizer feasibility: validate spectral occupancy for 5 Mbps CPSM on 4 MHz-spaced channels and choose channelizer filter width.
7. Detector/decoder metric handoff: decide whether detector emits only timing/frequency candidates or passes reusable likelihood state downstream to avoid duplicate metric computation.

---

# Suggested PR Sequence

1. FHSS gap closure, type model, and deterministic truth fixtures.

2. Synthetic FHSS IQ generator.

3. Hop detection, detected-pulse metadata, and pulse merge/association.

4. CPSM branch metric and Viterbi/MLSE decoder.

5. FHSS pulse-word decoder.

6. Preamble detector and message assembler.

7. End-to-end GraphX JSON config and executor tests.

8. Documentation and truth-in-labeling guardrails.

9. Future/out-of-scope:

   * Metal acceleration
   * channelizer acceleration
   * optional PDW diagnostics lane

---

# For Each Planned PR Provide

* title
* purpose
* files to touch
* files to delete
* tests to add
* tests to delete
* acceptance criteria
* risks
* rollback plan
* whether it is CI-safe or local-only

---

# Output

Save the planner report to:

```text
plan/roadmap/DSP_FHSS_DECODER_PR_ROADMAP.md
```
