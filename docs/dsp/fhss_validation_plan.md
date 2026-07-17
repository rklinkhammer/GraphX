# FHSS Receiver Validation Implementation Plan

## 1. Objective

Build a validation program that distinguishes four independent questions:

1. Are individual DSP nodes mathematically correct?
2. Are packet contracts, timing maps, and graph scheduling correct?
3. Does the receiver recover independently generated FHSS/CPM signals under
   controlled impairments?
4. Does performance measured with captured IQ agree with simulation and the
   declared operating requirements?

The existing deterministic fixture tests remain the first layer. They are not
removed or relabeled as performance tests. New evidence is added in isolated
node, short-chain, full-chain Monte Carlo, and recorded-IQ layers.

## 2. Required claims and non-claims

Before setting numerical thresholds, record the intended waveform and product
requirements in a versioned validation profile:

- RF band and legal/regulatory domain.
- Frequency table, usable hop set, hop selection algorithm, dwell time, and
  channel occupancy rules.
- CPM definition: alphabet, modulation index, frequency pulse, symbol order,
  initial/terminal phase policy, spectral mask, and occupied bandwidth.
- Packet and preamble definition, coding/CRC behavior, maximum message length,
  and missing-pulse policy.
- Receiver input bandwidth, sample format, nominal sample rate, oscillator
  tolerance, noise figure, dynamic range, and expected blockers.
- Target environments, speed/range limits, delay-spread limits, and antenna
  assumptions.
- Required probability of detection, false-alarm rate, BER/PER, latency, and
  resource limits.

Until this profile exists, results are engineering characterization, not
regulatory or interoperability conformance.

## 3. Test-layer architecture

### Layer A: deterministic unit fixtures

Purpose: fast mathematical and contract regression.

- Preserve existing clean generator/oracle tests.
- Require exact equality where floating-point formulation permits it.
- Run on every supported compiler and backend.
- Do not use results from this layer as receiver sensitivity evidence.

### Layer B: isolated-node characterization

Purpose: measure each node independently with inputs that do not originate from
the node under test or from a shared implementation helper.

- Use analytical vectors for simple transforms.
- Use a separately implemented Python, MATLAB, or GNU Radio oracle for DSP
  vectors.
- Store vector provenance, generator version, parameters, and hashes.
- Test valid, boundary, malformed, and adversarial inputs.
- Measure error, not only success/failure.

### Layer C: short-chain integration

Purpose: localize failures while testing interactions such as filtering plus
detection or synchronization plus decoding.

Recommended chains:

1. File source -> downconverter -> capture sink.
2. Downconverter -> channelizer -> one selected channel capture.
3. Channelizer -> one detector.
4. Detector -> merge -> candidate.
5. Candidate -> branch metric -> Viterbi -> word decode.
6. Word decode -> preamble detector -> assembler.

### Layer D: full-chain statistical simulation

Purpose: produce detection, BER, and PER curves across a declared impairment
space using independently generated randomized packets.

### Layer E: recorded-IQ replay

Purpose: validate against data with independent transmitter and receiver clocks,
front-end noise, quantization, AGC behavior, and real propagation.

### Layer F: conducted and OTA/HIL qualification

Purpose: validate the complete system with calibrated signal generators,
attenuators, channel emulators, blockers, and representative radios.

## 4. Isolated-node implementation matrix

### 4.1 `FHSSBinaryIqFileSourceNode`

Implement and test:

- `cf32_le` and `cf64_le` decoding with exact known byte patterns.
- Empty file and one-sample file.
- File length not divisible by the complex-sample width.
- Missing, unreadable, truncated, and concurrently replaced files.
- `first_complex_sample` at zero, at EOF, and past EOF.
- `max_complex_samples` below, equal to, and above available samples.
- Global start sample and sample-rate metadata propagation.
- EOS behavior and repeated `Produce()` behavior.
- Large-file overflow checks and bounded allocation behavior.
- Relative and absolute path behavior from the graph execution directory.
- Cross-platform golden hashes for decoded sample vectors.

