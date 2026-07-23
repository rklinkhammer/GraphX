# FHSS dashboard Phase 3 manual operator test

## Fresh-clone prerequisite

Run this qualification only from a newly downloaded clone of
`https://github.com/rklinkhammer/GraphX.git`. Record `git rev-parse HEAD` and
require a clean `git status --short` before building. Do not reuse a developer
checkout, build tree, installed dependencies, CMake cache, or prior evidence.
Required packages must already be installed on the host; stop if one is
missing rather than provisioning it under a temporary path.

This is a deterministic synthetic-IQ test. It provides no hardware-in-the-loop
(HWIL), conducted-RF, OTA, or production-RF qualification evidence. Run it from
the repository root on macOS or Linux with CMake, Ninja, a C++26 compiler,
Python 3, `jq`, `curl`, and `shasum` available.

## 1. Build, install, and create evidence inputs

Copy and paste this block. It creates only named paths beneath the fresh clone.

```sh
set -eu
ROOT="$PWD"
SCRATCH="$ROOT/.graphx-operator/phase3-scratch"
BUILD="$SCRATCH/build"
INSTALL="$BUILD/install"
OUT="$ROOT/.graphx-operator/phase3-evidence"
test ! -e "$SCRATCH"
test ! -e "$OUT"
mkdir -p "$OUT/evidence/schemas"
mkdir -p "$SCRATCH"
printf '%s\n' "graphx-fhss-dashboard-phase3-owned" > "$OUT/.graphx-owned"
PID=""
cleanup_owned() {
  if test -n "${PID:-}" && kill -0 "$PID" 2>/dev/null; then
    kill -INT "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
  fi
  rm -rf "$SCRATCH"
}
trap cleanup_owned EXIT HUP INT TERM
SCHEDULE="$OUT/schedule.json"
IQ="$OUT/replay.cf32"
TRUTH="$OUT/truth.json"
SIGMF="$OUT/replay.sigmf-meta"
VALIDATOR_PYTHON="$ROOT/.venv-dashboard-contracts/bin/python"
test -x "$VALIDATOR_PYTHON"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_DASHBOARD_CONTRACT_PYTHON="$VALIDATOR_PYTHON"
cmake --build "$BUILD" -j4
cmake --install "$BUILD" --prefix "$INSTALL"
"$VALIDATOR_PYTHON" \
  "$ROOT/examples/DSP/dashboard/api/validate_contracts.py"
cp "$ROOT/examples/DSP/dashboard/api/openapi.json" \
  "$ROOT/examples/DSP/dashboard/api/schemas/"*.json \
  "$OUT/evidence/schemas/"

jq '.nodes[] | select(.id == "source") | .node_config' \
  "$ROOT/libdsp/config/fhss_cpsm_channelized_fixture_500msps.json" > "$SCHEDULE"
"$BUILD/examples/DSP/graphx-dsp-fhss-iq-generator" \
  --message-json "$SCHEDULE" --iq-output "$IQ" --truth-output "$TRUTH" \
  --sigmf-meta "$SIGMF"
cp "$SCHEDULE" "$TRUTH" "$SIGMF" "$OUT/evidence/"
shasum -a 256 "$IQ" "$SCHEDULE" "$TRUTH" "$SIGMF" \
  > "$OUT/evidence/input-sha256.txt"
shasum -a 256 -c "$OUT/evidence/input-sha256.txt"
rm "$SCHEDULE" "$TRUTH"
test -f "$IQ" && test -f "$SIGMF"
test ! -e "$SCHEDULE" && test ! -e "$TRUTH"
```

Truth and schedule evidence is retained under `evidence/`, outside the receiver
input paths. Raw IQ and SigMF metadata remain separate files.

## 2. Launch stopped and capture the ephemeral URL

Use the source-built executable for the retained curl/jq evidence. Section 5
then runs the same lifecycle through the installed executable in its isolated
operator output tree.

