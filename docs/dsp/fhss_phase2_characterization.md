# FHSS Phase 2 engineering characterization

Status: **provisional engineering characterization only**. This is not product,
regulatory, interoperability, hardware, OTA, or production-RF qualification.

The machine-readable source of requirements and exact metric definitions is
`libdsp/config/fhss_phase2_validation_profile_v1.json`. The profile deliberately
separates the 1–1.504 GHz protocol RF table (metadata) from the representable
complex-IQ band. At 500 Msps only the profile's 64 distinct IQ offsets between
-236.25 and +236.25 MHz are evaluated.

Profile version: 1. Profile SHA-256:
`b19314e77a06cefacf6c440826ad5922e83fe0d0929511f086cc77c12cb249d5`.
Frozen threshold-set SHA-256:
`20f45ff5154e014b48d005e0ad43865f40850512f1bc0bbc0c72439191c49ca7`.
The checked report describes working-tree code built in C++26 (`-std=c++2c`)
and uses the profile's held-out evaluation seeds.

## Candidate implementations

- `FHSSProductionCandidateChannelizerNode`: deterministic CPU complex mixing,
  normalized 241-tap Hamming FIR, then 10:1 decimation. The 120-input-sample
  group delay is explicit in the sample-time map. State is retained only across
  globally contiguous packets and no synthetic tail is emitted at EOS.
- `FHSSAcquisitionPulseDetectorNode`: bounded-capture, arbitrary-epoch energy
  acquisition. It converts the 20th power percentile to the equivalent mean
  under the declared complex-Gaussian noise model, uses
  hysteresis and gap bridging, rejects invalid durations/coherence, suppresses
  duplicates, and emits detections at EOS. It receives no scheduled windows,
  message epoch, generator truth, or configured noise-floor measurement.

## Characterization summary

| Metric | Provisional target | CI evidence | Result |
|---|---:|---:|---|
| Passband ripple | ≤ 1.75 dB | 1.529371 dB | PASS |
| Stopband attenuation | ≥ 50 dB | 64.071989 dB | PASS |
| Adjacent rejection | ≥ 50 dB | 56.708048 dB | PASS |
| Alternate rejection | ≥ 50 dB | 94.261251 dB | PASS |
| Worst folding-band alias power | ≤ -50 dBc | -64.374949 dBc | PASS |
| Integrated folding-band alias power | ≤ -50 dBc | -76.038897 dBc | PASS |
| Candidate/oracle alias error | ≤ 1e-11 | 0 | PASS |
| Transition width | ≤ 9.5 MHz | 4.9 MHz | PASS |
| Equivalent noise bandwidth | 5–10 MHz | 6.333799 MHz | PASS |
| Channelizer allocation high-water | ≤ 16 MiB | 978,944 bytes | PASS |
| Group-delay error | ≤ 0.001 input sample | 0 | PASS |
| Near/far support | ≥ 30 dB | 30 dB | PASS |
| Packetized/one-shot error | ≤ 1e-11 | 0 | PASS |
| Detection probability at 10 dB SNR | 95% Wilson lower bound ≥ 0.95 | 1,000/1,000; [0.996173, 1] | PASS |
| False alarms per searched sample | 95% Wilson upper bound ≤ 1e-6 | 0/14,400,000; [0, 2.668e-7] | PASS |
| Timing error p95 | ≤ 160 input samples | 0 | PASS |
| Duplicate detections per truth pulse | 0 | 0 | PASS |
| Detector allocation high-water | ≤ 1 MiB | 72,440 bytes | PASS |

The compact CI suite also covers passband/transition/stopband responses from
the actual FIR kernel, adjacent and alternate tones, multitone and near/far vectors,
packet splits, unknown epochs, leading/trailing idle, multiple bursts,
partial-token boundaries, amplitude/phase/CFO/AWGN sweeps, wrong-rate and
non-pulse interferers, invalid/non-finite configuration, EOS, and plugin loading.

The checked report is reproduced with:

```console
cmake --build build-ninja/ninja-debug --target dsp_fhss_phase2_characterize
python3 examples/DSP/tools/characterize_fhss_phase2.py --trials 1000 --seed-partition evaluation --write
```

The C++ executable runs `FHSSFirChannelizerKernel`,
`FHSSAcquisitionPulseDetectorKernel`, and receiver-node contract vectors and
emits raw measurements. Python only launches that executable, computes the
Wilson intervals, and applies gates; it does not reimplement either candidate.
The evaluation searched both signal-bearing and noise-only captures, so every
searched channel sample is included in the false-detection denominator. The
deterministic 64x64 measured leakage matrix, measured SNR bias using
`(mean_power - noise_power) / noise_power`, node-produced frequency confusion
matrix, terminal/reset contract, latency convention, and actual candidate
runtime are in the JSON report.

Alias measurements cover positive and negative tones in every decimation
folding region through the 250 MHz input Nyquist: 12–25, 25–75, 75–125,
125–175, 175–225, and 225–250 MHz by absolute frequency. Each raw entry
contains input power, candidate post-filter/decimated power, independent
FIR/decimation-oracle power, and sample error. One-tone-per-band and dense
96-tone stopband vectors exercise simultaneous folding. Transition width is
the measured candidate response distance from the 2.5 MHz passband edge to the
first 50 dB stopband crossing. ENBW is derived from the actual candidate
impulse-response energy and DC gain.

Memory figures are observed deterministic allocation high-water counters, not
configured buffer limits or process-RSS estimates. The channelizer counter
includes the selected input copy, all 64 FIR histories, and all 64 output
vectors. The detector counter includes capture state, power/sorted/smoothed
work vectors, candidate ranges, and emitted detection/evidence vectors.

The filter and detector operating points were selected from architecture
constraints, analytical design bounds, and development seeds 1101–1104. Their
canonical threshold set was serialized and hashed before evaluation. Held-out
evaluation seeds 91001–91008 are not used to select thresholds; changing a
threshold or its provenance changes the hash and invalidates the checked
evaluation report.
Full Phase 3 impairment Monte Carlo remains deferred. Large IQ vectors and
minimized failures remain external artifacts identified by SHA-256.

The truth-free full-graph integration point uses a unit-amplitude independent
capture and a 0.05 linear-power acquisition floor. It qualifies channel
identity, pulse timing propagation, EOS, and downstream graph completion. It
does not qualify exact payload-word recovery after the causal channel filter;
that requires the later matched-filter/equalization validation.

## Known limitations

The reusable acquisition detector candidate buffers at most 4,194,304 channel
samples per capture. The deliberately smaller full-graph integration topology
uses 419,431 channel samples so one 4,194,304-input-sample file token remains
within the post-decimation bound. The detector reports at
EOS; it is not an unbounded low-latency streaming detector. The CPU filter bank
does not claim accelerator parity. Same-channel overlapping energy bursts are
not separated. Exact payload-word accuracy through the causal channel filter is
not yet qualified. The numerical thresholds are provisional and must be
replaced by approved product requirements before any qualification claim.