Acceptance gate: byte-accurate decoding, no partial-complex reads, no unchecked
integer conversion, and identical semantic output on supported hosts.

### 4.2 `FHSSDownconverterNode`

Vectors:

- DC, positive tone, negative tone, multitone, chirp, and complex noise.
- Zero translation and translations near DC, hop centers, and Nyquist.
- Nonzero global sample origins to verify phase continuity across packets.
- Packet splits at multiple boundaries; concatenated output must match a
  one-packet reference result.
- Random initial phase and amplitude.
- Invalid frame declarations and non-finite parameters.

Metrics:

- Maximum complex-sample error against an independent NCO oracle.
- Residual tone frequency and phase discontinuity at packet boundaries.

Acceptance gate: declared phase convention and sample-time map remain correct
for all packetizations.

### 4.3 channelizer/filter bank

Replace or supplement the fixture mixer/decimator with a production candidate
before making selectivity claims.

Isolated tests:

- Impulse response and group delay.
- Passband gain/ripple and phase linearity.
- Transition width and stop-band rejection.
- Every channel center, both band edges, and reserved channels.
- Adjacent-channel and alternate-channel tones.
- Broadband noise and multitone alias tests before and after decimation.
- Two simultaneous channels with 0 to at least the declared near/far power
  range.
- Packet-boundary state retention and filter warm-up/flush behavior.
- Comparison with an independent FIR/PFB reference.

Metrics:

- Passband ripple, stop-band attenuation, equivalent noise bandwidth, alias
  power, group-delay error, channel leakage matrix, and runtime.

Acceptance gate: requirements are expressed numerically and verified for every
output channel; mixing-only output is never reported as production
channelization.

### 4.4 `PerChannelPulseDetectorNode`

Split testing into the present scheduled-window detector and a future acquisition
detector. Keep their claims separate.

Scheduled-window isolated tests:

- Pulse at every integer offset across the window.
- Fractional-symbol timing offsets created by resampling.
- Partial pulses at packet boundaries.
- Noise-only, tone-only, wrong-modulation, and wrong-symbol-rate inputs.
- Random CPM words rather than only preamble words.
- Amplitude, AWGN, CFO, phase, Doppler, and multipath sweeps.
- Adjacent-channel leakage and same-channel interferers.
- Threshold boundary and non-finite configuration tests.

Acquisition-detector tests:

- Unknown burst epoch across at least one pulse period.
- Multiple bursts, arbitrary idle length, and noise-only captures.
- False detections from CW, impulsive noise, and unrelated modulations.
- Timing-estimation error and duplicate-detection suppression.

Metrics:

- Probability of detection, false alarms per second or per searched sample,
  timing-error distribution, frequency-index confusion, SNR-estimation bias,
  and confidence calibration.

Acceptance gate: thresholds are selected on a training dataset and evaluated on
a held-out dataset; the configured `noise_floor_db` is not accepted as a noise
measurement.

### 4.5 `FHSSPulseMergeNode` and candidate boundary

Isolated tests:

- Same pulse reported by multiple detectors with timing/frequency disagreement.
- Same-frequency collisions and cross-frequency simultaneous pulses.
- Near/far candidates, equal confidence, and deterministic tie-breaking.
- Out-of-order input ports, missing terminal inputs, duplicated EOS, failure,
  and cancellation.
- Timing-map combinations with decimation and filter group delay.
- Large candidate counts and worst-case overlap graphs.

Metrics:

- Association precision/recall, duplicate-removal accuracy, timing error after
  normalization, deterministic ordering, and memory/runtime scaling.

Acceptance gate: overlap policy is explicit. Unsupported overlap must return a
diagnostic rather than silently discarding valid evidence.

### 4.6 `CPSMBranchMetricKernel`

Isolated tests:

