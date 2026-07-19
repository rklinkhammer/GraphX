# FHSS dashboard Phase 4 manual operator test

This checklist validates the FHSS-specific Phase 4 dashboard with synthetic IQ
only. No HWIL, conducted-RF, channel-emulator, independently recorded, or OTA
evidence is available. Passing it qualifies the software observation path, not
RF performance.

## Build and machine checks

Use the pinned contract environment configured as
`GRAPHX_DASHBOARD_CONTRACT_PYTHON`:

```sh
python3 examples/DSP/dashboard/api/provision_contract_validators.py \
  --venv /private/tmp/graphx-dashboard-contracts-venv
/private/tmp/graphx-dashboard-contracts-venv/bin/python -m pip check
cmake -S . -B build-ninja/ninja-debug -G Ninja \
  -DCMAKE_CXX_STANDARD=26 -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_DASHBOARD_CONTRACT_PYTHON=/private/tmp/graphx-dashboard-contracts-venv/bin/python
```

The provisioning script installs the exact versions from
`requirements-contracts.lock`; `pip check` must report no broken requirements.
The configure output must identify `-std=c++2c`.

```sh
cmake --build build-ninja/ninja-debug
ctest --test-dir build-ninja/ninja-debug -R \
  'fhss_dashboard_(api_contracts|phase4_evidence_smoke)' --output-on-failure
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  exercise --phase 4 --build-dir build-ninja/ninja-debug \
  --output-dir /private/tmp/graphx-dashboard-phase4
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  verify --phase 4 --output-dir /private/tmp/graphx-dashboard-phase4
```

For a source-independent installed-tree check:

```sh
cmake --install build-ninja/ninja-debug \
  --prefix /private/tmp/graphx-dashboard-phase4-install
cd /private/tmp
/private/tmp/graphx-dashboard-contracts-venv/bin/python \
  /private/tmp/graphx-dashboard-phase4-install/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py \
  exercise --phase 4 --build-dir /private/tmp/graphx-dashboard-phase4-install \
  --output-dir /private/tmp/graphx-dashboard-phase4-installed-evidence
/private/tmp/graphx-dashboard-contracts-venv/bin/python \
  /private/tmp/graphx-dashboard-phase4-install/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py \
  verify --phase 4 \
  --output-dir /private/tmp/graphx-dashboard-phase4-installed-evidence
```

The workflow generates `replay.cf32` with the canonical
`graphx-dsp-fhss-iq-generator`, retains `replay.sigmf-meta`, and stores hashed
evaluator-only copies under `artifacts/<sha256-of-artifact-name>.bin`. It also
creates `impaired-cfo-awgn.cf32`, `negative-no-message.cf32`,
`malformed-iq.cf32`, `phase4-cases/*-config.json`, and
`phase4-clean-measured-baseline.json`. Generator schedule and truth paths are
unlinked before receiver replay.

The clean generator-only schedule must contain the receiver's explicit
64-channel, 7.5 MHz-spaced IQ offset map and message start samples shifted by
6,500 samples. This one-pulse-slot causal warm-up protects the first decoded
word; the receiver graph must still contain neither the schedule nor truth.
Natural receiver processing is bounded at 30 seconds and Stop remains bounded
at five seconds. If the processing bound expires without a graph completion
signal, confirm the terminal state is `failed` with code `execution_failed`,
not a partial successful completion.

On Overview, confirm every FHSS Schedule row has a numeric configured start,
preamble count, and body count. In Synthetic Schedule Expectations, confirm no
cell says `undefined` and adjacent pulse starts differ by 6,500 samples. The
values must agree with the Synthetic truth rows; the legacy 0,1,2… timing is a
failure. For the clean case, the Receiver Sample Spectrum selector should
default to a receiver-observed physical channel, show nonzero linear
magnitudes, and retain receiver provenance. For the negative case the selector
must show a disabled `Unavailable — no observed physical channel` option, the
request must omit the channel, and the response must have `channel_index: null`,
`availability.reason: no_candidate_detected`, and empty bins. It must never
visually or semantically fall back to channel 0. Exercise at
least one manual selector change within the bounded 0–63 range.

Do not interpret runtime completion as message acceptance. For clean IQ,
`terminal_result.code` is `execution_completed` and
`receiver_message_result.accepted` is true. For negative IQ, runtime execution
still completes, the assembler truthfully reports its missing-preamble status,
and `receiver_message_result` must say `accepted: false` with
`decoded_pulse_count: 0`.

