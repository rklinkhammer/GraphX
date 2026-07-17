# FHSS Architecture

This document describes the current GraphX FHSS implementation as of the live
`libdsp`, `libaccelgraph`, and `examples/DSP` code. The implementation is a
deterministic FHSS CPSM fixture and decoder lane. It is useful for GraphX packet
contracts, node integration, scheduler behavior, diagnostics, and accelerator
front-end validation. It is not yet a production RF receiver.

## Scope

The canonical implementation is the channelized fixture graph:

```text
libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
```

The graph role is:

```json
{
  "fhss_graph_role": "canonical_channelized_fixture",
  "canonical_fhss_graph": true,
  "reference_only": false
}
```

The canonical runtime lane is:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> FHSSFixtureFrequencyChannelizerNode
  -> PerChannelPulseDetectorNode[64]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

The generated topology is maintained by:

```text
examples/DSP/tools/generate_fhss_fixture_topology.py
```

## Protocol Model

The protocol constants live in `libdsp/include/dsp/fhss/FHSSProtocol.hpp`.

Current fixture constants:

| Property | Value |
| --- | --- |
| Frequency table size | 64 entries |
| Selectable frequencies | indices 1 through 62 |
| Reserved frequencies | indices 0 and 63 |
| Active frequency count | 4 |
| RF table base | 1 GHz |
| RF spacing | 8 MHz |
| Fixture IQ sample rate | 500 Msps |
| Bit rate | 5 Mbps |
| Samples per symbol | 100 |
| Bits per pulse | 32 |
| Pulse width | 3200 samples |
| Pulse gap | 3300 samples |
| Pulse period | 6500 samples |
| Preamble length | 16 pulses |
| Max message length | 256 pulses including preamble |

The RF table is metadata. Fixture samples are complex baseband/IF IQ at 500
Msps. Each frequency entry carries both:

```cpp
double rf_frequency_hz;
double iq_offset_frequency_hz;
```

`iq_offset_frequency_hz` is what the synthetic generator and channelizer use.
It may be derived from `iq_center_frequency_hz` as:

```text
rf_frequency_hz - iq_center_frequency_hz
```

Validation keeps active IQ offsets finite, distinct modulo sample rate, and
inside Nyquist after occupied-bandwidth and CFO guards.

## Packet Contracts

The public FHSS edge contracts live in
`libdsp/include/dsp/fhss/FHSSPackets.hpp` and are wrapped in accelerator-ready
GraphX control tokens by `libdsp/include/dsp/fhss/FHSSPorts.hpp`.

The edge sequence is:

| Edge | Packet |
| --- | --- |
| Synthetic IQ | `FHSSSyntheticIqOutputPacket` |
| Downconverted IQ | `FHSSDownconvertedIqPacket` |
| Channelized IQ | `FHSSChannelizedIqPacket` |
| Per-channel pulse evidence | `FHSSPerChannelPulseEvidencePacket` |
| Detected pulse evidence | `FHSSDetectedPulseEvidencePacket` |
| Pulse candidates | `FHSSPulseCandidateEvidencePacket` |
| CPSM branch metrics | `FHSSCpsmBranchMetricPacket` |
| CPSM symbol decisions | `FHSSCpsmSymbolDecisionPacket` |
| Decoded words | `FHSSDecodedPulseWordsPacket` |
| Assembled messages | `FHSSAssembledMessagePacket` |
| Diagnostics | `FHSSDiagnosticsPacket` |

All token aliases use:

```cpp
template <typename PacketT>
using FHSSGraphXToken = graph::gpu::accel::ControlToken<PacketT>;
```

The important architectural boundary is that FHSS semantic metadata stays in
the packet sidecar: timing, frequency, confidence, decode status, and
diagnostics. Current CPU decoding requires host complex IQ through
`FHSSGraphXComplexEvidence` with `HostSharedImmutable` residency. Future
accelerator storage may move samples out of band, but it must preserve the same
semantic sidecar contract.

## Node Responsibilities

### Synthetic IQ Source

`FHSSSyntheticIqSourceNode` wraps the deterministic generator in
`FHSSSyntheticIqGenerator.hpp`.

Responsibilities:

- Validate active frequencies, timing, IQ offsets, message roles, preamble
  consistency, payload frequency membership, and non-overlap.
- Generate zero-filled complex IQ with CPSM pulses inserted at scheduled global
  sample offsets.
