# FHSS Dashboard Phase 6 Manual Operator Test

Date: 2026-07-19

## Fresh-clone prerequisite

Run this qualification only from a newly downloaded clone of
`https://github.com/rklinkhammer/GraphX.git`. Record `git rev-parse HEAD` and
require a clean `git status --short` before building. Do not reuse a developer
checkout, build tree, installed dependencies, CMake cache, or prior evidence.
Required packages must already be installed on the host; stop if one is
missing rather than provisioning it under a temporary path.

```sh
export GRAPHX_OPERATOR_ROOT="$PWD/.graphx-operator"
mkdir -p "$GRAPHX_OPERATOR_ROOT"
```

This procedure qualifies bounded RFC 6455 event streaming, deterministic
replay/resynchronization, and the HTTP polling fallback of the loopback-only
FHSS dashboard. It covers both a source build and an installed, source-tree-
independent run. All IQ, messages, impairments, browser traffic, and evidence
are synthetic. No hardware-in-the-loop facility is available; this procedure
makes no HWIL, conducted-RF, OTA, or production-RF claim.

An external operator should record every command, exit status, tool version,
exception, and worksheet result. `PARTIAL` is not acceptance. Phase 6 is
complete only when both applicable lanes finish as `PASS / final_verified`.

## 1. Workspace and immutable inputs

Run from the repository root in one shell. Choose new output and install
directories; the operator refuses a nonempty output directory.

```sh
export GRAPHX_PHASE6_ROOT="$PWD"
export GRAPHX_PHASE6_BUILD="$PWD/build-ninja/ninja-debug"
export GRAPHX_PHASE6_PYTHON="$PWD/.venv-dashboard-contracts/bin/python"
export GRAPHX_PHASE6_SOURCE_OUT="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase6-source"
export GRAPHX_PHASE6_PREFIX="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase6-install"
export GRAPHX_PHASE6_INSTALL_OUT="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase6-installed"
test -f "$GRAPHX_PHASE6_ROOT/CMakePresets.json"
test -x "$GRAPHX_PHASE6_PYTHON"
test ! -e "$GRAPHX_PHASE6_SOURCE_OUT"
test ! -e "$GRAPHX_PHASE6_INSTALL_OUT"
```

Record provenance before changing anything:

```sh
git status --short
git rev-parse HEAD
cmake --version
ninja --version
python3 --version
jq --version
```

Do not clean, reset, stash, or overwrite unrelated worktree changes. The
operator writes only beneath the explicit output directory and refuses cleanup
unless its ownership marker and phase match.

## 2. Locked validator environment

Use the host-installed validator environment. It must already contain the exact
versions in `requirements-contracts.lock`. If it is absent or incomplete, stop
and have it installed before qualification:

```sh
"$GRAPHX_PHASE6_PYTHON" -m pip check
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/api/validate_contracts.py
```

Record the lock file, interpreter version, and installed package listing:

```sh
sha256sum examples/DSP/dashboard/api/requirements-contracts.lock
"$GRAPHX_PHASE6_PYTHON" -m pip freeze
```

Expected contract result: `OpenAPI 3.1.2 authoritative validation: PASS`.
This lane includes negative instances for unknown fields, negative/non-finite/
greater-than-uint64 counters and sequences, unsafe limits, malformed epochs and
identifiers, oversized batches, and unsupported commands.

## 3. C++26 source build and focused test

Configure with the repository preset. GraphX deliberately supplies the C++26
compiler flag directly because some CMake releases do not map `cxx_std_26`.

```sh
cmake --preset ninja-debug
cmake --build "$GRAPHX_PHASE6_BUILD" \
  --target graphx-dsp-fhss-demo test_dsp_example_unit -j2
jq -e 'map(select(.file | endswith("EmbeddedDashboardServer.cpp"))) |
  length == 1 and
  all(.command | test("-std=(c\\+\\+2c|gnu\\+\\+26)"))' \
  "$GRAPHX_PHASE6_BUILD/compile_commands.json"
"$GRAPHX_PHASE6_BUILD/examples/DSP/test/test_dsp_example_unit" \
  --gtest_filter='FhssDashboardEventReplayTest.*'
```

The focused output must include the real non-reading-WebSocket isolation test,
origin and negotiation tests, malformed frame close-code tests, fragmentation,
heartbeat/idle/lifetime, replay/resync, retention and byte bounds, publisher
ingress bounds, and bounded shutdown. Zero selected tests is a failure.