Confirm the report says `synthetic_data_only: true`, `hwil_available: false`,
and `production_rf_qualified: false`. Confirm raw IQ is separate from hashed
evaluator truth and SigMF metadata. The live output directory must contain no
`*-truth.json` or `*schedule.json` before a receiver case is served.

## Serve and inspect the stable cases

The `serve --case` command rebuilds and starts the selected receiver itself,
polls `/api/v1/fhss/status` to a terminal state, and writes
`phase4-<case>-served-state.json` before printing its state line. To exercise
the underlying lifecycle endpoints manually instead, launch the demo and use a
PID-scoped trap (never a broad `pkill`):

```sh
LOG=/private/tmp/graphx-dashboard-phase4-server.log
./build-ninja/ninja-debug/examples/DSP/graphx-dsp-fhss-demo \
  --dashboard --dashboard-port 18087 >"$LOG" 2>&1 &
DASH_PID=$!
trap 'kill -TERM "$DASH_PID" 2>/dev/null || true; wait "$DASH_PID" 2>/dev/null || true' EXIT
until grep -q '^Dashboard URL:' "$LOG"; do sleep 0.1; done
DASH_URL=$(sed -n 's/^Dashboard URL: //p' "$LOG" | tail -1)
CONFIG=$(curl -fsS "$DASH_URL/api/v1/fhss/config/authoritative")
REVISION=$(printf '%s' "$CONFIG" | jq -r '.config_revision')
curl -fsS -X POST -H 'Content-Type: application/json' \
  -d "{\"expected_revision\":$REVISION,\"command_id\":\"manual-rebuild\"}" \
  "$DASH_URL/api/v1/fhss/config/rebuild" | jq -e '.active_generation >= 1'
curl -fsS -X POST -H 'Content-Type: application/json' \
  -d '{"command_id":"manual-start"}' \
  "$DASH_URL/api/v1/fhss/commands/start" \
  | jq -e '.status == "accepted" and .code == "start_accepted"'
until curl -fsS "$DASH_URL/api/v1/fhss/status" \
  | jq -e '.lifecycle_state == "completed" or .lifecycle_state == "failed"' >/dev/null; do sleep 0.1; done
curl -fsS "$DASH_URL/api/v1/fhss/status" \
  | jq -e '.active_generation >= 1 and .active_run_epoch >= 1 and .terminal_result != null'
```

An active non-cooperative node `Consume()` can still outlive the documented
five-second stop wait and delay process destruction; this is the disclosed
generic runtime limitation. Do not reinterpret a stop timeout as completion.

Run one command at a time and leave it running while inspecting its printed
loopback URL:

```sh
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  serve --phase 4 --build-dir build-ninja/ninja-debug \
  --output-dir /private/tmp/graphx-dashboard-phase4 --case clean --port 18084
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  serve --phase 4 --build-dir build-ninja/ninja-debug \
  --output-dir /private/tmp/graphx-dashboard-phase4 --case impaired --port 18085
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  serve --phase 4 --build-dir build-ninja/ninja-debug \
  --output-dir /private/tmp/graphx-dashboard-phase4 --case negative --port 18086
```

For each case, verify that the page labels and toggles independently identify
`Synthetic truth`, `Receiver observed`, and `Evaluator comparison`. Refresh the
page and confirm generation, run epoch, observation ID/hash, counts, and
terminal result remain unchanged. The selected-channel spectrum must describe
receiver samples and non-calibrated magnitude, not RF power.

Useful direct checks, substituting the active port:

```sh
curl -fsS http://127.0.0.1:18084/api/v1/fhss/expected-truth
curl -fsS http://127.0.0.1:18084/api/v1/fhss/observations
curl -fsS http://127.0.0.1:18084/api/v1/fhss/comparison
PHYSICAL_CHANNEL="$(curl -fsS http://127.0.0.1:18084/api/v1/fhss/observations |
  jq -er '.observed_pulses[0].physical_channel_index')"
curl -fsS "http://127.0.0.1:18084/api/v1/fhss/spectrum?channel=${PHYSICAL_CHANNEL}&fft_size=128"
curl -fsS 'http://127.0.0.1:18084/api/v1/fhss/spectrum?fft_size=128'
curl -fsS http://127.0.0.1:18084/api/v1/fhss/observation-provenance
curl -fsS http://127.0.0.1:18084/api/v1/fhss/observation-history
```

