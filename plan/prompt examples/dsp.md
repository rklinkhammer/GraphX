```text
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR-sized plan for a GraphX DSP spectrogram/spectrum demo using the existing libdsp nodes and the same GraphX graph builder, executor, plugin loading, JSON config, and runtime patterns used by existing examples.

Goal:
Add a clean DSP demo shape:

SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>

This should demonstrate GraphX runtime dataflow with a deterministic signal-processing chain before any GPU/Metal DSP work is attempted.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- Current repository state
- libdsp/include/dsp/SineSignalNode.hpp
- libdsp/include/dsp/SineWaveGenerator.hpp
- libdsp/include/dsp/FFTNode.hpp
- libdsp/include/dsp/FFTManager.hpp
- libdsp/include/dsp/SpectrumSinkNode.hpp
- libdsp/plugins/CMakeLists.txt
- libgraph/test/unit/test_sine_signal_node_standalone.cpp
- Existing example runtime patterns using GraphBuilder, Executor, plugin loading, JSON config, and completion signaling
- Existing examples/SAR runtime/config/test patterns only as structural references, not as SAR architecture requirements


Planning requirements:
- Do not implement code.
- Do not add GPU or Metal DSP work in this plan.
- Do not redesign libdsp.
- Do not introduce compatibility shims.
- Prefer the existing `256` packet-size path because `SpectrumSinkNode<256>` already has a plugin.
- Use existing GraphX graph builder/executor/plugin mechanisms.
- Keep the first demo CPU-only and deterministic.
- Preserve existing libdsp node contracts:
  - `SineSignalNode<256>` emits `IqPacket<float, 256>`.
  - `FFTNode<float, 256>` consumes IQ messages and emits `MagnitudePacket<float, 256>`.
  - `SpectrumSinkNode<float, 256>` consumes `MagnitudePacket<float, 256>`.
- Include an explicit future extension boundary for a later Metal/FFT/spectrogram PR, but do not plan implementation details for that PR beyond identifying the boundary.
- Each planned PR must compile and test independently.
- Each PR must include tests.
- Avoid compatibility aliases and duplicate demo paths.

Required planning coverage:
1. Add a graph JSON config for the DSP chain.
2. Add or update plugin loading so the three DSP plugins can be loaded by the demo/runtime.
3. Add a small example executable or extend an existing example runner only if that matches current project conventions.
4. Add a graph-builder/executor integration test that runs the full chain.
5. Verify deterministic peak detection for a known sine frequency, such as 1000 Hz at 48000 Hz sample rate.
6. Verify completion/runtime behavior using the same GraphX executor completion conventions used elsewhere.
7. Add spectrum/spectrogram-style output artifacts only if they are small and deterministic:
   - JSON summary with frame count, peak frequency, peak magnitude, sample rate, FFT size, window type, and node diagnostics.
   - Optional text/CSV spectrum bins.
   - Do not require PNG/image output in the first PR unless existing helpers make this trivial.
8. Add documentation describing how to build and run the DSP demo.
9. Identify current gaps:
   - direct DFT, not FFT library/GPU FFT;
   - CPU-only;
   - only `SpectrumSinkNode<256>` plugin exists;
   - no real-time audio input;
   - no Metal execution yet.
10. Include future follow-up boundaries:
   - real spectrogram image sink;
   - multi-frame/chirp fixture;
   - CPU-vs-Metal parity;
   - real Metal kernel or Metal Performance Shaders FFT if supported;
   - performance instrumentation comparison.

Output:
Save the planner report to:

plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md

Report format:
For each planned PR provide:
- title
- purpose
- files to touch
- files to delete
- tests to add
- tests to delete
- acceptance criteria
- risks
- rollback plan
- whether it is CI-safe or local-only

Suggested PR shape:
- PR1: CPU DSP graph demo config and integration test.
- PR2: Demo executable or runner plus deterministic JSON/spectrum output.
- PR3: Documentation and guardrails for CPU-only/non-GPU truth-in-labeling.
- Future, out of scope: Metal DSP implementation and spectrogram image sink.
```


================

Recommended improvements:

1. **Rename / split the CPU FFT path**
   The current `FFTNode` is really a CPU spectrum node using `FFTManager`, whose comment says it uses a direct DFT for `N <= 1024`, not a true FFT. Rename it to `CpuSpectrumDftNode` or replace the implementation with an actual radix-2 FFT. Otherwise it conflicts conceptually with `MetalSpectrumDftNode`, which is explicitly `direct_dft`.  

2. **Unify CPU and Metal output semantics**
   `MetalSpectrumDftNode` always emits `MagnitudePacket` with `num_accumulated_packets = 1`, while the CPU path supports accumulation through `FFTManager`. Either add accumulation to the GPU path or make accumulation a separate upstream node so CPU/GPU spectrum nodes are comparable.  

3. **Do not hard-code Hann inside the Metal kernel**
   The Metal DFT kernel embeds Hann windowing directly in generated source. Move window type into config and either generate the proper kernel source or pass precomputed window coefficients as a device buffer. 

4. **Fix diagnostics**
   `FFTNode::GetDiagnostics()` returns an empty object, while the Metal/H2D/D2H nodes expose useful state such as backend, bytes, tickets, peak bin, and algorithm. Add diagnostics for sample rate, window type, accumulation count, packet counters, FFT/DFT count, average compute time, peak frequency history, and last output validity.  

5. **Make config validation consistent**
   `FFTNode::Configure()` silently ignores invalid `accumulation_count <= 0` and unknown window names, while `ConfigureExpected()` rejects them. Use one validation policy and make invalid configuration fail loudly. 

6. **Catch sidecar type errors in `FFTNode`**
   The GPU DFT path catches `std::bad_cast` when extracting the IQ sidecar. `FFTNode` directly calls `packet.sidecar.template get<IqPacketType>()`. Add the same guarded extraction so malformed tokens return `nullopt` instead of throwing through the graph.  

7. **Add algorithm selection**
   Introduce a common node parameter:
   `algorithm = direct_dft | radix2_fft | metal_dft | metal_fft`
   Then use a common spectrum interface so tests can compare CPU DFT, CPU FFT, and Metal DFT from the same IQ input.

8. **Improve GPU execution model**
   The Metal DFT launches one thread per bin with one serial loop over all samples. That is fine for validation, but should be labeled reference-quality. For performance, add a real Metal FFT path using staged radix-2 butterflies, shared/threadgroup memory where possible, and separate magnitude extraction.

9. **Separate “complex spectrum” from “magnitude spectrum”**
   Both CPU and Metal paths collapse directly to magnitudes. For SAR, phase matters. Add an intermediate `ComplexSpectrumPacket` / complex device layout, then make magnitude a downstream optional node.

10. **Fix D2H synchronization semantics**
    `DspMagnitudeD2HNode` waits on the kernel event before enqueueing D2H. Prefer preserving async graph semantics by having D2H depend on the kernel ticket/event instead of host-blocking, unless this node is explicitly a synchronization boundary. 

Highest-priority PR order:

**PR1:** Rename/document CPU DFT vs FFT and fix config/error handling.
**PR2:** Add useful diagnostics and metrics.
**PR3:** Unify CPU/GPU spectrum semantics and tests.
**PR4:** Add complex spectrum output.
**PR5:** Add real FFT backend after correctness is locked.

Yes. Add a dedicated **IQWindowNode** before FFT/DFT.

Suggested stream shape:

```text
IqSource
  -> IqWindowNode
  -> FftNode / DftNode
  -> ComplexSpectrumNode or MagnitudeNode
  -> Sink
```

The window node should:

```cpp
IqPacket<N> -> WindowedIqPacket<N>
```

or, better for your token architecture:

```cpp
AccelToken + IqSidecar
  -> AccelToken + WindowedIqSidecar
```

It should record:

```json
{
  "window_type": "hann",
  "window_length": 256,
  "normalization": "coherent_gain",
  "overlap": 0,
  "sample_rate_hz": 48000
}
```

Other DSP nodes that can reuse the same **windowed IQ packets**:

```text
FFT / DFT spectrum
STFT / spectrogram
PSD / Welch estimator
channel power / band-power detector
peak-frequency tracker
frequency-domain filter bank
matched filter / pulse compression preconditioning
cross-correlation / ambiguity-function analysis
phase / instantaneous-frequency analysis
SAR range compression
Doppler processing
```

One caution: do **not** force all IQ consumers to take windowed IQ. Some algorithms want raw IQ:

```text
AGC
DC offset removal
IQ imbalance correction
decimation / resampling
raw capture sink
time-domain correlation
some matched filters
```

So the better design is:

```text
RawIqPacket
  -> optional conditioning nodes
  -> IqWindowNode
  -> spectral / Doppler / PSD consumers
```

For GraphX, I would make windowing a **first-class DSP transform node**, not hidden inside `FFTNode` or `MetalSpectrumDftNode`. Then CPU FFT, CPU DFT, Metal DFT, and future Metal FFT all consume the same semantics.
A **frequency-hopping pulse detector producing Pulse Descriptor Words (PDWs)** is substantially more complex than a simple FFT spectrum analyzer, but it is very achievable. In fact, much of a modern Electronic Support Measures (ESM) or SIGINT receiver is built around this problem.

## Processing Complexity

A typical processing chain looks like:

```text
IQ Stream
    ↓
Window Node
    ↓
FFT / Channelizer
    ↓
Noise Estimation
    ↓
CFAR Thresholding
    ↓
Peak Detection
    ↓
Pulse Start/End Detection
    ↓
Pulse Tracking Across Time
    ↓
Frequency Hop Association
    ↓
Pulse Descriptor Word (PDW) Generation
    ↓
Emitter Identification / Deinterleaving
```

The difficulty increases at each stage:

| Stage                         | Complexity | Notes                           |
| ----------------------------- | ---------- | ------------------------------- |
| FFT/channelizer               | Low        | Already have this               |
| Thresholding (CFAR)           | Low        | Cell averaging                  |
| Pulse detection               | Medium     | Start/end timestamps            |
| Frequency estimation          | Medium     | Interpolation, centroid         |
| Pulse width estimation        | Medium     | Time tracking                   |
| Frequency hopping association | High       | Track same emitter through hops |
| Multiple overlapping emitters | Very High  | Deinterleaving problem          |
| PRI estimation                | Very High  | Histograms, clustering          |
| Emitter identification        | Very High  | EW/ESM level                    |

---

# PDW contents

Typical PDW:

```cpp
struct PDW {
    uint64_t toa_ns;              // time of arrival
    uint64_t pulse_width_ns;
    float frequency_hz;
    float amplitude_db;
    float phase;
    float bandwidth_hz;
    float snr_db;
    uint32_t emitter_id;
};
```

Military systems often include:

* AOA (angle of arrival)
* confidence
* modulation classification
* chirp rate
* instantaneous bandwidth
* frequency slope
* PRI group

---

# Frequency-Hopping Detection Difficulty

## Single emitter hopping

Easy.

```text
100 MHz
100 MHz
100 MHz
250 MHz
250 MHz
500 MHz
500 MHz
```

Track strongest peak over time.

---

## Multiple simultaneous hoppers

Hard.

```text
Emitter A:
100 → 150 → 200 MHz

Emitter B:
300 → 100 → 350 MHz

Emitter C:
150 → 400 → 250 MHz
```

Now association becomes a clustering problem.

Modern systems use:

* Kalman filters
* Multi-Hypothesis Tracking (MHT)
* JPDA
* DBSCAN clustering
* Neural networks

---

# Computational Requirements

For a 100 MHz IF:

```text
4096 FFT every 10 μs

100k FFT/sec

CFAR
Peak extraction
Pulse tracking
Association
```

GPU acceleration helps enormously.

---

# GraphX Node Architecture

I would split the problem into many nodes:

```text
IqSource
    ↓
WindowNode
    ↓
FFTNode
    ↓
PowerSpectrumNode
    ↓
CFARNode
    ↓
PeakDetectorNode
    ↓
PulseDetectorNode
    ↓
FrequencyEstimatorNode
    ↓
PDWGeneratorNode
    ↓
EmitterTrackerNode
    ↓
EmitterClassifierNode
```