## 4. Automated source-tree qualification

Run the production executable through documented HTTP and WebSocket surfaces.
The exercise launches and terminates every child process itself.

```sh
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py exercise \
  --phase 6 --build-dir "$GRAPHX_PHASE6_BUILD" \
  --output-dir "$GRAPHX_PHASE6_SOURCE_OUT"
jq -e '.phase == 6 and
  ([.checks[] | select(.pass != true)] | length) == 0' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-report.json"
```

The expected pre-capture report is `PARTIAL / partial_pre_browser`, solely
because the three operator-visible browser captures are not yet bound. Every
existing check must already be true.

### 4.1 Wire transcript completeness and hash binding

The raw IQ never contains dashboard truth, and the wire transcript never
contains secrets. It records synthetic loopback protocol evidence separately
from raw IQ. Every WebSocket connection and direction is inventoried. Concise
records are supplemented by bounded lossless canonical-JSONL chunks so all
high-volume frames and commands can be reconstructed, not merely summarized.
Validate bounds, ordered indices, completeness counters, chunk chains, scenario
inventory, and artifact hash:

```sh
jq -e '
  .schema == "graphx.dashboard.phase6-wire-transcript.v1" and
  (.records | length) <= .limits.max_records and
  (.lossless_streams | length) <= .limits.max_streams and
  ([.records[].index] == [range(0; (.records | length))]) and
  ([.lossless_streams[].stream_id] == .proof.lossless_stream_ids) and
  (.proof.lossless_streams_complete == true) and
  ([.lossless_streams[] |
    (.expected_document_count == .recorded_document_count) and
    (.expected_frame_count == .recorded_frame_count) and
    (.chunk_count == (.chunks | length)) and
    ([.chunks[].index] == [range(0; (.chunks | length))])] | all) and
  ([.records[].scenario] | unique | length) >= 12 and
  (.proof.replay_contiguous == true) and
  (.proof.publisher_epoch_changed == true)' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
TRANSCRIPT_DIGEST=$(sha256sum \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json" | awk '{print $1}')
jq -e --arg digest "$TRANSCRIPT_DIGEST" \
  '.artifact_hashes.phase6_wire_transcript == $digest' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-report.json"
```

`verify` bounded-decompresses every chunk, requires canonical JSONL, recomputes
the previous/terminal SHA-256 chain, recounts documents, frames and types,
checks first/last and strictly ordered sequences, and requires both directions
for every exercised connection scenario. The operator also validates a
mutation corpus. Omitted documents, duplicated/reordered chunks, altered count
claims, stream-inventory removal, replay gaps, uint64 overflow, or byte/hash
tampering must not be accepted.

### 4.2 Exact origin, hello, subscribe, and two clients

The accepted browser origin is the exact bound URL, for example
`http://127.0.0.1:64846`; the WebSocket endpoint is the same host and port at
`/api/v1/fhss/events/stream`. The request `Host` is never an authority source.

```sh
jq -e '
  ([.records[] | select(.scenario == "two_client" and .kind == "hello")] |
    length == 2 and all(.payload.schema ==
      "graphx.dashboard.websocket_hello.v1")) and
  ([.records[] | select(.scenario == "two_client" and .kind == "subscribe")] |
    length == 2 and all(.payload.action == "subscribe")) and
  ([.records[] | select(.scenario == "two_client" and .kind == "event") |
    .payload.sequence] as $sequences |
    ($sequences | length) == 2 and
    ($sequences | unique | length) == 1)' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
```

Check each event envelope contains the complete identity tuple:

```sh
jq -e '[.records[] | select(.kind == "event") | .payload |
  has("publisher_epoch") and has("sequence") and has("generation") and
  has("run_epoch") and has("config_revision") and has("config_etag") and
  has("controller_epoch") and has("job_id") and has("correlation_id")] |
  all' "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
```

### 4.3 Resume, retention, and publisher restart

Within-retention replay must be exactly contiguous from `last_sequence + 1`.
Sequence-ahead, expired ranges, and a prior publisher epoch must produce an
explicit `resync_required` and fixed same-origin snapshot route.