Expected results:

- clean: completed state and complete independently derived expected
  message/pulse accounting;
- impaired: completed state, seed 404, CFO 750 Hz, Eb/N0 18 dB, and measured
  matched/missed/unexpected comparison;
- negative: completed state with zero detected/observed pulses, no preamble
  lock, and no assembler/message completion;
- malformed (captured by `exercise`): failed state and no fabricated pulse.

For clean, inspect the actual golden fields: every
`observed_pulses[].global_start_sample` and `logical_frequency_index` must map
one-to-one to an expected pulse within 64 input samples, each comparison
`channel_delta` must be zero, every `decoded_value_agrees` must be true,
`missed_expected_indices`, `unexpected_observed_indices`, and `ambiguous` must
be empty, `preamble.locked` must be true, and `assembler.availability.state`
must be `available`. For impaired, do not require degradation: a receiver may
remain correct at 750 Hz CFO and 18 dB Eb/N0. Require the recorded impairment
identity and a deterministic measured difference in comparison details or the
receiver spectrum-bin hash relative to the clean baseline.

Spectrum provenance must identify `sample_captures.samples`, the channelizer
node/class/schema, sample rate, global start, input interval, symmetric Hamming
window, two-sided FFT-shift ordering, and `calibrated_power: false`. The focused
`ActiveGenerationWithoutTypedReceiverSourceIsUnavailableWithoutTruthFallback`
test installs an active graph manager without an
`IFHSSReceiverObservationSource` and requires `source_not_diagnosable`; no
expected message, word, or truth hash may appear. This isolated public receiver
lane is used because the production runtime intentionally has no unsafe toggle
that removes diagnostics from a running graph.

The exercise also uses the public
`receiver_input.dashboard_observation_enabled` configuration to rebuild and
run the production receiver once with observation export disabled. It requires
null counts, no typed sources, an indeterminate comparison, and an unavailable
spectrum; it then rebuilds with export restored and requires real observed
pulses again.

During the long replay the operator polls status, observations, provenance,
history, omitted-channel spectrum, comparison, expected truth, and metrics from
multiple HTTP workers. Responses must remain bounded, identities must agree on
generation and run epoch, graph progress must be visible, and observations
must be byte-identical after terminal state. The clean lane also writes
`phase4-clean-independent-oracle.json`; timing, physical-channel mapping,
MSB-first CPSM bits, and decoded words are derived directly from the schedule
and receiver map, and a one-bit perturbation must be rejected.

Repeat the following twice after refresh and compare the files byte-for-byte:

```sh
for n in 1 2; do
  curl -fsS http://127.0.0.1:18084/api/v1/fhss/observations >"/private/tmp/observed-$n.json"
  curl -fsS http://127.0.0.1:18084/api/v1/fhss/comparison >"/private/tmp/comparison-$n.json"
done
cmp /private/tmp/observed-1.json /private/tmp/observed-2.json
cmp /private/tmp/comparison-1.json /private/tmp/comparison-2.json
sha256sum /private/tmp/observed-1.json /private/tmp/comparison-1.json
```

Audit the receiver case recursively for prohibited side channels:

```sh
RECEIVER_GRAPH=/private/tmp/graphx-dashboard-phase4/phase4-receiver-minimal.json
jq -e '[path(..) as $p | $p[]? |
  select(type == "string") | ascii_downcase |
  select(. == "messages" or contains("truth") or contains("schedule") or
         . == "active_frequency_indices" or
         (contains("generator") and contains("metadata")) or
         (contains("expected") and (contains("word") or contains("value"))))] |
  length == 0' "${RECEIVER_GRAPH}"

jq '.graph.messages = []' "${RECEIVER_GRAPH}" >/private/tmp/receiver-injected.json
if jq -e '[path(..) as $p | $p[]? |
  select(type == "string") | ascii_downcase |
  select(. == "messages" or contains("truth") or contains("schedule") or
         . == "active_frequency_indices" or
         (contains("generator") and contains("metadata")) or
         (contains("expected") and (contains("word") or contains("value"))))] |
  length == 0' /private/tmp/receiver-injected.json; then
  echo 'FAIL: injected messages container escaped audit' >&2
  exit 1
fi
```