Notice that **WindowNode** and **FFTNode** are reusable by many downstream algorithms.

---

# Open-source implementations

## GNU Radio

Probably the best source.

Contains:

* FFT
* Polyphase channelizers
* Burst detectors
* Tagging framework
* Radar blocks

but no complete ESM PDW generator.

Official:

[GNU Radio](https://www.gnuradio.org/?utm_source=chatgpt.com)

---

## gr-radar

Radar extension to GNU Radio.

Provides:

* pulse extraction
* burst processing
* range/Doppler processing

Useful ideas but not full EW.

---

## gr-specest

Spectral estimation blocks:

* Welch
* MUSIC
* Cyclostationary

Good building blocks.

---

## REDHAWK SDR

Developed by government contractors.

Provides:

* signal tracking
* emitter management

Much closer to ESM architecture.

Official:

[REDHAWK SDR](https://redhawksdr.org/?utm_source=chatgpt.com)

---

## PySDR

Contains educational examples for:

* burst detection
* waterfall analysis
* spectrograms

Official:

[PySDR](https://pysdr.org/?utm_source=chatgpt.com)

---

## HermesPy

Contains:

* radar
* channel models
* waveform generation

Official:

[HermesPy](https://hermespy.org/?utm_source=chatgpt.com)

---

## SigMF Ecosystem

Useful for datasets and offline replay.

Official:

[SigMF](https://sigmf.org/?utm_source=chatgpt.com)

---

# Existing Open ESM Systems?

Very few.

Complete systems generating PDWs and performing deinterleaving are largely absent from open source because:

* EW/ESM is defense-related.
* Much work is ITAR-controlled.
* Most implementations are proprietary.

---

# GraphX Opportunity

Your GraphX architecture is unusually well suited to this problem because a PDW receiver naturally decomposes into nodes:

```text
IQ
 ↓
Window
 ↓
FFT
 ↓
CFAR
 ↓
Peak
 ↓
Pulse
 ↓
PDW
 ↓
Track
 ↓
Classify
```

The first six stages are generic DSP and could become a reusable library.

---

### Suggested first milestone

I would implement:

```text
SineSource
    ↓
WindowNode
    ↓
FFTNode
    ↓
CFARNode
    ↓
PeakDetectorNode
    ↓
PulseDetectorNode
    ↓
PDWGeneratorNode
    ↓
PDWSink
```

This produces a stream of PDWs from hopping tones and is roughly **PR1-level complexity**.

Emitter tracking and deinterleaving are probably **an order of magnitude more difficult** and would be a later phase.


Yes. There are several open-source projects that can generate **synthetic frequency-hopping IQ streams**, although very few are specifically intended as EW/PDW test sources. Most are communication-oriented. Some are excellent foundations for GraphX.

---

# 1. GNU Radio FHSS Flowgraphs

[GNU Radio](https://www.gnuradio.org/?utm_source=chatgpt.com)

There are numerous FHSS examples and OOT modules available. They generate actual complex IQ samples and support:

* pseudo-random hop sequences
* adjustable dwell time
* modulation (CW, FSK, PSK, LoRa)
* multiple channels
* noise injection

Example repositories include:

* FHSS_GNUradio
* Trondeau frequency hopper sequence generator
* ELRS FHSS implementations

([GNU Radio Events (Indico)][1])

### Complexity

Moderate.

### Output

```cpp
std::complex<float>
```

continuous IQ streams.

---

# 2. liquid-dsp Multi-source Generator

[liquid-dsp](https://liquidsdr.org/?utm_source=chatgpt.com)

The `msource` object allows many independently controlled signals with sample-level frequency control.

It supports:

* multiple simultaneous emitters
* amplitude control
* carrier frequency changes
* noise

([Liquid-DSP][2])

This is perhaps the simplest foundation for constructing:

```text
Emitter A:
100 → 150 → 250 MHz

Emitter B:
300 → 325 → 400 MHz
```

with overlapping transmissions.

---

# 3. ExpressLRS GNU Radio Implementation

An open-source implementation of the ELRS hopping mechanism.

Supports:

* pseudo-random hop sequences
* synchronized hopping
* LoRa modulation

([GNU Radio Events (Indico)][1])

Useful if realistic communications waveforms are desired.

---

# 4. SigMF

[SigMF](https://sigmf.org/?utm_source=chatgpt.com)

Not a generator itself, but ideal for recording and replaying synthetic IQ datasets.

You could create:

```text
FHSS_001.sigmf-data
FHSS_001.sigmf-meta
```

containing:

* hop table
* dwell times
* emitter IDs
* ground-truth PDWs

([Wikipedia][3])

---

# 5. Build a Purpose-Built GraphX Generator

For PDW and ESM work, this may actually be superior to existing projects.

## Emitter Model

```cpp
struct Emitter {
    float power_db;
    float bandwidth_hz;

    std::vector<float> hop_table;

    float dwell_time_us;

    ModulationType modulation;

    uint64_t seed;
};
```

---

## Node Graph

```text
EmitterGeneratorNode
            ↓
HopSchedulerNode
            ↓
ModulatorNode
            ↓
NoiseAdderNode
            ↓
IqPacketSourceNode
```

---

### Output

```cpp
IqPacket<1024>
```

with perfect ground truth:

```cpp
struct GroundTruthPulse {
    uint64_t toa_ns;
    uint64_t pulse_width_ns;
    float center_frequency_hz;
    uint32_t emitter_id;
};
```

---

# 6. What Doesn't Exist

I am not aware of an open-source equivalent of:

* Mercury Systems DRFM simulators
* BAE pulse simulators
* SPARTA EW scenario generators

that can generate:

```text
multiple hopping emitters
pulse trains
PRI modulation
staggered PRI
chirps
overlapping emitters
ground-truth PDWs
```

Those systems are generally proprietary.

---

# Recommendation for GraphX

Rather than trying to adapt a communications-oriented FHSS generator, I would create:

```text
PseudoEmitterNode
        ↓
HopSchedulerNode
        ↓
PulseShaperNode
        ↓
NoiseAdderNode
        ↓
IqSourceNode
```

and generate:

```text
IQ stream
+
GroundTruthPDW stream
```

simultaneously.

That would let you verify:

```text
IQ
 ↓
WindowNode
 ↓
FFTNode
 ↓
CFARNode
 ↓
PeakDetectorNode
 ↓
PulseDetectorNode
 ↓
PDWGeneratorNode
```

against known truth.

Such a generator would be relatively modest (perhaps 5–10 nodes) and would provide an extremely valuable testbed for everything above it. In many ways, it would resemble a miniature EW signal simulator, which surprisingly has very few open-source counterparts.

[1]: https://events.gnuradio.org/event/26/contributions/771/attachments/238/622/GabrielGarcia-FAUCAAI-Grcon25.pdf?utm_source=chatgpt.com "Implementation and Analysis of ExpressLRS Under ..."
[2]: https://liquidsdr.org/doc/msource/?utm_source=chatgpt.com "Multi-source signal generator (msource)"
[3]: https://en.wikipedia.org/wiki/SigMF?utm_source=chatgpt.com "SigMF"

Yes — you need a **synthetic FHSS message IQ generator**, not a generic tone hopper.

I would model it like this:

```text
FHSSMessage
  available_frequencies[]
  preamble_hop_sequence[]
  payload_pulses[]
  word_bits = 32
```

Pipeline:

```text
MessageSourceNode
  -> HopPatternEncoderNode
  -> PulseWordEncoderNode
  -> IqModulatorNode
  -> Noise/ImpairmentNode
  -> IqPacketizerNode
```

Core structures:

```cpp
struct HopFrequency {
    uint32_t index;
    double frequency_hz;
};

struct PulseSymbol {
    uint32_t frequency_index;
    uint64_t start_sample;
    uint64_t duration_samples;
    uint32_t word_value;
    bool is_preamble;
};

struct FHSSMessage {
    std::vector<double> available_frequencies_hz;
    std::vector<PulseSymbol> preamble;
    std::vector<PulseSymbol> payload;
};
```

For encoding a 32-bit word, use something explicit:

```text
word bit 0 -> frequency set A
word bit 1 -> frequency set B
```

or stronger:

```text
each pulse encodes 2–4 bits by selecting one of 4/16 frequencies
```

Example:

```text
16 available frequencies
4 bits per pulse
32-bit word = 8 pulses
```

That maps cleanly:

```cpp
frequency_index = (word >> (4 * pulse_index)) & 0xF;
```

The IQ generator then emits:

```cpp
iq[n] = amplitude * exp(j * phase[n])
```

with:

```cpp
phase[n+1] = phase[n] + 2*pi*frequency_hz/sample_rate_hz
```

Recommended message format:

```json
{
  "sample_rate_hz": 1000000,
  "available_frequencies_hz": [50000, 75000, 100000, 125000],
  "pulse_duration_us": 100,
  "guard_duration_us": 20,
  "preamble": {
    "word": "0xA5A5A5A5",
    "hop_pattern": [0, 1, 3, 2, 0, 3, 1, 2]
  },
  "payload": [
    {"word": "0xDEADBEEF"},
    {"word": "0x12345678"}
  ]
}
```

GraphX node design:

```text
FHSSMessageSourceNode
    emits FHSSMessageSidecar

FHSSPulsePlannerNode
    converts words + hop rules into PulsePlanSidecar

FHSSIqSynthesisNode
    converts PulsePlan into IqPacket<N>

FHSSGroundTruthNode
    emits expected PDWs/message truth
```

Key point: keep **truth data** beside the generated IQ:

```cpp
struct TruthPulse {
    uint64_t toa_sample;
    uint64_t width_samples;
    uint32_t frequency_index;
    double frequency_hz;
    uint32_t word_value;
    uint32_t bit_offset;
    bool is_preamble;
};
```

This gives you a deterministic test source for:

```text
IQ -> Window -> FFT/DFT -> CFAR -> PulseDetect -> PDW -> Decode32
```

I would implement this as a **GraphX test signal source**, not adapt GNU Radio first. It is small, deterministic, and exactly matches your detector validation needs.


Yes — the design should make **each pulse carry a full `uint32_t` value**, not a byte or nibble-derived symbol.

Update the model to:

```cpp
struct FHSSPulse {
    uint64_t start_sample;
    uint64_t duration_samples;

    uint32_t frequency_index;
    double frequency_hz;

    uint32_t value;        // full 32-bit pulse value
    bool is_preamble;
};
```

Then the message becomes:

```cpp
struct FHSSMessage {
    std::vector<double> available_frequencies_hz;

    std::vector<FHSSPulse> preamble_pulses;
    std::vector<FHSSPulse> payload_pulses;
};
```

The important correction is:

```text
one pulse == one 32-bit value
```

not:

```text
one pulse == 4 bits
8 pulses == one 32-bit word
```

So the generator should produce ground truth like:

```cpp
struct TruthPulse {
    uint64_t toa_sample;
    uint64_t width_samples;
    uint32_t frequency_index;
    double frequency_hz;

    uint32_t pulse_value;   // exact 32-bit decoded value
    bool is_preamble;
};
```

The detector/decoder target then becomes:

```text
IQ stream
  -> WindowNode
  -> FFT/DFT
  -> CFAR
  -> PulseDetector
  -> PDWGenerator
  -> PulseValueDecoder
```

Where `PulseValueDecoder` emits:

```cpp
struct DecodedPulseWord {
    uint64_t toa_sample;
    uint32_t frequency_index;
    uint32_t value;
    bool is_preamble;
};
```

For the preamble:

```cpp
std::vector<FHSSPulse> preamble = {
    {.frequency_index = 0, .value = 0xA5A5A5A5, .is_preamble = true},
    {.frequency_index = 3, .value = 0x5A5A5A5A, .is_preamble = true},
    {.frequency_index = 1, .value = 0xDEADBEEF, .is_preamble = true}
};
```

For payload:

```cpp
std::vector<FHSSPulse> payload = {
    {.frequency_index = 2, .value = 0x12345678},
    {.frequency_index = 0, .value = 0xCAFEBABE},
    {.frequency_index = 3, .value = 0x00000001}
};
```Yes, and this gets to an important architectural distinction.

There are **two kinds of decoders**:

## 1. Signal-Level Decoder (works directly on IQ)

This is probably what you're describing.

```text
IQ
  ↓
Window
  ↓
FFT / Channelizer
  ↓
Energy detector
  ↓
Pulse extraction
  ↓
Frequency tracker
  ↓
Symbol decoder
  ↓
32-bit pulse value
```

This decoder never produces generic PDWs first. Instead, it treats the hopping protocol as a waveform and attempts to recover:

```cpp
struct DecodedPulse {
    uint64_t toa;
    uint32_t value;
    uint32_t frequency_index;
    bool is_preamble;
};
```

The decoder itself performs:

* synchronization
* hop tracking
* symbol timing recovery
* frequency estimation
* pulse-width estimation
* bit decoding

This is analogous to a modem receiver.

---

## 2. PDW-Based Decoder

```text
IQ
 ↓
FFT
 ↓
CFAR
 ↓
Pulse detector
 ↓
PDW
 ↓
Protocol decoder
```

Here the protocol decoder works on:

```cpp
struct PDW {
    toa;
    width;
    frequency;
    power;
};
```

This approach is common in ESM systems where the protocol is unknown.

---

# For your protocol

Since:

* the hopping frequencies are known,
* the pulse widths are known,
* each pulse carries a 32-bit value,

I would **decode directly from IQ**, not from PDWs.

---

# I would architect it like this:

```text
IQ
 ↓
WindowNode
 ↓
FFTNode or PolyphaseChannelizerNode
 ↓
HopMatchedFilterNode
 ↓
PulseSynchronizerNode
 ↓
PulseValueDecoderNode
 ↓
PreambleDetectorNode
 ↓
MessageAssemblerNode
```

where:

### HopMatchedFilterNode

Tracks energy across the known frequency set:

```cpp
vector<double> available_frequencies;
```

and produces:

```cpp
struct FrequencyBinObservation {
    uint64_t time_slot;
    uint32_t frequency_index;
    float amplitude;
    float phase;
};
```

---

### PulseValueDecoderNode

Consumes many observations over the pulse duration and outputs:

```cpp
struct DecodedPulse {
    uint64_t toa;
    uint64_t width;
    uint32_t value;        // 32-bit value
    uint32_t hop_frequency_index;
    float confidence;
};
```

---

### PreambleDetectorNode

Looks for:

```cpp
0xA5A5A5A5
0xDEADBEEF
0x12345678
```

(or whatever sequence)

and declares:

```cpp
StartOfMessage
```

---

### MessageAssemblerNode

Builds:

```cpp
struct Message {
    vector<DecodedPulse> preamble;
    vector<DecodedPulse> payload;
};
```

---

# In fact, I would argue **PDWs are unnecessary for this problem**.

PDWs are useful when:

* frequencies are unknown,
* emitters are unknown,
* multiple emitters overlap,
* you're doing ESM/SIGINT.

For a known hopping protocol, the receiver is better viewed as a specialized modem:

```text
IQ
 ↓
Synchronize
 ↓
Track hops
 ↓
Decode pulse values
 ↓
Find preamble
 ↓
Assemble messages
```

This is much closer to GPS, Bluetooth, LoRa, or FHSS modem design than to classical EW/PDW processing.

---

For GraphX, I would probably introduce an entirely new branch:

```text
IqPacket
 ↓
WindowNode
 ↓
FFT/Channelizer
 ↓
FHSSPulseDecoderNode
 ↓
PreambleDetectorNode
 ↓
MessageAssemblerNode
```

with **FHSSPulseDecoderNode** being the heart of the system and operating directly on IQ-derived spectral information rather than on PDWs. I think this will be both simpler and more robust than trying to force the problem through a PDW architecture.


This makes the architecture cleaner:

```text
Preamble = sequence of 32-bit-valued pulses
Payload  = sequence of 32-bit-valued pulses
Hop      = frequency assignment per pulse
Message  = ordered pulse train
```

The IQ generator’s job is then to synthesize the waveform for each pulse while preserving exact truth metadata for every `uint32_t` pulse value.
-------

