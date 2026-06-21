# DSP FHSS Decoder Fixture

This document describes the deterministic GraphX FHSS CPSM fixture lanes added
through PR1 through PR16. They are CPU-only test fixture and decoder pipelines.
They are not production RF receivers, not claims of compatibility with any
external RF waveform, and not direct 1 GHz RF sampling models.

## Current Graph

The canonical FHSS implementation uses real GraphX nodes, dynamically loadable
plugins, and GraphX edge packet contracts. The canonical FHSS fixture graph is
the channelized graph:

```text
libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
```

Its top-level config metadata sets:

```json
{
  "fhss_graph_role": "canonical_channelized_fixture",
  "canonical_fhss_graph": true
}
```

The deleted pre-GraphX pseudo-node scaffolding is not the current node model.

The PR8 correlator-bank fixture config is retained only as a compatibility and
reference topology:

```text
libdsp/config/fhss_cpsm_fixture_500msps.json
```

Its top-level config metadata sets:

```json
{
  "fhss_graph_role": "reference_correlator_bank_fixture",
  "canonical_fhss_graph": false,
  "reference_only": true
}
```

The reference CPU lane is:

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

The canonical PR14/PR15 channelized fixture config is:

```text
libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
```

The canonical channelized CPU lane is:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
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

The canonical graph always wires the source through `FHSSDownconverterNode` before
channelization, even when the downconverter is configured as validated
passthrough. `ChannelizerNode` exposes exactly 64 GraphX output ports, one per
frequency index. Output port `N` feeds one `PerChannelPulseDetectorNode` for
frequency index `N` and channel id `N`; ports `0` and `63` remain receiver
guard/metadata channels and are still invalid transmitted pulse frequencies.

FHSS GraphX node ports use `graph::gpu::accel::ControlToken<...>` carrying the
FHSS PR7A packet sidecars. Complex IQ evidence, timing metadata, frequency
metadata, confidence, and decoder diagnostics are carried through those
sidecars. Future accelerator storage may move sample buffers out of band, but
the FHSS semantic packet contract remains the GraphX edge model.

## Protocol Limits

The deterministic fixture uses:

- `500 Msps` sample rate.
- `5 Mbps` bit rate.
- `100 samples/symbol`.
- `32 symbols/pulse`.
- `3200` pulse samples.
- `3300` gap samples.
- `6500` samples per pulse period.
- At most `256` total pulses, including the preamble.
- Explicit source message schedules. `FHSSSyntheticIqSourceNode` is configured
  with `messages[]`; each message has a stable `message_id`, a
  `transmit_start_sample`, and an ordered pulse list.

The RF metadata table has exactly 64 entries:

```text
rf_frequency_hz = 1 GHz + frequency_index * 8 MHz
frequency_index in [0, 63]
```

Indices `0` and `63` are reserved edge/guard entries. Selectable active
frequencies are limited to indices `[1, 62]`. The current fixture uses exactly
four transmitted active frequencies derived from the 16-pulse preamble pattern.
Payload/body pulse frequencies are not selected randomly by the source; every
transmitted pulse explicitly supplies its `frequency_index`, `value`, and
`role`.