```sh
DEMO="$BUILD/examples/DSP/graphx-dsp-fhss-demo"
LOG="$OUT/dashboard.log"
"$DEMO" --dashboard --dashboard-port 0 >"$LOG" 2>&1 &
PID=$!
for n in $(seq 1 100); do
  BASE=$(sed -n 's/.*Dashboard URL: \(http[^ ]*\).*/\1/p' "$LOG" | tail -1)
  test -n "${BASE:-}" && break
  sleep 0.1
done
test -n "${BASE:-}"
printf '%s\n' "$BASE" | tee "$OUT/evidence/dashboard-url.txt"
curl -fsS "$BASE/readyz" | tee "$OUT/evidence/readyz.json"
curl -fsS "$BASE/api/v1/fhss/status" | tee "$OUT/evidence/status-initial.json"
jq -e '.lifecycle_state == "not_built" and .active_generation == 0 and
  .terminal_result == null and .stop_requested == false' \
  "$OUT/evidence/status-initial.json"
```

`--dashboard` is the documented production-capability mode: it exposes the
public lifecycle routes but never auto-builds or auto-starts, even when the
configured IQ path exists. `--dashboard-no-run` is inspection-only and hides
those routes, so it is not used for this lifecycle test.

Open `$BASE` in a browser and confirm that configured schedule data is labelled
as expectation, not detection. There must be no decoder-confidence, detected
pulse, Viterbi, or spectrum result presented as an observation.

## 3. Configure the truth-free receiver and run generations 1 and 2

```sh
curl -fsS -D "$OUT/evidence/config.headers" \
  "$BASE/api/v1/fhss/config/authoritative" \
  -o "$OUT/evidence/config-authoritative.json"
ETAG=$(awk 'BEGIN{IGNORECASE=1} /^ETag:/{gsub("\\r",""); print $2}' \
  "$OUT/evidence/config.headers")
jq -n --arg iq "$IQ" '[{op:"add",path:"/receiver_input",value:{
  file_path:$iq,sample_format:"cf32_le",first_complex_sample:0,
  max_complex_samples:0,max_read_complex_samples:4194304}}]' \
  > "$OUT/evidence/patch.json"
curl -fsS -X PATCH -H 'Content-Type: application/json-patch+json' \
  -H "If-Match: $ETAG" --data-binary @"$OUT/evidence/patch.json" \
  "$BASE/api/v1/fhss/config" | tee "$OUT/evidence/patch-response.json"

curl -fsS "$BASE/api/v1/fhss/graph/receiver-minimal" \
  | tee "$OUT/evidence/receiver-minimal.json"
jq -e '[.. | objects | keys_unsorted[] | ascii_downcase |
  select(test("^messages$|truth|generator.*metadata|expected.*(value|word)|(value|word).*expected|transmitted.*frequency|frequency.*transmitted|burst.*epoch|epoch.*burst|^active_frequency_indices$"))]
  | length == 0' \
  "$OUT/evidence/receiver-minimal.json"

for GEN in 1 2; do
  curl -fsS "$BASE/api/v1/fhss/config/authoritative" \
    | tee "$OUT/evidence/config-authoritative-$GEN.json" >/dev/null
  REV=$(jq -r .config_revision \
    "$OUT/evidence/config-authoritative-$GEN.json")
  jq -n --arg id "manual-rebuild-$GEN" --argjson rev "$REV" \
    '{schema:"graphx.dashboard.config_rebuild.v1",command_id:$id,expected_revision:$rev}' \
    > "$OUT/evidence/rebuild-$GEN-request.json"
  curl -fsS -X POST -H 'Content-Type: application/json' \
    --data-binary @"$OUT/evidence/rebuild-$GEN-request.json" \
    "$BASE/api/v1/fhss/config/rebuild" \
    | tee "$OUT/evidence/rebuild-$GEN-response.json"
  jq -n --arg id "manual-start-$GEN" '{command_id:$id}' \
    > "$OUT/evidence/start-$GEN-request.json"
  curl -fsS -X POST -H 'Content-Type: application/json' \
    --data-binary @"$OUT/evidence/start-$GEN-request.json" \
    "$BASE/api/v1/fhss/commands/start" \
    | tee "$OUT/evidence/start-$GEN-response.json"
  for n in $(seq 1 200); do
    curl -fsS "$BASE/api/v1/fhss/status" \
      | tee -a "$OUT/evidence/status-$GEN-poll.ndjson" \
      > "$OUT/evidence/status-$GEN-latest.json"
    printf '\n' >> "$OUT/evidence/status-$GEN-poll.ndjson"
    STATE=$(jq -r .lifecycle_state "$OUT/evidence/status-$GEN-latest.json")
    test "$STATE" = completed && break
    sleep 0.05
  done
  test "$STATE" = completed
  jq -e --argjson gen "$GEN" '.active_generation == $gen and
    .stop_requested == false and .terminal_result.generation == $gen and
    .terminal_result.code == "execution_completed"' \
    "$OUT/evidence/status-$GEN-latest.json"
  curl -fsS "$BASE/api/v1/fhss/metrics" \
    | tee "$OUT/evidence/metrics-$GEN.json"
  curl -fsS "$BASE/api/v1/fhss/diagnostics" \
    | tee "$OUT/evidence/diagnostics-$GEN.json"
done
```

