# FHSS Phase 3 Simulation Characterization

## Status and claim boundary

Phase 3 v7 is the current passing engineering-simulation characterization. Its
single frozen held-out run completed all 162 cases at all 43 matrix points with
no timeout, nonzero status, retry, seed substitution, or missing allocation
evidence. All five predeclared Wilson-bound gates and both machine gates passed.
This is not product, regulatory, interoperability, hardware, conducted, OTA,
or production-RF qualification.

Earlier evidence remains immutable and must be interpreted historically:

- v2 and v3 are invalidated because independent review found incorrect
  impairment order, noise calibration, conditional-error denominators, message
  statistics, truth timing, provenance, allocation evidence, and schema
  enforcement.
- v4 is invalidated because case 117 of 162 exposed missing receiver terminal
  propagation; a graph could wait forever when a lane ended without pulse
  evidence.
- v5 is a machine FAIL with four retained execution failures: `ebn0_30db`
  seed 36118 timed out, `doppler_pos5khz` seed 36102 returned status 2,
  `alternate_blocker_6db` seed 36101 timed out, and
  `cochannel_fhss_sir_neg6_t6500` seed 36102 timed out.
- v6 is a machine FAIL because `cochannel_fhss_sir_12_t6500` seed 38101
  exceeded the frozen 150-second process guard without producing a summary.
  Its five statistical gates passed, but a statistical pass cannot override an
  execution-machine failure.

The v7 independent tool imports no GraphX or libdsp waveform-generator or
receiver helper. It gives the message-free Phase 2 receiver only raw binary IQ
and receiver configuration. The receiver graph contains no `messages`,
transmit schedule, expected words, generator truth, or synthetic IQ source;
truth is opened only after receiver execution for scoring.

## Signal and noise definitions

The wanted reference `Pactive` is the mean wanted-transmitter power over active
wanted samples after timing, TDL, and fading, but before blockers, hardware, and
noise. Blocker SIR and AWGN independently reference this frozen power, so a
blocker never raises the noise reference.

For the one-bit-per-symbol complex-baseband CPSM waveform:

```text
Eb = Es = Pactive / Rb
E{|n[k]|^2} = N0 Fs
sample SNR = Pactive / E{|n[k]|^2}
Eb/N0 = sample SNR * Fs/Rb = sample SNR * 100
```

The two real Gaussian components each have variance
`E{|n[k]|^2}/2`. Units are recorded as watts, joules, watts/hertz, hertz,
samples, seconds, decibels, or dimensionless ratios as applicable.

The executable impairment sequence is frozen and composition-tested:

1. fractional timing and sample-clock resampling;
2. normalized fractional TDL and fading;
3. Doppler, CFO, initial phase, and phase noise;
4. blockers and other FHSS transmitters;
5. IQ imbalance and DC;
6. AGC transient;
7. complex AWGN;
8. clipping;
9. quantization.

## Metrics and denominators

Matching is one-to-one within frozen time/frequency gates. Misses, false
detections, duplicates, ambiguous same-time/same-word transmitter associations,
and temporal/same-hop collisions remain separate. Conditional BER/WER use only
decoded associated words; a miss is not converted into 32 bit errors. PER uses
unique `(message_id, transmitter_id)` messages. Collision rows report each
transmitter's pulse and complete-message capture probability.

Rows include Pd, conditional BER/WER, per-message PER, FAR per searched channel
sample, frequency confusion, timing RMSE and absolute-error p50/p95/p99/max,
CFO error, confidence calibration, runtime, randomized epoch range, and actual
allocation high-water. Missing channelizer or detector allocation diagnostics
is a machine failure; no arbitrary allocation-byte pass threshold is invented.

## Frozen v7 design and result