- Optionally apply a realistic receive overlay for overlap, stationary receiver
  geometry, moving transmitter paths, propagation delay, path-loss amplitude,
  Doppler, timing jitter, and missing pulses.
- Emit truth-correlated `FHSSSyntheticIqToken` payloads.
- Support whole-schedule production and explicit message injection through
  `IFHSSMessageInjectionSource`.

Unsupported feature flags are rejected for noise and multipath. Doppler is
supported only through the realistic motion model. Overlap is opt-in through
`allow_overlap`.

#### Source configuration shape

The source node receives a `FHSSSyntheticIqGeneratorConfig`, usually parsed
from the source node's `node_config` in the graph JSON. The important source
fields are:

```json
{
  "active_frequency_indices": [24, 28, 32, 36],
  "iq_center_frequency_hz": 1240000000.0,
  "messages": [
    {
      "message_id": 1,
      "transmit_start_sample": 0,
      "pulses": [
        {"frequency_index": 24, "value": 2863311530, "role": "preamble"},
        {"frequency_index": 28, "value": 2004318071, "role": "preamble"},
        {"frequency_index": 24, "value": 16909060, "role": "body"}
      ]
    }
  ],
  "idle_mode": "zero",
  "idle_duration_samples": 0,
  "occupied_bandwidth_hz": 5000000.0,
  "max_abs_cfo_hz": 1000.0,
  "enable_noise": false,
  "enable_doppler": false,
  "enable_multipath": false,
  "allow_overlap": false,
  "realistic": {
    "enabled": false
  }
}
```

The actual canonical config has 16 preamble pulses before body pulses. The
short example above only shows the shape.

`FHSSSyntheticIqGeneratorConfigFromJson` parses this into:

- `decode_config.frequency`: RF table settings plus IQ offsets. When
  `iq_center_frequency_hz` is present, every table offset is derived as
  `RfFrequencyHz(index) - iq_center_frequency_hz`.
- `decode_config.active_frequency_indices`: the four selected active
  frequencies.
- `decode_config.preamble_pulses`: copied from the first message's first 16
  pulses.
- `messages`: the full vector of scheduled messages.
- `idle_duration_samples`: optional zero-output length when there are no
  messages.
- `realistic`: optional realistic receive-model settings. When omitted or
  disabled, the generator behaves like the deterministic fixture.
- feature flags: noise and multipath remain unsupported; Doppler is available
  only through the realistic motion model.

#### Source node production model

`FHSSSyntheticIqSourceNode` is a GraphX source node with one output port. It is
not continuously sampling a device. Each production event dequeues a
`FHSSMessageInjectionRequest` and turns that request into one complete synthetic
IQ packet.

There are two request modes:

| Request kind | Operation |
| --- | --- |
| `WholeSchedule` | Generate the complete `messages[]` schedule from the node config. |
| `ScheduledMessage` | Replace `config.messages` with one injected JSON message and generate only that message. |

For normal graph execution, `Produce()` installs a compatibility
`WholeSchedule` request when the queue is empty. That means a static graph run
with the canonical config emits one token containing IQ for the whole configured
schedule, then marks the token with `EdgeEndOfStream`.

For dashboard or external control, callers use `GetMessageInjectionQueue()` or
`ProduceInjectedMessage()` through `IFHSSMessageInjectionSource`. Injected
messages can carry `FHSSMessageCorrelation` values:

```cpp
struct FHSSMessageCorrelation {
  std::string scenario_id;
  std::uint64_t message_id;
  std::uint64_t release_sequence;
};
```

The source copies that correlation into the output packet sidecar so downstream
diagnostics can relate an emitted waveform back to the UI/control request.

#### Generator validation pipeline

`GenerateSyntheticIqFixture` is the pure helper that turns a config into:

```cpp
struct FHSSSyntheticIqFixture {
  std::vector<std::complex<double>> samples;
  std::vector<FHSSTruthPulse> truth_pulses;
  FHSSTimingModel timing;
};
```

It validates in this order:

1. Reject unsupported feature flags: noise and multipath. Doppler requires the
   realistic model.
2. Require exactly four distinct selectable active frequencies.
3. Derive and validate the timing model: 500 Msps, 5 Mbps, 100 samples per
   symbol, 3200 pulse samples, 3300 gap samples, and 6500 samples per period.