The source message schema is:

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
        {"frequency_index": 28, "value": 16909060, "role": "body"}
      ]
    }
  ],
  "idle_mode": "zero",
  "idle_duration_samples": 0
}
```

The first 16 pulses of each message are the preamble and must be marked
`"preamble"`. Remaining message pulses must be marked `"body"`. Zero-message
source configs are allowed when they provide an explicit idle duration; they
emit deterministic zero/NULL complex samples.

The preamble is hop-only:

- exactly 16 pulses;
- lock is based on the hop sequence;
- decoded preamble word values are fixture consistency / secondary validation;
- identical preamble frequencies must have identical fixture word values.

## Frequency Truth In Labeling

The 1 GHz RF frequencies are metadata. Fixture IQ uses baseband/IF offset
frequencies at `500 Msps`.

A 500 Msps complex sample stream cannot represent the full 64-entry 1 GHz RF
table as direct sampled RF while also preserving 8 MHz absolute spacing. The
fixture therefore stores both:

```cpp
uint32_t frequency_index;
double rf_frequency_hz;        // protocol metadata
double iq_offset_frequency_hz; // sampled fixture offset
```

The IQ offset is what the generator uses in the complex exponential and what
the detector uses for dehopping. RF center frequency remains metadata for truth
comparison and diagnostics.

PR10 source configs may derive offsets from the RF metadata table and an IQ
center:

```text
iq_offset_frequency_hz = rf_frequency_hz - iq_center_frequency_hz
```

The derived offsets are still validated against Nyquist, occupied-bandwidth,
and CFO guards. The bundled fixture uses active indices close enough to the IQ
center for those guards to pass in a 500 Msps complex-baseband span.

## RF Feasibility And Future Coverage Strategy

The current deterministic channelized graph is still a fixture graph. It does
not claim production channelizer separation.

The exact occupied-bandwidth and channel-filter requirements for 5 Mbps CPSM on
8 MHz spacing remain unresolved in PR16. A later PR must define the
occupied-bandwidth metric or RF spectral mask, channel filter passband,
transition width, stopband attenuation, group delay, and adjacent-channel
rejection threshold before the decoder can make production channelizer claims.

The default future full selectable-frequency coverage strategy is retuned
sub-band windows. The receiver configuration preserves one logical GraphX
channel output port per configured frequency, but a physical 500 Msps capture
window realizes only the frequency subset whose RF centers, occupied bandwidth,
CFO allowance, and filter guards fit in that sub-band. The full 64-entry RF
metadata table must not be described as one simultaneous alias-free 500 Msps
complex-baseband capture. Full-table simultaneous capture requires a higher
sample rate or a later explicit alias/downconversion model.

Current canonical impairment diagnostics remain limited to disabled/rejected
fixture status, including `unsupported_impairments_rejected`. Future impairment
work must use a planned status vocabulary before adding Doppler/noise/CFO/phase
drift/multipath behavior:

```text
unsupported
disabled
configured_rejected
estimated
degraded
invalid
```

PDW diagnostics remain optional and non-canonical. They must not replace the
GraphX FHSS decoder packet contracts or become the canonical decoder output
unless a later PR explicitly changes that contract.

## CPSM Assumptions

The fixture uses binary CPSM modeled as continuous phase modulation:

```text
a[k] in {-1, +1}
0 -> +1
1 -> -1
theta(t) = 2*pi*h*sum_k a[k]*q(t-k*T_b)
s(t) = A * exp(j*theta(t))
```

Initial fixture assumptions:

- `h = 1/2`;
- rectangular full-response phase pulse;
- initial phase `0`;
- continuity inside each pulse;
- terminal phase unconstrained unless checked by the decoder.

Each pulse carries exactly one `uint32_t` value as 32 CPSM symbol decisions.
The pulse-word decoder maps `+1 -> 0`, `-1 -> 1`, and assembles MSB first.

Magnitude-only DFT/FFT output is not the canonical decoder input. Word recovery
comes from complex IQ evidence via CPSM branch metrics, Viterbi/MLSE symbol
decisions, and pulse-word decoding.

The correlator-bank graph is useful for regression comparison and PR1
compatibility. It must not be described as production-like channelization, and
new end-to-end fixture work should target the canonical channelized graph unless
a PR explicitly states otherwise.

## Unsupported In PR1 Through PR16

The current deterministic lane rejects or leaves out the following behavior:

- real RF capture;
- external datasets;
- direct 1 GHz RF sampling at 500 Msps;
- production channelizer separation claims beyond the deterministic CPU fixture channelizer;
- Doppler, noise, multipath, and phase-impairment behavior;
- overlapped message separation;
- Metal/GPU acceleration of the FHSS lane;
- production RF compatibility or external waveform compatibility;
- PDW diagnostics as canonical decoder output.

Overlap is unsupported in the current PR1 behavior and is reported/rejected
deterministically.

## Future Boundaries

Future work may add a real channelizer, occupied-bandwidth validation for the
selected CPSM pulse shape, pulse-start acquisition beyond
`message_start_sample = 0`, Doppler/noise robustness, overlap-aware message
separation, Metal acceleration, and optional PDW-style diagnostics.

Those features should be added only with explicit graph contracts and tests.
They should not relabel the current CPU fixture as production RF, GPU
accelerated, overlap-aware, Doppler/noise-capable, or PDW-driven.

## Validation

The current CI-safe regression gates are:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  --gtest_filter='FHSSGraphXExecutorTest.*:FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXGuardrailTest.*'

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  --gtest_filter='*FHSS*:*CPSM*'
```