- Analytical single-symbol vectors for every phase state and input symbol.
- Full trellis transition table generated independently.
- Known amplitude and phase rotations.
- AWGN sweeps with and without amplitude normalization.
- CFO, timing offset, pulse truncation, and multipath distortion.
- Zero samples, denormals, NaN/Inf, and mixed valid/invalid evidence.

Metrics:

- Branch-cost error against a coherent likelihood oracle.
- Correct-branch ranking probability versus Eb/N0 and impairment.

Acceptance gate: document whether metrics are true log likelihoods or normalized
correlations; confidence must not be interpreted as probability until
calibrated.

### 4.7 `CPSMViterbiDecoderKernel`

Isolated tests:

- Exhaustive words for short symbol counts and randomized 32-bit words.
- Every initial state and terminal-state policy.
- Ties, nearly tied paths, saturated costs, and invalid trellises.
- Compare traceback and final metric with a separately implemented exhaustive
  ML decoder for tractable lengths.
- AWGN BER curves against the independent CPM reference.

Metrics:

- Symbol/word error rate, path-metric error, second-best separation, confidence
  reliability diagram, and runtime per pulse.

Acceptance gate: clean-vector parity plus statistically plausible BER trend and
independent-oracle agreement.

### 4.8 `FHSSPulseWordDecoderNode`

Isolated tests:

- MSB-first mapping for structured patterns and randomized words.
- Confidence threshold just below/equal/above the boundary.
- Invalid symbol values, symbol counts, metrics, and metadata.
- Preservation of timing, frequency, status, and evidence identity.

Acceptance gate: no waveform assumptions beyond the documented symbol contract.

### 4.9 preamble detector

Isolated tests:

- Preamble at every position in longer decoded streams, not only index zero.
- Partial preambles at both ends.
- One through multiple hop errors and word errors.
- Random traffic and adversarial near-preamble sequences.
- Repeated messages, false locks, and reacquisition after a failed message.
- Missing pulses and extra pulses within the preamble.

Metrics:

- Acquisition probability, false locks per decoded pulse, lock-position error,
  and reacquisition latency.

Acceptance gate: support or explicitly reject search, erasure tolerance, and
approximate matching; a first-16-pulses comparison is classified as fixture
behavior.

### 4.10 message assembler

Isolated tests:

- Minimal receiver-only configuration containing `preamble_pulses` but neither
  `active_frequency_indices` nor `messages`; both the preamble detector and
  assembler must configure successfully.
- Parser-contract test proving `FHSSMessageAssemblerConfigFromJson` reads only
  fields represented by `FHSSMessageAssemblerConfig` instead of routing through
  the broader `FHSSDecodeConfigFromJson` fixture parser.
- Derive the locked active-frequency set from the distinct frequency indices in
  the configured preamble, then validate payload membership against that
  runtime-derived set.
- Reject an invalid preamble-derived active set without accepting a separately
  configured active set as an override.
- Graph-instantiation regression using the binary-IQ topology after removing
  the redundant assembler and preamble-node `active_frequency_indices` fields.
- Multiple messages in one stream and messages split across tokens.
- Missing, duplicated, late, reordered, and corrupt pulses.
- Maximum message length and back-to-back preambles.
- Message timeout and state reset.
- Overlapping transmitters with unique and reused message identifiers.
- CRC/FEC outcomes when those protocol features are defined.

Metrics:

- Message precision/recall, PER, truncation rate, association errors, and
  assembly latency.

Acceptance gate: assembler state must not depend on generator truth or a
predeclared message schedule. Receiver-node parsers accept their minimal
documented configuration, and the active-frequency set used for payload
validation is derived from a successfully locked preamble rather than required
as redundant JSON input.

### 4.11 sink and diagnostics

Isolated tests:

- Success, invalid evidence, timeout, cancellation, and partial-message output.
- Schema versioning and required/optional fields.
- Large diagnostics payloads and JSON numeric boundaries.
- Consistency between counters and emitted records.
- Distinguish configured, estimated, and truth-only quantities.