The rebuild endpoint is synchronous and returns HTTP 200 only after publication;
start returns 202. Natural completion above is intentional and distinct for
generation 1 and generation 2.

## 4. Exercise bounded stop on a running replay

```sh
LONG_SCHEDULE="$OUT/long-schedule.json"
LONG_IQ="$OUT/long.cf32"
LONG_TRUTH="$OUT/long-truth.json"
LONG_SIGMF="$OUT/long.sigmf-meta"
jq '.messages[0] as $m | .messages=[range(0;128) as $i |
  ($m + {message_id:(1000+$i),transmit_start_sample:($i*((($m.pulses|length)+1)*6500))})]' \
  "$OUT/evidence/schedule.json" > "$LONG_SCHEDULE"
"$BUILD/examples/DSP/graphx-dsp-fhss-iq-generator" \
  --message-json "$LONG_SCHEDULE" --iq-output "$LONG_IQ" \
  --truth-output "$LONG_TRUTH" --sigmf-meta "$LONG_SIGMF"
cp "$LONG_SCHEDULE" "$LONG_TRUTH" "$LONG_SIGMF" "$OUT/evidence/"
shasum -a 256 "$LONG_IQ" "$LONG_SCHEDULE" "$LONG_TRUTH" "$LONG_SIGMF" \
  > "$OUT/evidence/long-input-sha256.txt"
LONG_BYTES=$(wc -c < "$LONG_IQ" | tr -d ' ')
test "$LONG_BYTES" -ge 120000000
test $((LONG_BYTES / 8)) -ge 15000000
rm "$LONG_SCHEDULE" "$LONG_TRUTH"

curl -fsS -D "$OUT/evidence/config-long.headers" \
  "$BASE/api/v1/fhss/config/authoritative" -o "$OUT/evidence/config-long.json"
ETAG=$(awk 'BEGIN{IGNORECASE=1} /^ETag:/{gsub("\\r",""); print $2}' \
  "$OUT/evidence/config-long.headers")
jq -n --arg iq "$LONG_IQ" '[{op:"replace",path:"/receiver_input",value:{
  file_path:$iq,sample_format:"cf32_le",first_complex_sample:0,
  max_complex_samples:4194304,max_read_complex_samples:4194304}}]' \
  > "$OUT/evidence/patch-long.json"
curl -fsS -X PATCH -H 'Content-Type: application/json-patch+json' \
  -H "If-Match: $ETAG" --data-binary @"$OUT/evidence/patch-long.json" \
  "$BASE/api/v1/fhss/config" > "$OUT/evidence/patch-long-response.json"
curl -fsS "$BASE/api/v1/fhss/config/authoritative" \
  | tee "$OUT/evidence/config-authoritative-3.json" >/dev/null
REV=$(jq -r .config_revision "$OUT/evidence/config-authoritative-3.json")
jq -n --argjson rev "$REV" \
  '{schema:"graphx.dashboard.config_rebuild.v1",command_id:"manual-rebuild-3",expected_revision:$rev}' \
  > "$OUT/evidence/rebuild-3-request.json"
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data-binary @"$OUT/evidence/rebuild-3-request.json" \
  "$BASE/api/v1/fhss/config/rebuild" | tee "$OUT/evidence/rebuild-3-response.json"
jq -n '{command_id:"manual-start-3"}' \
  > "$OUT/evidence/start-3-request.json"
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data-binary @"$OUT/evidence/start-3-request.json" \
  "$BASE/api/v1/fhss/commands/start" \
  | tee "$OUT/evidence/start-3-response.json"
for n in $(seq 1 100); do
  curl -fsS "$BASE/api/v1/fhss/status" \
    | tee -a "$OUT/evidence/status-running-poll.ndjson" \
    > "$OUT/evidence/status-running.json"
  printf '\n' >> "$OUT/evidence/status-running-poll.ndjson"
  curl -fsS "$BASE/api/v1/fhss/metrics" \
    | tee -a "$OUT/evidence/metrics-running-poll.ndjson" \
    > "$OUT/evidence/metrics-running.json"
  printf '\n' >> "$OUT/evidence/metrics-running-poll.ndjson"
  jq -e '.lifecycle_state == "running"' "$OUT/evidence/status-running.json" >/dev/null \
    && jq -e '[.edges[]? | ((.messages_enqueued // 0) +
      (.messages_dequeued // 0))] | (add // 0) > 0' \
      "$OUT/evidence/metrics-running.json" >/dev/null && break
  sleep 0.05
done
test "$(jq -r .lifecycle_state "$OUT/evidence/status-running.json")" = running
jq -e '[.edges[]? | ((.messages_enqueued // 0) +
  (.messages_dequeued // 0))] | (add // 0) > 0' \
  "$OUT/evidence/metrics-running.json"
curl -fsS "$BASE/api/v1/fhss/diagnostics" \
  | tee "$OUT/evidence/diagnostics-running-3.json" >/dev/null
START_NS=$(date +%s)
jq -n '{command_id:"manual-stop-3"}' \
  > "$OUT/evidence/stop-3-request.json"
curl --max-time 6 -fsS -X POST -H 'Content-Type: application/json' \
  --data-binary @"$OUT/evidence/stop-3-request.json" \
  "$BASE/api/v1/fhss/commands/stop" \
  | tee "$OUT/evidence/stop-3-response.json"
END_NS=$(date +%s)
test $((END_NS-START_NS)) -le 5
curl -fsS "$BASE/api/v1/fhss/status" \
  | tee "$OUT/evidence/status-stopped-3.json" \
  | jq -e '.lifecycle_state == "stopped" and .stop_requested == true and
    .terminal_result.generation == 3 and
    .terminal_result.code == "execution_cancelled"'
```