The first command must pass and the deliberate injected-container command must
fail. The audit targets the persisted effective receiver-minimal graph, not the
authoritative scenario (which legitimately contains scheduled messages), and
checks every path component including empty object/array container keys.

Validate live documents against the authoritative schemas with the operator's
`exercise`/`verify` commands and the registered
`fhss_dashboard_api_contracts` test. The validator builds a Draft 2020-12
registry for all cross-schema URN references; the lightweight subset checker is
not the authority.

## Capture screenshot evidence

The source-tree and installed-tree automation lanes intentionally end as
`PARTIAL` / `partial_pre_browser`: they validate replay and served-state
contracts but cannot manufacture browser evidence. They are not completed
Phase 4 qualifications. Three genuine browser captures first leave the report
at `PARTIAL` / `captures_complete_unverified`; only a subsequent successful
`verify --require-screenshots` may transition it to `PASS` /
`final_verified`.

Capture each visible browser as a real PNG, stop that case, then bind it to its
case-specific served-state evidence:

```sh
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  record-screenshot --phase 4 --output-dir /private/tmp/graphx-dashboard-phase4 \
  --case clean --path /path/to/clean.png
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  record-screenshot --phase 4 --output-dir /private/tmp/graphx-dashboard-phase4 \
  --case impaired --path /path/to/impaired.png
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  record-screenshot --phase 4 --output-dir /private/tmp/graphx-dashboard-phase4 \
  --case negative --path /path/to/negative.png
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  verify --phase 4 --require-screenshots \
  --output-dir /private/tmp/graphx-dashboard-phase4
```

The final command must print `PASS`. It validates complete, non-uniform PNG
containers of at least 640 by 360 pixels with meaningful color/luminance and
pixel-transition content; hashes; capture time/freshness; loopback URL; case,
generation, and run epoch; case config and IQ identity; receiver observation
identity; and the absence of live truth files. A tiny placeholder image, stale
capture, URL/case/generation/run mismatch, modified screenshot, or modified
served-state file must make verification fail.

Screenshot checklist for each overlay toggle:

- URL/port and case identity visible in the capture record;
- Synthetic truth alone, Receiver observed alone, then both together;
- Evaluator comparison counts and timing deltas visible;
- generation, run epoch, revision, ETag, observation ID/hash visible;
- selected channel, FFT size, sample rate, window, units, and non-calibrated
  spectrum label visible;
- negative case visibly shows zero detection and no lock/message completion.

Re-run `verify --require-screenshots` after copying the evidence directory or
install tree. It recomputes the dashboard, OpenAPI, every schema, operator,
report-schema, content-addressed artifact, served-state, and PNG hashes.

Cleanup is marker- and phase-guarded and removes only exact operator-owned
artifacts:

```sh
/private/tmp/graphx-dashboard-contracts-venv/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  cleanup --phase 4 --output-dir /private/tmp/graphx-dashboard-phase4
```

## Troubleshooting and result worksheet

- `install ... authoritative live validation`: use the pinned contract Python,
  not the system interpreter.
- `dashboard did not publish its bound URL`: inspect the captured server log,
  executable path, plugin directory, and whether the requested port is in use.
- `live truth artifact must be absent`: move evaluator evidence out of the live
  output path or rerun `exercise`; never weaken the guard.
- `clean case lacks full ... agreement`: inspect observation provenance and
  comparison JSON; do not substitute expected truth for missing diagnostics.
- `impaired ... delta is incomplete`: verify `impaired-metadata.json`, IQ hash,
  receiver spectrum availability, and clean baseline identity.
- stop timeout: retain the process PID and diagnose the non-cooperative node;
  do not use a repository-wide process kill.

Record `PASS` or `FAIL` with artifact/report hashes for each line:

| Gate | Result | Evidence |
|---|---|---|
| C++26 dashboard configure/build |  |  |
| authoritative contracts |  |  |
| focused observation/evidence tests |  |  |
| source operator and verify |  |  |
| installed-tree operator and verify |  |  |
| clean golden decode |  |  |
| impaired identity and measured delta |  |  |
| negative and malformed truthfulness |  |  |
| byte-stable refresh |  |  |
| recursive truth-side-channel audit |  |  |
| three bound screenshots and hash reverification |  |  |
| synthetic-only/no-HWIL qualification statement |  |  |