Acceptance gate: diagnostics never label a configured noise floor as measured
SNR and never count dropped truth pulses as received pulses.

## 5. FHSS message-to-IQ generation

### 5.1 architecture-conformant generator application

Add a command-line application, `graphx-dsp-fhss-iq-generator`, under
`examples/DSP`. Its purpose is to turn a message schedule created by
`fhss_message_tool.py` into the binary IQ consumed by
`FHSSBinaryIqFileSourceNode`. This is a concrete producer for replay datasets,
not merely a test helper.

The application shall use `FHSSSyntheticIqGeneratorConfigFromJson` and
`GenerateSyntheticIqFixture` as the canonical implementation of the generation
rules documented in `fhss_architecture.md`. It must not run the receiver graph
or copy decoded receiver output into its truth data.

Command-line contract:

```text
graphx-dsp-fhss-iq-generator \
  --message-json schedule.json \
  --iq-output capture.cf32 \
  --truth-output capture.truth.json \
  [--sigmf-meta capture.sigmf-meta] \
  [--sample-format cf32_le|cf64_le]
```

Required behavior:

- Accept the existing source/message JSON shape: four active selectable
  frequencies, IQ center or explicit IQ offsets, scheduled messages, idle
  duration, overlap policy, feature flags, and optional realistic model.
- Enforce the architecture timing profile: 500 Msps, 5 Mbps, 100 samples per
  symbol, 32 bits per pulse, 3200 pulse samples, 3300 zero-gap samples, and a
  6500-sample pulse period.
- Require 16 preamble pulses and at most 256 total pulses per message; require
  preamble/body roles, active-set membership, and consistent preamble word
  values for repeated preamble frequencies.
- Place pulse `i` at
  `transmit_start_sample + i * 6500`, size the output to the maximum scheduled
  message end or idle duration, and represent unused samples as complex zero.
- Derive IQ offsets as `rf_frequency_hz - iq_center_frequency_hz` when a center
  frequency is supplied, and enforce finite, distinct, guarded-Nyquist offsets.
- Map each 32-bit word MSB-first (`0 -> +1`, `1 -> -1`) and synthesize the
  documented binary CPSM waveform with modulation index `h = 1/2`, rectangular
  full-response phase pulse, 100 samples per symbol, continuous accumulated
  phase within each pulse, and initial phase zero.
- Add complex samples when explicitly permitted messages overlap; otherwise
  reject overlap. Reject unsupported noise and multipath flags, and permit
  Doppler only through the documented realistic motion model.
- Apply the documented realistic overlay when enabled: seeded missing-pulse
  decisions, linearly interpolated transmitter paths, propagation delay, timing
  jitter, path-loss amplitude, and radial-velocity Doppler.
- Write raw interleaved little-endian IQ in the selected format. Write optional
  SigMF metadata containing at least datatype, sample rate, center frequency,
  sample count, and generator/version provenance.
- Write a separate truth manifest containing the architecture's nominal and
  received pulse starts, duration, frequency index, RF and IQ-offset frequency,
  Doppler, delay, range, amplitude, dropped state, word, role, and message id,
  plus input/output SHA-256 hashes. Truth must never be embedded in the raw IQ
  file or required by the receiver graph.
- Refuse to overwrite an existing output unless an explicit `--force` option is
  supplied, remove partial outputs after failure, and return a nonzero exit code
  with a stable diagnostic category for invalid configuration or I/O failure.

Application tests:

- CLI help, required arguments, invalid JSON, invalid output paths, overwrite
  protection, and deterministic repeatability.
- Byte-for-byte `cf32_le` and `cf64_le` output checks for analytical short
  schedules.
- Exact sample count, zero gaps, pulse placement, bit/symbol order, hop
  frequency, amplitude, phase continuity, overlap summation, and truth-manifest
  checks.
