# DSP FHSS Decoder Fixture

This document describes the deterministic GraphX FHSS CPSM fixture lane added
through PR1 through PR8. It is a CPU-only test fixture and decoder pipeline. It
is not a production RF receiver, not a claim of compatibility with any external
RF waveform, and not a direct 1 GHz RF sampling model.

## Current Graph

The canonical FHSS implementation uses real GraphX nodes, dynamically loadable
plugins, and GraphX edge packet contracts. The deleted pre-GraphX pseudo-node
scaffolding is not the current node model.

The PR8 fixture config is:

```text
libdsp/config/fhss_cpsm_fixture_500msps.json
```

The CPU lane is:

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

The RF metadata table has exactly 64 entries:

```text
rf_frequency_hz = 1 GHz + frequency_index * 8 MHz
frequency_index in [0, 63]
```

Indices `0` and `63` are reserved edge/guard entries. Selectable active
frequencies are limited to indices `[1, 62]`. The current fixture uses exactly
four active frequencies derived from the 16-pulse preamble pattern. Payload/body
pulse frequencies are deterministic random selections from those four active
preamble frequencies.

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

## Unsupported In PR1 Through PR8

The current deterministic lane rejects or leaves out the following behavior:

- real RF capture;
- external datasets;
- direct 1 GHz RF sampling at 500 Msps;
- real channelizer topology or channelizer separation claims;
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
