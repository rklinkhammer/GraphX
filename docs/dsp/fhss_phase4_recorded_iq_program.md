# FHSS Phase 4 recorded-IQ and HIL program

## Outcome

Phase 4 physical validation is **UNAVAILABLE/DEFERRED**, not passed. The user
confirmed that no hardware-in-loop, conducted, channel-emulator, or OTA testing
is available. Phase 4 test data is therefore synthetic dry-run data only. The
repository contains governed infrastructure, but zero independent physical
captures, transmitter logs, equipment/calibration records, RF authorizations,
held-out hardware replays, or hardware paired points. No Phase 3 v7 or other
synthetic signal is relabeled as recorded evidence. No RF or hardware action was
performed.

This work is infrastructure readiness only. It is not product, regulatory,
certification, interoperability, hardware, OTA, or production-RF qualification.

## Evidence boundary

Large IQ remains outside Git. A collection manifest contains only relative
paths, immutable hashes, byte counts, formats, physical-session partitions,
equipment/calibration references, rights, and metadata. The operator supplies
the corpus root explicitly. Missing files, mismatched hashes, partial complex
samples, unsupported formats, unknown fields, non-finite values, cross-session
partition leakage, or missing held-out calibration fail closed.

The only IQ used by the automated tests is synthetic data, including four
literal floating-point values created inside a temporary directory. Every such
fixture has `synthetic_dry_run` provenance and class. Validation rejects an
attempt to promote that provenance to conducted, emulator, OTA, or physical
negative-control evidence. Synthetic fixtures are not recorded, HIL, hardware,
or performance evidence and never contribute to the physical readiness counts.

The replay sequence is:

1. Validate and hash-bind the frozen profile, collection, receiver executable,
   truth-free graph, plugins, and external IQ.
2. Patch only the binary-IQ file path and declared receiver output paths.
3. Run the actual `dsp_fhss_demo` binary-IQ receiver graph.
4. Wait until the receiver process terminates.
5. Only then open the independently recorded transmitter event log.
6. Match detections one-to-one, retain misses/false detections/duplicates and
   failed executions separately, and journal the first attempt atomically.

The journal writes an `attempting` record before IQ resolution. Missing files,
hash/size/format/partial-sample/SigMF failures therefore become immutable
pre-execution failures. A process interruption converts the durable in-progress
entry to a terminal interrupted failure on resume; it is never retried or
substituted. Post-receiver evaluator failures retain the actual command, return
status, stdout/stderr, executable, graph, effective-config, plugin, IQ and
exposure provenance.

Matching maximizes cardinality before minimizing total absolute timing error;
it is not greedy. Every unmatched detection contributes to the false-alarm
numerator, while detections that also fell inside an already-associated window
are separately tagged as duplicate subtypes. Confidence is taken from the
frozen profile. Session-clustered percentile-bootstrap intervals are enveloped
by Wilson score bounds using the number of independent sessions as the
effective sample size. This deliberately prevents finite all-success or
all-failure evidence from collapsing to `[1,1]` or `[0,0]`. Physical gates use
the conservative endpoints. False-alarm bounds use exact one-sided Poisson-CDF
inversion, including the zero-event case.

Complete selected journals retain failed and timed-out captures and aggregate
them into a governed FAIL report; they are never dropped or substituted.
Evaluator-accessible event logs contribute failed signal events as misses and
message errors. Missing or invalid event evidence marks affected metrics
unresolved and fails their gates. Incomplete receiver runs use explicit
receiver-reported processed samples when available; otherwise searched
exposure is zero and unknown, which fails the execution and false-alarm gates.
The intended IQ file length is never treated as timeout exposure.

The receiver graph scan recursively rejects message schedules, expected words,
truth manifests, waveform generators, transmitted-frequency hints, hidden
epochs, and synthetic sources. Resume never retries or replaces a journaled
capture.

The current receiver evidence sink completes after one assembled message and
retains that message's decoded pulses. The v1 capture contract therefore
freezes at most one transmitted FHSS message per signal-bearing capture;
negative/control captures contain none. Multi-message capture validation is an
explicitly unsupported future extension, not silently undercounted evidence.

## Frozen-state policy

[The Phase 4 profile](../../libdsp/config/fhss_phase4_validation_profile_v1.json)
extends the exact Phase 3 v7 hashes without modifying them. It is intentionally
marked `synthetic_only_infrastructure_physical_validation_unavailable`.
Provisional thresholds and
correlation metrics are declared, but a held-out freeze is forbidden until a
lawful, calibrated, session-partitioned physical corpus and approved agreement
margins exist. No held-out result was inspected or generated.