4. Validate the 64-entry RF metadata table and IQ offset guards.
5. Validate every scheduled message:
   - at least 16 pulses;
   - no more than 256 total pulses;
   - first 16 pulses have role `preamble`;
   - later pulses have role `body`;
   - preamble hops are inside the active set;
   - repeated preamble frequencies use the same fixture word value;
   - body frequencies are also inside the active set.
6. Validate the message schedule:
   - message end sample must not overflow;
   - messages must not overlap;
   - overlap is rejected unless `allow_overlap` is true.
7. Build the 64-entry frequency map with RF frequency and IQ offset for each
   frequency index.

If any validation fails, generation returns `std::unexpected` and the source
node emits no token.

#### Message schedule to global sample positions

Each message is placed at its explicit `transmit_start_sample`. Each pulse is
then placed on the deterministic pulse-period grid:

```text
global_start_sample =
  message.transmit_start_sample + pulse_index * 6500
```

The pulse occupies the first 3200 samples of that period. The remaining 3300
samples are left as zero gap samples.

For example, the canonical message has 18 pulses: 16 preamble pulses and 2 body
pulses. A message starting at sample 117000 places its pulses at:

```text
117000, 123500, 130000, ...
```

because each slot advances by 6500 samples.

Before synthesis, the generator computes the total output sample count as the
maximum of `idle_duration_samples` and every scheduled message end:

```text
message_end_sample =
  transmit_start_sample + pulse_count * 6500
```

It pre-sizes the sample vector with complex zeros. That is why gaps, idle time,
and unused channels appear as silence in the fixture IQ.

When `allow_overlap` is true, schedule validation permits message time ranges
to overlap. Pulse synthesis writes into absolute sample positions and adds IQ
samples into the existing vector. If two transmitters arrive at the same sample,
the output is the sum of both complex signals. The generator does not attempt to
fix or label the collision beyond preserving truth metadata; downstream pulse
merge and message construction are responsible for interpreting the result.

#### Truth metadata

For every pulse, the generator appends one `FHSSTruthPulse`:

```cpp
struct FHSSTruthPulse {
  std::uint64_t global_start_sample;
  std::uint64_t nominal_global_start_sample;
  std::uint64_t duration_samples;
  std::uint32_t frequency_index;
  double rf_frequency_hz;
  double iq_offset_frequency_hz;
  double doppler_hz;
  double propagation_delay_seconds;
  double range_m;
  double amplitude;
  bool dropped;
  std::uint32_t value;
  bool is_preamble;
  std::uint64_t message_id;
};
```

This truth vector is not the runtime decoder output. It is fixture evidence for
tests and source bookkeeping. The GraphX output token currently carries the
complex IQ and timing model; downstream nodes re-detect, merge, decode, and
assemble from the IQ evidence.

`nominal_global_start_sample` is the schedule-grid start before realistic
receive effects. `global_start_sample` is the receiver-arrival start after
propagation delay and timing jitter. If a pulse is marked `dropped`, the truth
entry is retained but no samples are written for that pulse.

#### Realistic receive overlay

The realistic overlay is configured under `realistic`:

```json
{
  "allow_overlap": true,
  "enable_doppler": true,
  "realistic": {
    "enabled": true,
    "rng_seed": 7,
    "missing_pulse_probability": 0.1,
    "timing_jitter_stddev_samples": 2.0,
    "apply_propagation_delay": true,
    "apply_path_loss": true,
    "apply_doppler": true,
    "reference_range_m": 1000.0,
    "minimum_range_m": 1.0,
    "receiver": {
      "position_m": {"x": 0.0, "y": 0.0, "z": 0.0}
    },
    "transmitter_paths": [
      {
        "message_id": 1,
        "waypoints": [
          {"time_seconds": 0.0, "position_m": {"x": 1000.0, "y": 0.0, "z": 0.0}},
          {"time_seconds": 0.1, "position_m": {"x": 2000.0, "y": 0.0, "z": 0.0}}
        ]
      }
    ]
  }
}
```

The receiver is stationary. Each transmitter path is associated with a
`message_id` and is linearly interpolated between time-ordered waypoints. If a
message has no matching path, it still transmits on the schedule grid but gets
no range, path-loss, delay, or Doppler adjustment.

For every scheduled pulse in realistic mode:

1. A deterministic RNG decides whether the pulse is dropped using
   `missing_pulse_probability`.
2. The transmitter position is interpolated at the pulse's nominal transmit
   time.
3. Range to the stationary receiver is computed.
4. Propagation delay shifts the receive `global_start_sample` by
   `round(range / c * sample_rate)`.