If a graph node does not cooperatively return from `Consume()`, stop returns a
504 `stop_timeout` after five seconds and retains the thread/owner state. It
does not detach or report false completion; final process destruction may wait
for that non-cooperative node.

## 5. Failed rebuild rollback and installed-tree check

Patch `/receiver_input/sample_format` to an unsupported value, capture the non-2xx RFC
9457 response, and verify `/status` still reports generation 3. Restore
`cf32_le` before repeating the procedure with the installed executable.

```sh
curl -fsS -D "$OUT/evidence/config-invalid.headers" \
  "$BASE/api/v1/fhss/config/authoritative" \
  -o "$OUT/evidence/config-authoritative-invalid.json"
ETAG=$(awk 'BEGIN{IGNORECASE=1} /^ETag:/{gsub("\\r",""); print $2}' \
  "$OUT/evidence/config-invalid.headers")
jq -n '[{op:"replace",path:"/receiver_input/sample_format",value:"bad"}]' \
  > "$OUT/evidence/patch-invalid-request.json"
curl -fsS -X PATCH -H 'Content-Type: application/json-patch+json' \
  -H "If-Match: $ETAG" \
  --data-binary @"$OUT/evidence/patch-invalid-request.json" \
  "$BASE/api/v1/fhss/config" > "$OUT/evidence/patch-invalid.json"
curl -fsS "$BASE/api/v1/fhss/config/authoritative" \
  | tee "$OUT/evidence/config-authoritative-after-invalid-patch.json" >/dev/null
REV=$(jq -r .config_revision \
  "$OUT/evidence/config-authoritative-after-invalid-patch.json")
jq -n --argjson rev "$REV" \
  '{schema:"graphx.dashboard.config_rebuild.v1",command_id:"manual-invalid",expected_revision:$rev}' \
  > "$OUT/evidence/rebuild-invalid-request.json"
HTTP=$(curl -sS -o "$OUT/evidence/rebuild-invalid.json" -w '%{http_code}' \
  -X POST -H 'Content-Type: application/json' \
  --data-binary @"$OUT/evidence/rebuild-invalid-request.json" \
  "$BASE/api/v1/fhss/config/rebuild")
test "$HTTP" -ge 400
jq -e '.status >= 400' "$OUT/evidence/rebuild-invalid.json"
curl -fsS "$BASE/api/v1/fhss/status" | tee "$OUT/evidence/status-after-invalid.json" \
  | jq -e '.active_generation == 3'
kill -INT "$PID"
wait "$PID"
PID=""
```