- Negative tests for every generator validation rule in
  `fhss_architecture.md`, including unsupported feature flags.
- Round-trip test: generate IQ, point only the binary replay graph's
  `file_path` at it, run the receiver, and compare receiver results with the
  separately stored truth manifest.

Acceptance gate: the application reproducibly creates a self-describing binary
capture from message JSON, all architecture rules are traceable to tests, and
the message-free receiver graph consumes the IQ without access to the generator
configuration or truth manifest.

### 5.2 independent waveform oracle and channel harness

Create a second tool outside `libdsp` that emits binary IQ plus a truth
manifest. It must not call GraphX generator or decoder helpers. Its clean mode
independently implements the waveform equations in `fhss_architecture.md` and
is used to detect shared defects in the canonical generator application and
receiver.

Inputs:

- Validation profile and seed.
- Randomized messages and hopping sequences.
- CPM parameters and burst timing.
- Channel and hardware-impairment scenario.

Outputs:

- `cf32_le` or `cf64_le` IQ.
- SigMF metadata where appropriate.
- Truth JSON containing transmit and receive event times, hop index, word,
  channel taps, CFO, SNR/Eb/N0, and seed.
- SHA-256 hashes and generator version.

Minimum channel set:

1. Clean reference.
2. AWGN with Eb/N0 sweep.
3. Static CFO and random phase.
4. Sample-clock and fractional timing error.
5. Tapped-delay-line Rayleigh and Rician fading.
6. Time-varying Doppler spectrum.
7. Adjacent- and co-channel blockers.
8. Independent overlapping FHSS transmitters.
9. IQ imbalance, DC, clipping, quantization, phase noise, and AGC transient.
10. Combinations selected through pairwise coverage plus targeted worst cases.

Reference model choices and versions must be recorded. Suitable baselines include
3GPP/ETSI tapped-delay-line methodology, ITU-R free-space loss, and GNU Radio's
channel-model impairment vocabulary. These are engineering references unless the
declared product standard makes them normative.

## 6. Statistical methodology

For each operating point:

- Randomize payload, phase, burst epoch, and channel realization.
- Use multiple explicit seeds and record them.
- Continue until both a minimum trial count and a minimum observed-error count
  are satisfied, or report that the target error rate was not resolved.
- Report a binomial confidence interval for detection and PER.
- Keep threshold-development data separate from final evaluation data.
- Preserve failing IQ windows and manifests as minimized regression fixtures.

Required curves:

- Detection probability and false-alarm rate versus SNR.
- BER and word/message PER versus Eb/N0.
- PER versus CFO and clock offset at several SNRs.
- PER versus delay spread and Doppler.
- Capture probability versus signal-to-interferer ratio and relative timing.
- Adjacent-channel rejection/blocking versus offset and blocker level.
- Timing and frequency-estimation RMSE.

## 7. Recorded-data program

### Conducted captures

- Independent TX/RX oscillators through cable and programmable attenuation.
- Calibrated signal power sweep.
- Separate wanted source and blocker source.
- Clock/CFO measurements captured as metadata.

### Channel-emulator captures

- Reproduce selected AWGN, TDL, Doppler, and fading matrix points.
- Compare simulator and hardware results using the same acceptance metrics.

### OTA captures

- Static LOS, obstructed LOS, indoor multipath, outdoor mobile, and near/far
  interferer scenarios.
- Record antennas, geometry, gain settings, temperature, equipment identifiers,
  firmware, and calibration date.

### Dataset governance

- Immutable manifest and hashes.
- Training, validation, and held-out partitions by capture session—not by
  slicing adjacent samples from the same session.
- License and privacy review.
- Small CI subset plus complete offline corpus.

## 8. CI and execution tiers

### Per-commit, target under five minutes

- Existing deterministic suite.
- Generator-application CLI, golden-byte, architecture-rule, and deterministic
  replay tests.
- All isolated-node analytical vectors.
- Small independent golden-vector set.
- JSON schema/configuration checks.
- Sanitizer-compatible malformed-input tests.

