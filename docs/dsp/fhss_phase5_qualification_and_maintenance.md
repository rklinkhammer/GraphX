# FHSS Phase 5 qualification and maintenance

## Scope and outcome

Phase 5 qualifies software and engineering evidence only. There is no HWIL,
conducted, channel-emulator, OTA, independently recorded, field, customer, or
other physical RF data. All current data is synthetic or software-derived.
Consequently:

- software/engineering release readiness is `PASS` for the bound artifacts;
- the frozen Phase 3 v7 synthetic characterization is `PASS` in its declared
  matrix;
- recorded-IQ/HIL validation is `UNAVAILABLE_DEFERRED`;
- production-RF qualification is `NOT_QUALIFIED`.

This is not regulatory, certification, interoperability, product, hardware, or
production-RF qualification. Infrastructure tests and configured values are
not measurements. Physical sessions, captures, searched exposure, paired
points, and recorded field failures all equal zero.

## Evidence and traceability

The versioned profile separates claim classes and forbids promotion from
synthetic/software evidence to recorded or physical claims. Requirements,
claims, immutable evidence, tests, limitations, and minimized regressions are
linked through strict registries in `libdsp/config`. Validation rejects unknown
fields in governed records, duplicate or dangling IDs, circular supersession,
missing files, byte-hash changes, unsafe paths and symlink escapes, non-finite
JSON, incomplete reverse traceability, evidence-free passing claims, stale or
contradictory status, and physical evidence in this synthetic-only baseline.

Phase 3 v5 and v6 remain hash-bound historical failures. Phase 3 v7 remains the
passing frozen synthetic evidence; it is not hardware evidence. Phase 4 remains
infrastructure readiness with physical criteria unavailable and zero physical
counts.

## Regression corpus

The compact manifest stores configuration/schedule metadata and expected
outcomes, not generated IQ. It covers the Phase 1 parser dependency, Phase 2
channelizer/detector boundary, Phase 3 no-detection EOS, v4 terminal failure,
v5/v6 timeout shape, circular-FIR workload, CPSM state, Phase 4 journal and
promotion attacks, and unsupported t800/t1600 partial-hop offsets. Every entry
is labeled `synthetic` or `software_runtime`; none is a field failure.

Large IQ, sensitive equipment/location data, credentials, proprietary
recordings, and fabricated field captures are prohibited from this corpus.

## CI and trends

Per-commit CI runs schema/registry validation, the compact corpus, deterministic
report verification, Phase 1–4 smoke tests, and topology/truth-isolation scans.
It performs no download. Qualification and verification freshly run the full
libdsp suite (either 174 passes with Metal available, or 169 passes plus exactly
five explicitly named native-Metal-unavailable skips),
the full DSP example suite (56 passes and zero skips), and the selected C++
corpus filter. Any additional, missing, or renamed skip fails software
readiness. Nightly CI may also run
cross-compiler/back-end checks, sanitizer-compatible malformed-input tests,
and allocation/runtime trends. Release-candidate validation adds the declared
software regressions, frozen Phase 3 verification, Phase 4 infrastructure
verification, trace completeness, and limitation review. Native-Metal skips on
unsupported hosts remain skips, not passes.

The frozen Phase 3 v7 artifacts are the current synthetic baseline. A trend
decision must declare metric, units, baseline hash, sample count, variance
treatment, tolerance/confidence, and alert/failure thresholds before results
are inspected. One noisy run cannot pass or fail a trend. Rebaselining requires
a new versioned profile and independent approval; held-out seeds are not rerun
to seek a preferred outcome. The current clean-detection trend entry binds its
measured v7 baseline but leaves tolerance, alert, and failure thresholds null,
so its decision is explicitly `UNVERIFIED`; Phase 5 makes no trend PASS claim.

## Future failure intake

Future failures are classified as synthetic, software/runtime, recorded field,
conducted, channel emulator, OTA/HIL, customer/interoperability, or
unknown/unverified. A recorded/field classification requires lawful access,
immutable original hash, provenance and capture metadata, privacy/license
review, environment and receiver version, observed behavior, retention policy,
and a session-disjoint development/held-out plan. The minimized record retains
the original hash, expected behavior, regression, requirement/claim impact,
fix reference, and non-regression evidence.

A synthetic artifact labeled recorded or field is rejected. No fictional field
entry may be created. Current recorded-field entry count is zero.

## Reproduction

```sh
python3 examples/DSP/tools/fhss_phase5_qualify.py validate

python3 examples/DSP/tools/fhss_phase5_qualify.py verify \
  --raw libdsp/config/fhss_phase5_qualification_raw_v1.json \
  --report libdsp/config/fhss_phase5_qualification_report_v1.json \
  --markdown docs/dsp/fhss_phase5_qualification_report_v1.md

python3 examples/DSP/test/test_fhss_phase5_qualification.py
```

`qualify --output-dir <new-empty-directory>` freshly executes every governed
gate and stages the complete raw JSON,
aggregate JSON, and Markdown set before committing it. Any second- or
third-file commit failure rolls the whole set back, and existing outputs are
never overwritten. `verify` is read-only, freshly reruns the same bounded
commands, compares every captured deterministic field, and regenerates all
three byte-for-byte. Captured output is normalized only for elapsed-time
values; argv, exit status, parsed counts and test IDs, normalized output bytes,
hashes/sizes, executable/input/source/build/compiler/environment bindings, and
the exact allowed-skip set must match. The older test attestation is retained
only as historical provenance and can never substitute for these fresh gates.
The tool has no network, publishing,
tagging, release, upload, or RF-control capability.

## Acceptance A–I

| Criterion | Result | Evidence |
|---|---:|---|
| A. Claims and baseline integrity | PASS | Four verdict classes are separate; historical evidence is immutable. |
| B. Requirements and traceability | PASS | Five measurable requirements and four claims are bidirectionally linked through six exact trace relationships. |
| C. Evidence integrity | PASS | Thirteen source artifacts are path-safe, semantically checked, and file-byte hash-bound. |
| D. Regression corpus | PASS | Nine minimized synthetic/software entries; zero field entries. |
| E. Qualification runner | PASS | Deterministic generation, no-clobber output, and read-only verification. |
| F. CI and trend policy | PASS | Per-commit/nightly/release tiers and predeclared rebaseline policy. |
| G. Failure maintenance | PASS | Governed future intake; synthetic-to-field promotion rejected. |
| H. Robustness and C++26 | PASS | Adversarial Python tests pass; no C++ changed. |
| I. Reports and hygiene | PASS | Raw JSON, aggregate JSON, Markdown, hashes, and verdicts reproduce. |

Open limitations remain: t800/t1600 partial-hop collisions are unsupported;
the generic executor can perform an unbounded join when active `Consume()`
lacks cooperative interruption; native-Metal skips require a supported host;
and no physical validation exists. Removing the physical block requires an
authorized, calibrated, independently recorded, session-disjoint Phase 4
program with pre-frozen gates and agreement criteria.