Run the repository's installed-tree replay from the built tree. It installs to
an isolated prefix, launches outside the source asset directory, generates and
isolates new IQ/truth/SigMF inputs, performs all three runtime generations, and
verifies its signed report hashes:

```sh
ctest --test-dir "$BUILD" \
  -R '^fhss_dashboard_phase3_installed_tree_operator$' \
  --output-on-failure
"$VALIDATOR/bin/python" \
  "$ROOT/examples/DSP/dashboard/operator/fhss_dashboard_operator.py" verify \
  --phase 3 \
  --output-dir "$BUILD/fhss-dashboard-installed-phase3-operator-ctest"
cp "$BUILD/fhss-dashboard-installed-phase3-operator-ctest/phase3-report.json" \
  "$OUT/evidence/installed-phase3-report.json"
```

The test is a concrete installed-binary replay, not a build/launch-only check.
The source and installed-tree operator tests are the automated smoke of the
entire stopped → patch → rebuild → start → natural completion / running stop →
rollback flow above; a PASS is required in addition to the retained curl/jq
evidence.

## 6. Final report and independent hash verification

Create the manual report only after every assertion above passes, then hash
every retained input, schema, request, response, log, runtime artifact, and
report. The manifest excludes only itself. `shasum -c` is the independent final
pass and must report every entry `OK`.

```sh
jq -n --arg source_revision "$(git rev-parse HEAD)" \
  --arg dashboard_url "$BASE" --argjson long_iq_bytes "$LONG_BYTES" \
  '{schema:"graphx.fhss.dashboard.phase3.manual.v1",result:"PASS",
    source_revision:$source_revision,dashboard_url:$dashboard_url,
    synthetic_data_only:true,hwil_available:false,
    generations:{natural:[1,2],cancelled:3},long_iq_bytes:$long_iq_bytes}' \
  > "$OUT/evidence/manual-phase3-report.json"
find "$OUT" -type f ! -name evidence-sha256.txt -print0 \
  | sort -z | xargs -0 shasum -a 256 \
  > "$OUT/evidence/evidence-sha256.txt"
shasum -a 256 -c "$OUT/evidence/evidence-sha256.txt" \
  | tee "$OUT/evidence/evidence-sha256-verification.log"
! grep -q 'FAILED' "$OUT/evidence/evidence-sha256-verification.log"
```

The verification log is intentionally not part of the manifest it verifies.
To clean up later, first confirm the ownership marker, then name the two exact
owned roots; never use a wildcard or an unresolved variable:

```sh
test "$(cat "$OUT/.graphx-owned")" = graphx-fhss-dashboard-phase3-owned
rm -rf "$OUT"
```

## Sanitizer lifecycle race reproduction

From a new empty path, configure dashboard-on C++26 with TSAN and run the
registered live-thread lifecycle tests:

```sh
TSAN_BUILD="$ROOT/.graphx-operator/phase3-tsan"
test ! -e "$TSAN_BUILD"
cmake -S "$ROOT" -B "$TSAN_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON -DENABLE_TSAN=ON \
  -DGRAPHX_BUILD_EXAMPLES_SAR=OFF \
  -DGRAPHX_DASHBOARD_CONTRACT_PYTHON="$VALIDATOR_PYTHON"
cmake --build "$TSAN_BUILD" --target test_dsp_example_unit -j4
"$TSAN_BUILD/examples/DSP/test/test_dsp_example_unit" \
  '--gtest_filter=GraphRuntimeSessionOwnerTest.*:FHSSGraphRuntimeOwnerConcurrencyTest.*'
```

## PASS/FAIL worksheet

| Check | PASS/FAIL | Evidence |
|---|---|---|
| C++26 source and install builds | | |
| IQ/truth/schedule/SigMF persisted and hashed separately | | |
| Truth and schedule absent from receiver execution inputs | | |
| Receiver graph has no messages/truth/active-frequency list | | |
| Generation 1 and 2 natural completion separately attributed | | |
| Generation 3 observed running with traffic before bounded stop | | |
| Failed rebuild preserves generation 3 | | |
| Installed launch works outside source tree | | |
| UI has no fabricated receiver observations | | |
| No HWIL/RF qualification claim | | |

Any failed row makes the manual test FAIL. Preserve `$OUT/evidence` for review;
remove only `$OUT` and `$BUILD` when the evidence is no longer required.