The 43-point matrix contains a genuine active-symbol Eb/N0 curve at
10/15/20/25/30 dB; CFO; sample-clock error; fractional timing; TDL; static
Rayleigh/Rician and time-varying sum-of-sinusoids Rayleigh fading; Doppler;
adjacent/alternate/cochannel tones; phase noise; hardware impairments; and a
same-hop FHSS capture grid at SIR -6/0/6/12 dB and relative starts
0/6500/13000 samples. Every seed randomizes the burst epoch in [20000,80000]
samples.

The 30 dB Eb/N0 gate is physically the invalidated v3 10 dB sample-SNR point,
because `Fs/Rb=100`; only the units and calibration are corrected. The clean
PER target is tightened from the invalid v3 value 0.50 to 0.10: a provisional
clean receiver should completely and correctly deliver at least 90 percent of
messages. Forty independently seeded gated messages make 0/40 errors a Wilson
95 percent upper bound of about 0.0876, so the target is testable.

Two fresh seeds cover every matrix point and 38 additional predeclared seeds
extend only clean and 30 dB Eb/N0, for 40 messages at each gate. Gates use the
lower Wilson bound for Pd and upper bounds for conditional BER, PER, and FAR.
No threshold, seed, or point may change after the profile hash is frozen.

The v7 evaluation seeds are 39101 and 39102. The predeclared gate-extension
seeds are 39103 through 39140 and apply only to `clean_60db` and `ebn0_30db`.
All are disjoint from v2 through v6. The evaluator executed exactly 162 cases
once, journaled every case durably, and did not retry or substitute any case.

The verified v7 result is:

- 162 of 162 executions completed and 43 of 43 matrix rows are present;
- the allocation-evidence machine gate passed;
- the all-receiver-executions machine gate passed with zero failures;
- clean Pd lower Wilson bound: `0.9943825300862589 >= 0.95`;
- clean conditional BER upper Wilson bound:
  `0.00017650646959373138 <= 0.01`;
- clean message error-rate upper Wilson bound:
  `0.08762160119728664 <= 0.10`;
- 30 dB Eb/N0 Pd lower Wilson bound: `0.9943825300862589 >= 0.90`;
- clean false alarms per searched channel sample upper Wilson bound:
  `8.627719954915304e-8 <= 1e-6`.

## Provenance and reproducibility

Every raw case binds the receiver executable, receiver graph, complete sorted
plugin set, effective receiver configuration, exact command/return status,
stdout/stderr, compiler version, compile-command database, verified C++26 mode,
git HEAD/dirty-state hash, independent tool, profile, scenario, and IQ with
SHA-256. The raw case array has its own integrity hash. Verification rejects
tampering, nonzero receiver status, missing provenance, non-C++26 evidence, or
non-reproducible aggregate bytes.

IQ and truth outputs are separate and transactional. Generation refuses
existing paths, rejects unknown/non-finite/out-of-range data using an internal
strict validator, atomically creates both files without overwrite, and rolls
back IQ if truth commit fails.

The frozen v7 bindings are:

- profile file SHA-256:
  `3a9b8d332d78f6f6b0de6f464047e5e5a3012b34abdfb78d66db416953e14a83`;
- canonical profile SHA-256:
  `709df93e2d829a39730dbdc062485c1e37b71c32c6bf865c890e2473a062c5bf`;
- frozen section SHA-256:
  `a4aa258dd5461fb7d3e4e51e016e9a827040e226db1c227bea760153c4140ee4`;
- evaluator SHA-256:
  `65283b6d80c6179ce329f97c6b5a7638107ed360ef2bd87590ad29f33099ae8a`;
- receiver executable SHA-256:
  `a52a78d98fd744e68feeff89c4ed57dba27d2bbeca9a03ec3b9fdbb21143336b`;
- receiver graph SHA-256:
  `672cc99fe1776e608d05972ab1fdc9873ada9d3f0ba43285d5000e9a257c2ae8`;
- complete plugin-set SHA-256:
  `1a8f56259bebfcb03a718fa4684f3a043889857d26387b908b0f18ef6ca5e94f`;
- raw case-array SHA-256:
  `12cc70464ac2844cd84532b44ec8834953147aa5cb3edef7a0ada904952374cd`;