```sh
jq -e '
  (.proof.replayed_sequences ==
    [range(.proof.resume_from_sequence + 1;
           .proof.resume_from_sequence + 1 +
           (.proof.replayed_sequences | length))]) and
  any(.records[]; .scenario == "sequence_ahead" and
    .kind == "resync_required" and .payload.reason == "sequence_ahead" and
    .payload.snapshot_url == "/api/v1/fhss/snapshot") and
  any(.records[]; .scenario == "publisher_restart" and
    .kind == "resync_required") and
  (.proof.old_publisher_epoch != .proof.new_publisher_epoch)' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
```

### 4.4 Genuinely stalled WebSocket and healthy progress

The exercise opens a raw RFC 6455 client with a small receive buffer, sends a
valid masked subscribe command, and deliberately performs no reads while
events are produced. A maintained WebSocket drains concurrently. The HTTP slow
client is registered only after the raw WebSocket overflow has been observed,
so it cannot satisfy the stalled-WebSocket proof. The raw client must then
receive `resync_required` and a bounded close after reading resumes; a real Step
job and the healthy WebSocket must continue.

```sh
jq -e '
  any(.records[]; .scenario == "stalled_websocket" and
    .kind == "handshake_response" and .http_status == 101) and
  any(.records[]; .scenario == "stalled_websocket" and
    .kind == "subscribe" and .client_id == "phase6-stalled-ws") and
  any(.records[]; .scenario == "stalled_websocket" and
    .kind == "resync_required") and
  any(.records[]; .scenario == "stalled_websocket" and
    .kind == "close" and .close_code == 1000 and
    (.frame_hex | length) >= 4) and
  any(.records[]; .scenario == "stalled_websocket" and
    .kind == "job_terminal" and .payload.state == "completed") and
  any(.records[]; .scenario == "slow_client_overflow" and
    .kind == "event" and .client_id == "phase6-healthy")' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
jq -e 'any(.checks[]; .name ==
  "slow consumer overflow isolates healthy client and job" and .pass)' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-report.json"
```

### 4.5 Origin and malformed-input matrix

Missing, foreign, malformed, or duplicate Origin, requester-forged Host,
unsupported subprotocol, bad version/key, duplicate key, unmasked data,
oversized messages, too many fragments, invalid UTF-8, unexpected continuation,
fragmented control, malformed resume, and sequence-ahead inputs must fail
safely. Extension offers may upgrade only when no extension is negotiated.

```sh
jq -e '
  any(.records[]; .scenario == "cross_origin" and .http_status == 403) and
  any(.records[]; .scenario == "missing_origin" and .http_status == 403) and
  any(.records[]; .scenario == "duplicate_origin" and .http_status == 403) and
  any(.records[]; .scenario == "forged_host" and .http_status == 403) and
  any(.records[]; .scenario == "subprotocol_offer" and .http_status == 400) and
  any(.records[]; .scenario == "extension_offer" and .http_status == 101) and
  (.proof.negative_close_codes == {
    "continuation":1002, "control":1002, "fragments":1009,
    "malformed_resume":1008, "oversized":1009,
    "unmasked":1002, "utf8":1007})' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
```

### 4.6 Heartbeat, idle close, outage fallback, and shutdown

Heartbeat acknowledgements keep an active client alive; a no-pong client is
closed within the configured idle bound. During a genuine WebSocket-only
outage, HTTP health and snapshots remain available, the rendered page enters
bounded polling, and WebSocket restoration atomically replaces the coherent
model. Shutdown is tested with both a healthy and a stalled connection active.

```sh
jq -e '
  any(.records[]; .scenario == "heartbeat" and .kind == "heartbeat") and
  any(.records[]; .scenario == "heartbeat" and .kind == "heartbeat_ack") and
  any(.records[]; .scenario == "idle_no_pong" and
    .kind == "close" and .close_code == 1008 and
    (.frame_hex | length) >= 4) and
  any(.records[]; .scenario == "websocket_outage" and
    .kind == "outage_poll_restore" and
    (.payload.transitions | map(.state)) ==
      ["live","websocket_unavailable","polling","websocket_restored"] and
    .payload.coherent_return.agrees == true) and
  any(.records[]; .scenario == "bounded_shutdown" and .kind == "shutdown" and
    .payload.elapsed_ms < 8000 and .payload.active_clients_before >= 2 and
    .payload.stalled_socket_closed == true and .payload.process_exit == 0)' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-wire-transcript.json"
```