5. Optional timing jitter adds a deterministic Gaussian sample offset.
6. Optional path loss scales pulse amplitude by
   `reference_range_m / max(range_m, minimum_range_m)`.
7. Optional Doppler estimates radial range rate from nearby path samples and
   shifts the IQ offset by `-(range_rate / c) * rf_frequency_hz`.

This model is intentionally modest: it creates more realistic receiver-arrival
IQ without trying to solve association, synchronization, or missing-pulse
repair inside the source.

#### CPSM sample synthesis

`AppendCpsmPulseSamples` writes exactly 32 symbols per pulse and 100 samples per
symbol, so each pulse contributes 3200 complex samples.

The 32-bit `value` field becomes CPSM symbols MSB-first:

```text
bit 0 -> symbol +1
bit 1 -> symbol -1
```

For symbol `k`, the generator walks all 100 samples. Each complex sample is:

```text
sample = exp(j * (theta + hop_phase))
```

where:

```text
theta =
  2*pi*0.5*(completed_phase_pulse_sum + symbol*q(sample_in_symbol))

q(sample_in_symbol) =
  0.5 * sample_in_symbol / samples_per_symbol

hop_phase =
  2*pi*(iq_offset_frequency_hz + doppler_hz)*global_sample/sample_rate_hz
```

The CPSM part encodes the pulse word. The hop phase places that pulse at its
configured IQ offset frequency, plus optional Doppler. In deterministic mode,
amplitude is fixed at 1.0 and Doppler is 0. In realistic mode, amplitude may be
path-loss scaled. Initial phase is fixed at 0.0.

After each symbol, `completed_phase_pulse_sum` advances by `0.5 * symbol`.
That preserves continuous accumulated phase across the 32 symbols inside a
pulse.

#### Output token construction

After fixture generation, `FHSSSyntheticIqSourceNode::ProduceFromQueue` moves
the sample vector into a shared immutable host payload and creates:

```cpp
FHSSSyntheticIqToken token;
token.token_id = next_token_id_++;
token.sidecar.correlation = request.correlation;
token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(...);
token.sidecar.timing = fixture.timing;
```

The sample time map currently starts at input global sample 0 for the emitted
packet. Later nodes preserve and transform this timing metadata as they
downconvert, channelize, detect, and merge pulses.

### Downconverter

`FHSSDownconverterNode` validates a reference-frame configuration and either
passes IQ through or applies:

```text
output[n] = input[n] * exp(-j 2*pi*translation_frequency_hz*n/sample_rate_hz)
```

The canonical graph configures it as validated passthrough. It is still present
in the canonical topology so the graph has an explicit frequency-reference
boundary.

### Fixture Channelizer

`FHSSFixtureFrequencyChannelizerNode` is a compile-time 64-output
`TypedFixedFanNode`. Each output port corresponds exactly to one frequency
index. There is no aggregate "all channels" packet type.

Responsibilities:

- Validate one receiver channel per frequency table entry.
- Build per-channel metadata from the RF/IQ frequency map.
- Mix each channel by `-iq_offset_frequency_hz`.
- Apply fixture decimation.
- Optionally write channel IQ capture through SigMF helpers.
- Mark indices 0 and 63 as receiver guard or metadata channels.

This is a fixture channelizer, not a production filter bank. The code itself
states that it is frequency mixing and decimation only.

### Per-Channel Detector

`PerChannelPulseDetectorNode` consumes one `FHSSChannelizedIqToken` and emits
one `FHSSPerChannelPulseEvidenceToken`.

Responsibilities:

- Require single-channel metadata where `channel_id == frequency_index`.
- Walk deterministic pulse windows using protocol pulse period.
- Estimate mean power, peak amplitude, SNR, phase slope, and symbol coherence.
- Emit pulse metadata and complex evidence ranges for windows above threshold.

The detector is tuned for the deterministic fixture. It is not an arbitrary
energy detector for unscheduled RF captures.

### Pulse Merge

`FHSSPulseMergeNode` supports two input styles:

- Port 0 for the older detected-pulse aggregate contract.
- Ports 1 through 64 for canonical per-channel detector outputs.

For the canonical path, the merge node waits for all 64 per-channel inputs in
one token batch. It requires a shared token id, accumulates local detections,
normalizes them into the global sample domain, and emits ordered candidates on
output port 1.

Merge semantics live in `FHSSPulseMerge.hpp`:

- Invalid metadata and invalid evidence ranges are rejected.
- Duplicate detections on the same frequency and overlapping time keep the
  higher-confidence or higher-SNR candidate.
- Cross-frequency overlapping pulses are rejected as unsupported.
- Candidates are globally ordered by start sample.
- Provisional and optional final slot indices are derived from pulse period and
  message epoch.

### Candidate Stage

`FHSSPulseCandidateNode` currently passes candidates through unchanged. It keeps
an explicit graph stage for future candidate filtering or policy decisions.

### CPSM Branch Metric

`CPSMBranchMetricNode` consumes globally ordered pulse candidates. For each
candidate it requires usable host complex IQ evidence, then calls
`CPSMBranchMetricKernel::Compute`.

The kernel models binary CPSM with modulation index `h = 1/2`, four accumulated
phase states, rectangular full-response phase pulse shaping, and 32 symbols per
pulse.

### Viterbi Decoder

`CPSMViterbiDecoderNode` decodes each candidate pulse using
`CPSMViterbiDecoderKernel::Decode`.

The Viterbi state machine:

- Starts from `initial_phase_state`.
- Scores transitions for symbols `+1` and `-1`.
- Accumulates branch costs.
- Optionally enforces a terminal phase state.
- Emits symbol decisions, phase states, best path metric, second-best metric,
  and confidence.

### Pulse Word Decoder

`FHSSPulseWordDecoderNode` converts CPSM symbols to bits:

```text
+1 -> 0
-1 -> 1
```

It assembles each 32-symbol pulse MSB-first into a `uint32_t`, preserves pulse
metadata and Viterbi confidence, and reports invalid symbol count, invalid
symbol decisions, low confidence, and non-finite metrics.

### Preamble Detector

`FHSSPreambleDetectorNode` performs hop-only preamble detection. It checks the
first 16 globally ordered decoded pulses against the configured preamble
frequency sequence.

Important: repeated preamble frequencies must have consistent fixture word
values, but lock itself is based on hop order. Word mismatches in observed
decoded data do not prevent hop-only lock.

### Message Assembler

`FHSSMessageAssemblerNode` reorders decoded pulses globally, rejects unsupported
overlap, locks the configured preamble, splits preamble and payload pulses, and
requires payload frequencies to stay inside the locked active frequency set.

### Message Sink

`FHSSMessageSinkNode` stores the last assembled message and exposes diagnostics
through `IDiagnosable`. Diagnostics include pulse counts, rejected count,
preamble lock, active frequency indices, decoded pulses, sample-time mapping,
confidence, Viterbi metric, decoded value, and unsupported-feature flags.

## Configuration Flow

FHSS JSON parsing helpers live in `FHSSGraphXConfig.hpp`.

The source node config supplies:

- `active_frequency_indices`
- `iq_center_frequency_hz` or explicit `iq_offsets`
- `messages[]`
- `idle_mode`
- `idle_duration_samples`
- `occupied_bandwidth_hz`
- `max_abs_cfo_hz`
- unsupported-feature flags

Each scheduled message has a stable `message_id`, `transmit_start_sample`, and
ordered pulse list. The first 16 pulses must be marked `preamble`; later pulses
must be marked `body` or `payload`.

The graph generator expands the topology instead of relying on compact fan-out
syntax. That produces one node per detector and one explicit edge from each
channelizer output port into the corresponding detector.

## Runtime Integration

The demo executable is `examples/DSP/src/fhss_demo.cpp`.

It loads the canonical graph through `GraphExecutorBuilder`, dynamically loads
plugins, optionally patches the source and decoder configs from external
message JSON, runs the real graph, and can write:

- summary JSON
- effective config JSON
- optional channel IQ SigMF captures
- dashboard API state when built with `GRAPHX_BUILD_WEB_DASHBOARD`

The source supports message injection, and dashboard tests cover configuration,
rebuild controls, message injection, replay, visualization, and artifact export.

## Plugin Surface

Each FHSS graph node has a plugin wrapper under `libdsp/plugins`, including:

- `fhss_synthetic_iq_source_node_plugin.cpp`
- `fhss_downconverter_node_plugin.cpp`
- `fhss_fixture_frequency_channelizer_node_plugin.cpp`
- `per_channel_pulse_detector_node_plugin.cpp`
- `fhss_pulse_merge_node_plugin.cpp`
- `fhss_pulse_candidate_node_plugin.cpp`
- `cpsm_branch_metric_node_plugin.cpp`
- `cpsm_viterbi_decoder_node_plugin.cpp`
- `fhss_pulse_word_decoder_node_plugin.cpp`
- `fhss_preamble_detector_node_plugin.cpp`
- `fhss_message_assembler_node_plugin.cpp`
- `fhss_message_sink_node_plugin.cpp`