- raw file SHA-256:
  `70ec33d427e2c1bb4296382de5c059b2aa6723432f81a55e04ba791de5af6240`;
- report file SHA-256:
  `e5c5f7b18cc917aab8a3faea794b70a0d407d1c2a5b679b6905f2ef0f91339d1`.

The receiver and plugins were built with `-std=c++2c`. Regression evidence
before the held-out run covered all 174 enumerated libdsp tests (169 passed and
five explicitly skipped because native Metal was unavailable), all 56 DSP
example tests, 28 passing Phase 3 Python tests with one intentional skip, and
both registered Phase 3 CTests. Validation-only serial pilots passed five of
five historical shapes, and two-way concurrent stress passed ten of ten
executions before the fresh partition was exposed.

## Development-only findings and limitations

The corrected validation pilot measured 83/544 conditional bit errors and a
message failure at 10 dB Eb/N0, while 15--40 dB pilot points decoded cleanly.
These validation seeds are excluded from held-out evaluation.

The supported whole-hop cochannel grid completed all 12 v7 held-out points.
Partial-hop same-channel starts at 800 samples timed out twice in historical
pre-circular-FIR development evidence; 1600 samples remains in that deferred
risk class and was not separately executed after the repeated t800 failure.
Neither point was revalidated after the circular-FIR fix. Both therefore remain
conservatively `unsupported/deferred` failures with no passing claim in
`libdsp/config/fhss_phase3_validation_inventory_v7.json`. No fresh v7 held-out
seed was spent on either unsupported point.

Other limitations:

- CFO diagnostics are dominated by CPSM data phase and are not qualified.
- Decoder confidence is not calibrated as probability.
- Non-gated rows have only two held-out messages, so low rates remain unresolved.
- The time-varying fading option is a finite engineering sum of sinusoids, not a
  standardized deployment profile; composite phase can rotate rapidly near a
  fading null even though every component is inside the declared Doppler support.
- Phase noise and hardware effects are synthetic, not measured front-end data.
- The GraphExecutor timeout path requests stop and then performs an unbounded
  join; active `Consume()` work has no general cooperative-interruption API.
  The circular FIR removed the observed pathological workload, but the generic
  lifecycle limitation remains.

The primary-source manifest is
`docs/dsp/fhss_phase3_reference_manifest_v2.json`. ETSI EN 300 328 V2.2.2
clauses 4.3.1.12 and 5.4.11 motivate recorded wanted/blocker combinations only;
the regulatory bands, levels, apparatus, and conformance claim are not imported.

## Verification and destructive reproduction warning

Routine verification is read-only and reproduces the report byte-for-byte from
the frozen profile and raw journal:

```sh
python3 examples/DSP/tools/fhss_phase3_independent_v7.py verify \
  --profile libdsp/config/fhss_phase3_validation_profile_v7.json \
  --raw libdsp/config/fhss_phase3_evaluation_raw_v7.json \
  --report libdsp/config/fhss_phase3_characterization_report_v7.json
```

Do not use held-out evaluation as a routine reproduction command. It generates
fresh IQ, executes the receiver, and writes the raw journal and report paths;
rerunning the frozen v7 seeds would destroy their held-out status and is not a
valid substitute for the sealed one-shot evidence. The exact historical command
shown below is recorded only for provenance and must target new output paths if
explicitly authorized for an investigation:

```sh
python3 examples/DSP/tools/fhss_phase3_independent_v7.py evaluate \
  --profile libdsp/config/fhss_phase3_validation_profile_v7.json \
  --receiver build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-fhss-demo \
  --graph libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --plugins build-ninja/ninja-debug-metal-native/plugins \
  --partition evaluation \
  --raw AUTHORIZED_NEW_RAW_PATH.json \
  --report AUTHORIZED_NEW_REPORT_PATH.json
```

Temporary IQ, truth, and effective configs are removed after each run. Large IQ
captures are not checked into the repository.