### Nightly

- Moderate Monte Carlo matrix.
- Cross-compiler and CPU/backend parity.
- Short recorded-IQ replay set.
- Performance-regression checks.

### Weekly or release qualification

- Full Monte Carlo curves with confidence intervals.
- Complete recorded-IQ corpus.
- Long-duration false-alarm/noise-only runs.
- Blocking, collision, and stress matrices.
- Reproducible HTML/JSON report and archived artifacts.

No statistical test should fail solely because one random run crossed a hard
threshold. Use fixed evaluation seeds, adequate sample counts, confidence bounds,
and an explicit policy for trend regressions.

## 9. Implementation sequence and gates

### Phase 0: requirements and evidence schema

Deliver:

- Versioned validation profile.
- Metric definitions and acceptance thresholds.
- IQ/truth manifest schema.
- Dataset and seed policy.

Gate: every claimed performance property has a measurable definition.

### Phase 1: file replay and isolated-node foundation

Deliver:

- `graphx-dsp-fhss-iq-generator` with message-JSON input, raw binary IQ,
  optional SigMF metadata, and separate truth-manifest output.
- Binary IQ source and message-free replay graph.
- Independent clean vectors.
- Isolated tests for source, downconverter, branch metric, Viterbi, word
  decoder, preamble, assembler, and diagnostics.
- Decoupled preamble/assembler JSON parsing so receiver-only nodes do not
  require `active_frequency_indices` through the full decode-fixture parser.

Gate: generated IQ is byte-correct and conforms to every generation rule in
`fhss_architecture.md`; clean independent vectors decode; and the message-free
graph instantiates after redundant receiver-node `active_frequency_indices`
fields are removed.

### Phase 2: channelizer and detector characterization

Deliver:

- Production-candidate filter bank.
- Acquisition-capable detector or an explicit scheduled-window-only boundary.
- Channelizer leakage/selectivity report.
- Detector ROC and timing-error report.

Gate: filter and detection requirements pass independently.

### Phase 3: impairment harness and full-chain Monte Carlo

Deliver:

- Independent waveform/channel generator and parity report against the
  architecture-conformant generator application's clean mode.
- AWGN, CFO, clock, multipath, fading, Doppler, and collision matrices.
- BER/PER/ROC reports with confidence intervals.

Gate: declared simulation thresholds pass without using shared truth logic.

### Phase 4: recorded IQ and HIL

Deliver:

- Conducted, channel-emulator, and OTA datasets.
- Held-out replay results.
- Simulation-to-hardware correlation report.

Gate: held-out capture performance satisfies the validation profile.

### Phase 5: qualification and maintenance

Deliver:

- Release qualification report.
- Traceability from requirement to test and artifact.
- Curated regression corpus containing minimized field failures.

Gate: no production-RF claim is made without a corresponding independent and
recorded-data result.

## 10. Immediate repository deliverables

- `graphx-dsp-fhss-iq-generator`, built by the DSP example CMake project, with
  architecture-conformant schedule validation, CPSM synthesis, binary IQ,
  optional SigMF metadata, truth-manifest output, and executable-level tests.
- `FHSSBinaryIqFileSourceNode` with `cf32_le` and `cf64_le` input.
- `fhss_cpsm_binary_iq_500msps.json`, containing no `messages` fields.
- Unit tests for file decoding, selection, metadata, and EOS.
- Refactor `FHSSPreamblePulseSpecsFromJson` and
  `FHSSMessageAssemblerConfigFromJson` to parse `preamble_pulses` directly;
  remove the redundant active-frequency fields from the binary-IQ graph and add
  minimal-configuration and graph-instantiation regressions.
- Follow-up graph-execution test that generates a temporary independent binary
  vector, patches only `file_path`, executes the graph, and checks receiver
  output without invoking `GenerateSyntheticIqFixture` for its oracle.