`test_fhss_graphx_nodes.cpp` verifies that nodes use accelerator control-token
sidecars and are registered/dynamically loadable.

## Acceleration Layer

`libaccelgraph` provides FHSS accelerator-facing nodes and type aliases. The
type bridge reuses the DSP FHSS token aliases, so accelgraph does not define a
separate semantic packet model.

Current accelerator-facing nodes include:

- `AccelFhssDownconverterNode`
- `AccelFhssChannelizerNode`
- `AccelFhssPerChannelPulseDetectorNode`
- `AccelFhssBranchMetricNode`

Configuration is shared through `FHSSAccelConfig`:

- backend: `cpu`, `metal`, or `cuda`
- fallback policy: `strict` or `allow`
- provider id
- session key
- CUDA device ordinal

The current native Metal and CUDA FHSS kernel paths are intentionally not
implemented. The nodes wrap CPU reference implementations, report exact
diagnostics for unavailable native paths, and support fallback behavior. CUDA
paths include session probes and staging transfers for validation, but fallback
execution is not reported as native GPU execution.

## Test Architecture

The test suite covers the implementation in layers:

- Protocol validation: timing, frequency table, active sets, preamble shape,
  payload membership, IQ offset guards.
- Synthetic IQ generation: sample counts, truth metadata, unsupported feature
  rejection, nonzero transmit times, idle output, overlap rejection.
- Pulse merge: global timing normalization, decimation mapping, duplicate
  selection, overlap rejection, evidence preservation.
- CPSM decode: trellis transitions, branch metrics, Viterbi oracle parity,
  terminal phase policy, invalid evidence length.
- Word decode: symbol-to-bit mapping, MSB-first assembly, metadata preservation,
  low confidence and invalid metrics.
- Message assembly: hop-only lock, invalid active sets, payload rejection,
  message length, missing preamble, overlap rejection.
- Packet/node contracts: edge packet definitions, token sidecars, channelized
  contracts, plugin registration.
- Graph executor: canonical topology execution and sink diagnostics.
- Accelgraph: CPU parity, strict/allow fallback behavior, descriptor fields,
  hybrid pipeline execution, and evidence artifacts.
- Demo/dashboard: CLI behavior, external message JSON, deterministic dashboard
  APIs, rebuild controls, event replay, visualization, and artifact export.

## Architectural Limits

The implementation intentionally does not yet provide:

- Production RF channelization or adjacent-channel rejection guarantees.
- A defined spectral mask or production channel filter specification.
- Noise, multipath, CFO estimation, or broader impairment modeling.
- Overlap-aware separation of simultaneous cross-frequency pulses.
- Native Metal/CUDA FHSS kernels.
- Full simultaneous interpretation of all 64 1 GHz-spaced RF table entries as
  alias-free 500 Msps sampled RF.

The correct reading is that the system models a deterministic, validated,
channelized FHSS fixture over a semantic packet contract designed to survive
future accelerator storage changes.

## Extension Points

The cleanest extension points are:

- Replace fixture channel mixing/decimation with a real channelizer while
  preserving one GraphX output port per configured frequency.
- Add native accelerator kernels behind the existing accelgraph wrappers while
  preserving DSP packet sidecars.
- Add impairment support by extending generator validation and diagnostics
  deliberately instead of silently accepting feature flags.
- Add overlap-aware association in `FHSSPulseMergeKernel` and message assembly
  with explicit status vocabulary.
- Move sample storage to accelerator sidecars while keeping timing/frequency
  metadata available to CPU-visible diagnostics and graph contracts.

## Review Notes

The implementation has a coherent contract boundary: protocol and semantic
metadata are central, and node implementations mostly transform tokens without
inventing local side channels. The strongest architectural choice is the
compile-time 64-port channelizer plus per-channel detector fan-out, because it
makes the one-frequency-one-edge contract visible to GraphX and plugins.

The main risk is terminology drift. Several components are named like receiver
building blocks, but their current behavior is deterministic fixture logic. New
work should continue to say "fixture channelizer", "CPU reference", or
"accelerator-facing wrapper" until native channel filters, impairment models,
and accelerator kernels exist.