The transcript and report must also show the no-pong idle-close check. Treat a
missing close frame, counter, identity field, browser transition, or process
exit as a failed lane, not as an evidence limitation.

## 5. Direct Firefox captures and provenance

Phase 6 requires direct maintained-Firefox evidence for `live`, `replay`, and
`resync`. Hand-authored console JSON, a supplied empty message list, placeholder
PNG, or capture from a different served-state URL is rejected. Set
`GRAPHX_FIREFOX_BINARY` only when Firefox is not discoverable on `PATH` or at
the standard macOS path.

For each case, run `serve` in terminal A. It prints the exact loopback URL and
writes the served-state document. Leave it running:

```sh
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py serve \
  --phase 6 --case live --build-dir "$GRAPHX_PHASE6_BUILD" \
  --output-dir "$GRAPHX_PHASE6_SOURCE_OUT"
```

In terminal B, confirm the exact URL and capture only that served state:

```sh
jq -er '.url | select(startswith("http://127.0.0.1:"))' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-live-served-state.json"
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py capture-browser \
  --phase 6 --case live --build-dir "$GRAPHX_PHASE6_BUILD" \
  --output-dir "$GRAPHX_PHASE6_SOURCE_OUT"
```

Stop terminal A with Ctrl+C and confirm no demo remains:

```sh
pgrep -fl 'graphx-dsp-fhss-demo.*--dashboard' && exit 1 || true
```

Repeat the two commands with `live` replaced by `replay`, then `resync`. The
captured page must visibly show, respectively:

1. a monotonically advancing live WebSocket sequence;
2. a contiguous replay through the recorded sequence; and
3. an explicit resync snapshot followed by live transport.

The capture command records the real Firefox name/version/user agent, headless
state, WebDriver BiDi session and context IDs, UTC capture time, served-state
SHA-256, screenshot SHA-256, observed DOM states, and actual console entries.
Verify all three PNGs are non-placeholder and bound to the manifest:

```sh
jq -e '.captured_files | keys | sort == ["live","replay","resync"]' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-screenshot-manifest.json"
for CASE_NAME in live replay resync; do
  test -s "$GRAPHX_PHASE6_SOURCE_OUT/screenshots/$CASE_NAME.png" || exit 1
  file "$GRAPHX_PHASE6_SOURCE_OUT/screenshots/$CASE_NAME.png" | \
    grep -q 'PNG image data' || exit 1
done
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 6 --require-screenshots \
  --output-dir "$GRAPHX_PHASE6_SOURCE_OUT"
jq -e '.result == "PASS" and .evidence_status == "final_verified" and
  ([.checks[] | select(.pass != true)] | length) == 0' \
  "$GRAPHX_PHASE6_SOURCE_OUT/phase6-report.json"
```

## 6. Installed, source-tree-independent qualification

Install binaries, assets, schemas, operator, and helper files to a new prefix.
Then leave the source tree before running the installed operator. Passing a
build directory inside the source tree does not qualify this lane.

```sh
cmake --install "$GRAPHX_PHASE6_BUILD" --prefix "$GRAPHX_PHASE6_PREFIX"
test -x "$GRAPHX_PHASE6_PREFIX/bin/graphx-dsp-fhss-demo"
test -f "$GRAPHX_PHASE6_PREFIX/share/graphx/fhss-dashboard/index.html"
test -f "$GRAPHX_PHASE6_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py"
cd ${GRAPHX_OPERATOR_ROOT}
"$GRAPHX_PHASE6_PYTHON" \
  "$GRAPHX_PHASE6_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py" \
  exercise --phase 6 --build-dir "$GRAPHX_PHASE6_PREFIX" \
  --output-dir "$GRAPHX_PHASE6_INSTALL_OUT"
"$GRAPHX_PHASE6_PYTHON" \
  "$GRAPHX_PHASE6_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py" \
  verify --phase 6 --output-dir "$GRAPHX_PHASE6_INSTALL_OUT"
cd "$GRAPHX_PHASE6_ROOT"
```

Repeat Section 5 using the installed operator, installed prefix as
`--build-dir`, and installed output directory. Final verification must use
`--require-screenshots` and produce `PASS / final_verified`. The report input
hashes must refer to the installed operator, schemas, OpenAPI document,
transport module, and transcript schema; source files must not satisfy an
installed hash.