Development, validation, and held-out data must be partitioned by independent
physical session, equipment run, or physical trial. Adjacent portions of one
recording cannot populate different partitions. Tuning may use development
captures only. Any receiver change requires minimized development evidence, an
isolated regression, preserved original evidence, and a newly frozen,
session-disjoint held-out run.

## Execution tiers

Per-commit CI validates schemas/contracts, hashes and formats, literal-byte
conversion, topology isolation, one-to-one matching, clustering, and safe
missing-corpus behavior. It performs no external download.

Offline/full execution requires an explicit external corpus root and frozen
hashes. It journals results atomically, preserves failures/timeouts, and has no
simulation fallback. Missing physical data is a hard failure.

Readiness resolves the complete governed artifact graph: IQ and SigMF,
transmitter event logs, calibration artifacts, RF authorization artifacts,
equipment references, held-out session membership, scenario inventories, and
minimum independent-session/capture/exposure requirements. Count-filled but
unverifiable metadata cannot return READY.

The `correlate` command supports future nonempty paired evidence only after
agreement criteria are frozen. It requires compatible units and reference
planes, propagates simulation and calibration standard uncertainty, emits
machine-readable residuals, applies the frozen absolute margin, and requires a
declared discrepancy classification for every failed point. The committed
zero-point correlation report remains UNAVAILABLE/DEFERRED.

```sh
python3 examples/DSP/tools/fhss_phase4_recorded.py validate \
  --profile libdsp/config/fhss_phase4_validation_profile_v1.json

python3 examples/DSP/tools/fhss_phase4_recorded.py scan-graph \
  --graph libdsp/config/fhss_phase2_binary_iq_receiver.json

python3 examples/DSP/test/test_fhss_phase4_recorded.py
```

After an authorized corpus exists, validation and replay additionally require
`--collection`, `--corpus-root`, and, for replay, the frozen receiver, graph,
plugin directory, journal, and work root. Conversion to canonical `cf32_le`
uses `convert` and emits a separate provenance JSON; originals are never
modified.

Frozen physical evaluation is permitted only after both the statistical
threshold set and correlation agreement criteria are frozen. The threshold
set carries a digest over its origin and numeric limits. Synthetic-only
profiles must retain provisional policy states and a null threshold digest.

Dataset, freeze-manifest, replay-journal, and aggregate bindings use SHA-256
over the exact serialized artifact bytes. Correlation loads those artifacts,
validates the complete bound v2 journal and physical selection, and reproduces
the aggregate before accepting paired hardware evidence.

## Acceptance A–I

| Criterion | Result | Evidence |
|---|---:|---|
| A. Baseline and independence | PASS | Phase 3 v7 hashes are bound; truth-free production graph scan passes; generator is not imported or used. |
| B. Dataset governance | UNAVAILABLE/DEFERRED | Strict schemas and synthetic provenance guards exist, but there is no physical collection manifest or external corpus. |
| C. Calibration and metadata | UNAVAILABLE/DEFERRED | Contracts reject invalid/missing held-out calibration, but zero calibration/equipment records exist. |
| D. Replay and truth isolation | UNAVAILABLE/DEFERRED | Synthetic dry-run orchestration/isolation tests pass; zero physical captures were replayed. |
| E. Statistical validity | UNAVAILABLE/DEFERRED | Session-clustered machinery and pre-freeze policy exist; zero physical held-out sessions or observations exist. |
| F. Capture-program completeness | UNAVAILABLE/DEFERRED | Conducted 0/0 s, emulator 0/0 s, OTA 0/0 s; hardware testing is unavailable. |
| G. Simulation-to-hardware correlation | UNAVAILABLE/DEFERRED | Zero hardware paired points; correlation claims are prohibited. |
| H. Robustness and C++26 | PASS | Python infrastructure negative/golden tests pass; no C++ source changed. The inherited executor unbounded-join limitation remains disclosed. |
| I. Reports and regression hygiene | PASS | Readiness/correlation JSON and this report agree; focused and affected regressions are reported in the implementation handoff. |

The [machine-readable readiness report](../../libdsp/config/fhss_phase4_readiness_report_v1.json)
records zero sessions, captures, searched seconds, held-out replays, and paired
correlation points. The [correlation report](../../libdsp/config/fhss_phase4_correlation_report_v1.json)
is explicitly `UNAVAILABLE_DEFERRED`.

## Known limitations

- Partial-hop collision offsets t800 and t1600 remain unsupported from Phase 3.
- The generic executor may perform an unbounded join after timeout when an
  active `Consume()` operation lacks cooperative interruption.
- No conducted, emulator, OTA, environmental, equipment, calibration, privacy,
  licensing, retention, or authorization claim can be evaluated until real
  governed records are supplied.
- Passing infrastructure tests cannot satisfy the Phase 4 held-out gate.
