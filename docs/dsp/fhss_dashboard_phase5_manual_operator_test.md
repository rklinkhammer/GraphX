# FHSS Dashboard Phase 5 Operator Test

This procedure validates message-oriented dashboard control using synthetic IQ
only. No HWIL facility is available. The result is software-contract evidence,
not RF, regulatory, interoperability, or hardware qualification.

## Prerequisites

From the repository root, build the C++26 dashboard and provision the locked
OpenAPI/JSON Schema validator environment:

```sh
cmake -S . -B build-ninja/ninja-debug -G Ninja \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_DASHBOARD_CONTRACT_PYTHON="$PWD/.venv-dashboard-contracts/bin/python"
cmake --build build-ninja/ninja-debug --target \
  graphx-dsp-fhss-demo graphx-dsp-fhss-iq-generator test_dsp_example_unit
```

If the validator environment does not exist, create it with
`examples/DSP/dashboard/api/provision_contract_validators.py` as documented in
the operator README.

Validate an installed-tree launch independently of the source layout:

```sh
cmake --install build-ninja/ninja-debug --prefix /path/to/new/graphx-install
/path/to/new/graphx-install/bin/graphx-dsp-fhss-demo \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 0 \
  --dashboard-artifact-root /path/to/new/installed-artifacts
```

Use a newly created artifact root writable only by the operator account. Each
job is bounded to four messages, 512 pulses, 4,194,304 samples, 64 MiB of IQ,
and 1 MiB per metadata document. The controller retains at most 32 jobs/2 MiB
of serialized history for one hour.

## Automated public workflow

Choose a new, operator-owned output directory. The tool launches the production
demo on loopback and uses only its public HTTP interface and public executables.

```sh
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py exercise \
  --phase 5 --build-dir build-ninja/ninja-debug \
  --output-dir /path/to/new/phase5-output

.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 5 --output-dir /path/to/new/phase5-output
```

The workflow must demonstrate all of the following:

1. Step submits one complete 18-pulse protocol message and reaches a terminal
   receiver result.
2. Repeating the same idempotency key and body returns the same opaque job ID;
   conflicting reuse is rejected with RFC 9457 problem details.
3. A queued job can be cancelled without generating artifacts, and an active
   job reaches a bounded terminal cancellation state.
4. Continue replays multiple complete messages, while a short deadline produces
   a deterministic timeout.
5. Reset is rejected during active work, advances the controller epoch after
   terminal work, and repeated reset is idempotent.
6. Raw IQ, truth, SigMF metadata, receiver configuration, and manifest are
   separate files with matching hashes.
7. The receiver graph contains no message, schedule, truth, or active-frequency
   keys.
8. After the dashboard stops and truth is deleted, the public demo replays the
   IQ plus receiver-minimal graph successfully.

Inspect `phase5-report.json`, each saved API response, and the job manifest;
do not rely only on the console summary.

The equivalent direct requests are:

```sh
curl -sS -H 'Content-Type: application/json' \
  -H 'Idempotency-Key: manual-step-1' \
  -d '{"request_id":"manual-step-1","timeout_ms":60000}' \
  "$URL/api/v1/fhss/commands/step" | jq .
curl -sS -H 'Content-Type: application/json' \
  -H 'Idempotency-Key: manual-continue-1' \
  -d '{"request_id":"manual-continue-1","message_count":2,"timeout_ms":60000}' \
  "$URL/api/v1/fhss/commands/continue" | jq .
curl -sS "$URL/api/v1/fhss/jobs" | jq .
curl -sS -H 'Content-Type: application/json' -d '{}' \
  "$URL/api/v1/fhss/jobs/$JOB_ID/cancel" | jq .
curl -sS -H 'Content-Type: application/json' -d '{}' \
  "$URL/api/v1/fhss/commands/reset" | jq .
```

Repeat the Step request byte-for-byte with the same key and verify the job ID,
artifact hashes, graph generation, and terminal timestamp do not change. Reuse
the key with a different `message_count` and require HTTP 409 with
`idempotency_key_reused_with_different_payload`. Poll the job URI until its
state is terminal; do not treat graph completion alone as receiver acceptance.

For a completed job, verify the separate paths and the recursive receiver
boundary:

```sh
jq -e '.artifacts | has("iq") and has("truth") and has("sigmf") and has("receiver_config")' job.json
jq -e '.. | objects | keys[] | select(. == "messages" or . == "active_frequency_indices" or contains("truth"))' \
  receiver-minimal.json && exit 1 || true
shasum -a 256 iq.cf32 iq.sigmf-meta truth.withheld.json receiver-minimal.json
```

The operator independently requires 18 pulses at 6,500-input-sample spacing,
with the first pulse after one 6,500-sample receiver warm-up slot. Its decoded
word and channel comparisons are not derived solely from the production
generator or comparison route. During live replay the controller removes the
truth file; it restores truth only after replay is terminal. Queued cancellation
must have an empty artifact map and both work flags false. The 100 ms supported
deadline is the deterministic timeout case.

## Browser inspection

Run the production demo with `--dashboard` and an explicit artifact root, then
open the printed loopback URL:

```sh
build-ninja/ninja-debug/examples/DSP/graphx-dsp-fhss-demo \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 0 \
  --dashboard-artifact-root /path/to/new/browser-artifacts
```

Confirm that the UI labels the data as synthetic and exposes complete-message
Step/Continue controls rather than node stepping. Submit Step, observe the job
state independently from graph lifecycle and receiver comparison, refresh the
page, and confirm refresh does not create a duplicate job. Exercise Continue,
Cancel, and Reset and compare the displayed opaque ID and terminal state with
`GET /api/v1/fhss/jobs`.

For each certified state, start a dedicated served case, capture the genuine
browser page, and bind the PNG to the served-state document:

```sh
.venv-dashboard-contracts/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  serve --phase 5 --build-dir build-ninja/ninja-debug \
  --output-dir /path/to/new/phase5-output --case step
.venv-dashboard-contracts/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  record-screenshot --phase 5 --output-dir /path/to/new/phase5-output \
  --case step --path /path/to/genuine-step.png
```

Repeat with `continue` and `cancelled`. Then require strict certification:

```sh
.venv-dashboard-contracts/bin/python examples/DSP/dashboard/operator/fhss_dashboard_operator.py \
  verify --phase 5 --output-dir /path/to/new/phase5-output --require-screenshots
```

The final report must be `PASS` / `final_verified`. A partial pre-browser
report is not final evidence. On restart, the controller epoch changes,
in-memory jobs and idempotency entries are not restored, committed artifacts
remain explicitly replayable, and incomplete `.tmp` files are removed. The
generic GraphX limitation remains: a non-cooperative node can prevent a bounded
executor join even though this FHSS path cooperates.

## Cleanup

Stop the demo normally. The operator cleanup command removes only a directory
carrying its ownership marker:

```sh
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py cleanup \
  --phase 5 --output-dir /path/to/new/phase5-output
```

Retain the report and artifacts instead when they are required as validation
evidence.