## 7. Evidence manifest and independent review

Retain these files from each lane:

- `phase6-report.json` and `phase6-wire-transcript.json`;
- `phase6-screenshot-manifest.json`;
- `phase6-{live,replay,resync}-served-state.json`;
- `screenshots/{live,replay,resync}.png`;
- browser console/provenance evidence and content-addressed `artifacts/`;
- build/test/contract-validator logs, package list, git revision, and this
  completed worksheet.

Verify every report artifact digest points to both the named file where one is
required and its content-addressed evidence blob. Raw IQ, truth, receiver
observations, SigMF metadata, browser captures, and wire transcript remain
separate artifacts with separate hashes.

## 8. Troubleshooting and safe recovery

- `refusing preexisting nonempty output directory`: select a new empty path.
  Never delete an unrecognized directory to make the command pass.
- `installed/source independent ... not found`: confirm install completed and
  use the install prefix, not `share/graphx/fhss-dashboard`, as `--build-dir`.
- Origin `403`: use the exact `http://127.0.0.1:<effective-port>` origin from
  the served-state document. `localhost`, a different port, omitted Origin, or
  the request Host does not match.
- `PARTIAL`: inspect `.checks[] | select(.pass != true)`. Only pending genuine
  browser captures are expected before Section 5; any other false check is a
  failure.
- Firefox capture timeout: confirm terminal A is still running, the URL equals
  the served-state URL, and no proxy rewrites loopback. Do not manufacture the
  console file or screenshot.
- `resync_required`: expected only in the explicit old epoch, sequence-ahead,
  retention-gap, overflow, and resync cases. An unexplained resync in live or
  within-retention replay is a failure.
- shutdown failure or surviving PID: preserve logs and transcript, terminate
  only the recorded PID, and mark the lane failed. Do not hide it with cleanup.
- sanitizer/TSAN failure: retain the first diagnostic and exact command; do not
  promote a normal-build pass to acceptance.

## 9. Acceptance worksheet

Record `PASS`, `FAIL`, or `N/A` with an evidence path and reviewer initials.
`N/A` requires written authorization; HWIL is already explicitly out of scope.

| Criterion | Source | Installed | Evidence / notes |
|---|---:|---:|---|
| Pinned authoritative contracts and negative corpus pass |  |  |  |
| C++26 build and focused protocol tests pass |  |  |  |
| Exact Origin, hello, subscribe, and two-client sequence pass |  |  |  |
| Within-retention replay is strictly contiguous |  |  |  |
| Gap, sequence-ahead, and old epoch force coherent resync |  |  |  |
| Actual non-reading WebSocket overflows/resyncs/closes independently |  |  |  |
| Healthy WebSocket and real Step job progress during stall |  |  |  |
| Missing/foreign/duplicate Origin and forged Host are rejected |  |  |  |
| Malformed frame/resume matrix returns exact status/close codes |  |  |  |
| Heartbeat/ack and no-pong idle-close bounds pass |  |  |  |
| WebSocket outage uses bounded polling and coherently restores |  |  |  |
| Healthy plus stalled active-client shutdown is bounded |  |  |  |
| Identity tuples and per-client sequence ordering are complete |  |  |  |
| Transcript bounds, mutation corpus, manifest, and hashes pass |  |  |  |
| Live/replay/resync Firefox captures have direct provenance |  |  |  |
| Final report is `PASS / final_verified` with no false checks |  |  |  |
| Full regression, ASan/UBSan, TSAN, and `git diff --check` pass |  |  |  |
| Synthetic-only/no-HWIL limitation is recorded |  |  |  |

## 10. Cleanup

First confirm there are no serving processes. Cleanup only operator-owned output
using the same installed/source operator that created it:

```sh
pgrep -fl 'graphx-dsp-fhss-demo.*--dashboard' && exit 1 || true
"$GRAPHX_PHASE6_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py cleanup \
  --phase 6 --output-dir "$GRAPHX_PHASE6_SOURCE_OUT"
"$GRAPHX_PHASE6_PYTHON" \
  "$GRAPHX_PHASE6_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py" \
  cleanup --phase 6 --output-dir "$GRAPHX_PHASE6_INSTALL_OUT"
```

Archive accepted evidence before cleanup. The cleanup command is intentionally
unable to remove an unmarked directory or a directory owned by another phase.
