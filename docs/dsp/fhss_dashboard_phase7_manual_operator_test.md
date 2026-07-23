# FHSS Dashboard Phase 7 Manual Operator Test

Date: 2026-07-20

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

This procedure validates FHSS investigation-bundle integrity, official SigMF
1.2.6 metadata, truth isolation, and deterministic binary-IQ replay. All data
is synthetic. No hardware-in-the-loop, conducted-RF, OTA, field-capture, or
production-RF qualification is available or implied.

Phase 7 passes only when the source and installed workflows both report
`PASS / final_verified`, all focused and full regressions pass, and every hash
below can be independently reproduced.

## 1. Record the immutable environment

Run from the repository root and choose new, empty output directories.

```sh
export GRAPHX_PHASE7_ROOT="$PWD"
export GRAPHX_PHASE7_BUILD="$PWD/build-ninja/ninja-debug"
export GRAPHX_PHASE7_PYTHON="$PWD/.venv-dashboard-contracts/bin/python"
export GRAPHX_PHASE7_SOURCE_OUT="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-source"
export GRAPHX_PHASE7_PREFIX="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-install"
export GRAPHX_PHASE7_INSTALL_OUT="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-installed"
test ! -e "$GRAPHX_PHASE7_SOURCE_OUT"
test ! -e "$GRAPHX_PHASE7_INSTALL_OUT"
test -x "$GRAPHX_PHASE7_PYTHON"
git status --short
git rev-parse HEAD
cmake --version
ninja --version
python3 --version
jq --version
```

Do not reset, stash, or clean unrelated worktree changes. The operator refuses
a nonempty output directory and cleanup requires its ownership marker.

## 2. Verify host validators and build C++26

The host validator environment must already contain the versions in
`requirements-contracts.lock`. If it does not, stop and have it installed
before qualification.

```sh
"$GRAPHX_PHASE7_PYTHON" -m pip check
"$GRAPHX_PHASE7_PYTHON" \
  examples/DSP/dashboard/api/validate_contracts.py
cmake --build "$GRAPHX_PHASE7_BUILD" --target \
  graphx-dsp-fhss-demo graphx-dsp-fhss-iq-generator test_dsp_example_unit
```

The configure output must identify `-std=c++2c`. Contract validation must say
OpenAPI 3.1.2 passed. The official schema is pinned at:

```text
examples/DSP/dashboard/sigmf/official-v1.2.6/sigmf-schema.json
```

No normal runtime step downloads schemas or tooling.

## Direct HTTP smoke test (without the exercise wrapper)

Bind only loopback and send the matching Origin on every request:

```sh
export GRAPHX_PHASE7_MANUAL="${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-manual"
export GRAPHX_PHASE7_URL="http://127.0.0.1:18777"
export GRAPHX_PHASE7_ORIGIN="$GRAPHX_PHASE7_URL"
mkdir -p "$GRAPHX_PHASE7_MANUAL"
"$GRAPHX_PHASE7_BUILD/examples/DSP/graphx-dsp-fhss-demo" \
  --dashboard --dashboard-host loopback --dashboard-port 18777 \
  --dashboard-no-run \
  --dashboard-assets "$GRAPHX_PHASE7_ROOT/examples/DSP/dashboard" \
  --dashboard-artifact-root "$GRAPHX_PHASE7_MANUAL" \
  >"$GRAPHX_PHASE7_MANUAL/server.log" 2>&1 &
export GRAPHX_PHASE7_PID=$!
curl -fsS -H "Origin: $GRAPHX_PHASE7_ORIGIN" \
  "$GRAPHX_PHASE7_URL/api/v1/fhss/snapshot" | jq -e '.schema'
```

Submit one synthetic job, retain its identifier, and poll that identifier:

```sh
curl -fsS -D "$GRAPHX_PHASE7_MANUAL/job.headers" \
  -H "Origin: $GRAPHX_PHASE7_ORIGIN" -H 'Content-Type: application/json' \
  -H 'Idempotency-Key: manual-job-1' \
  -d '{"request_id":"manual-job-1","timeout_ms":60000}' \
  "$GRAPHX_PHASE7_URL/api/v1/fhss/commands/step" \
  >"$GRAPHX_PHASE7_MANUAL/job-submit.json"
export GRAPHX_PHASE7_JOB_ID="$(jq -er '.job_id' "$GRAPHX_PHASE7_MANUAL/job-submit.json")"
while :; do
  curl -fsS -H "Origin: $GRAPHX_PHASE7_ORIGIN" \
    "$GRAPHX_PHASE7_URL/api/v1/fhss/jobs/$GRAPHX_PHASE7_JOB_ID" \
    >"$GRAPHX_PHASE7_MANUAL/job.json"
  jq -e '.state|IN("completed","failed","cancelled","timed_out")' \
    "$GRAPHX_PHASE7_MANUAL/job.json" >/dev/null && break
  sleep 0.1
done
jq -e '.state=="completed" and .work.generator_invoked and .work.receiver_replay_invoked' \
  "$GRAPHX_PHASE7_MANUAL/job.json"
```

The copy preflight must return HTTP 428 and the exact completed-job byte
estimate. Then export each mode and poll only its returned operation ID.

```sh
curl -sS -o "$GRAPHX_PHASE7_MANUAL/estimate.json" -w '%{http_code}\n' \
  -H "Origin: $GRAPHX_PHASE7_ORIGIN" -H 'Content-Type: application/json' \
  -H 'Idempotency-Key: manual-estimate-1' \
  -d "{\"request_id\":\"manual-estimate-1\",\"bundle_name\":\"manual-unconfirmed\",\"job_id\":\"$GRAPHX_PHASE7_JOB_ID\",\"iq_mode\":\"copy\"}" \
  "$GRAPHX_PHASE7_URL/api/v1/fhss/investigations/exports"
jq -e --argjson n "$(jq '.artifacts.iq.bytes' "$GRAPHX_PHASE7_MANUAL/job.json")" \
  '.estimated_iq_bytes==$n' "$GRAPHX_PHASE7_MANUAL/estimate.json"
for mode in reference copy; do
  confirm=''
  test "$mode" = copy && confirm=',"confirm_copy":true'
  curl -fsS -H "Origin: $GRAPHX_PHASE7_ORIGIN" -H 'Content-Type: application/json' \
    -H "Idempotency-Key: manual-export-$mode-1" \
    -d "{\"request_id\":\"manual-export-$mode-1\",\"bundle_name\":\"manual-$mode\",\"job_id\":\"$GRAPHX_PHASE7_JOB_ID\",\"iq_mode\":\"$mode\",\"timeout_ms\":120000$confirm}" \
    "$GRAPHX_PHASE7_URL/api/v1/fhss/investigations/exports" \
    >"$GRAPHX_PHASE7_MANUAL/export-$mode.json"
  operation_id="$(jq -er '.operation_id' "$GRAPHX_PHASE7_MANUAL/export-$mode.json")"
  while :; do
    curl -fsS -H "Origin: $GRAPHX_PHASE7_ORIGIN" \
      "$GRAPHX_PHASE7_URL/api/v1/fhss/investigations/operations/$operation_id" \
      >"$GRAPHX_PHASE7_MANUAL/export-$mode-terminal.json"
    jq -e '.state|IN("completed","failed","cancelled","timed_out")' \
      "$GRAPHX_PHASE7_MANUAL/export-$mode-terminal.json" >/dev/null && break
    sleep 0.1
  done
  jq -e '.state=="completed"' "$GRAPHX_PHASE7_MANUAL/export-$mode-terminal.json"
done
```

Use the same POST/poll loop for `import-validations` and `replays`, with body
`{"request_id":"manual-<kind>-<mode>","bundle_name":"manual-<mode>",
"timeout_ms":120000}`. Save the four terminal JSON documents. Both replay
results must have `matches_expected:true`; the two semantic hashes must reduce
to one unique line:

```sh
jq -r '.result.semantic_receiver_result_sha256' \
  "$GRAPHX_PHASE7_MANUAL/replay-reference-terminal.json" \
  "$GRAPHX_PHASE7_MANUAL/replay-copy-terminal.json" | sort -u | wc -l
```

Cancel only a returned operation ID. Stop only the recorded PID; do not use a
process-name search or broad kill.

```sh
curl -fsS -X POST -H "Origin: $GRAPHX_PHASE7_ORIGIN" \
  "$GRAPHX_PHASE7_URL/api/v1/fhss/investigations/operations/$operation_id/cancel" | jq .
kill -TERM "$GRAPHX_PHASE7_PID"
wait "$GRAPHX_PHASE7_PID"
test -z "$(find "$GRAPHX_PHASE7_MANUAL/fhss-investigations" -maxdepth 1 -name '.tmp-*' -print -quit)"
```

## 3. Source-tree external workflow

Maintained Firefox must be installed; set `GRAPHX_FIREFOX_BINARY` when it is not
discoverable.

```sh
"$GRAPHX_PHASE7_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py exercise \
  --phase 7 --build-dir "$GRAPHX_PHASE7_BUILD" \
  --output-dir "$GRAPHX_PHASE7_SOURCE_OUT"
"$GRAPHX_PHASE7_PYTHON" \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 7 --output-dir "$GRAPHX_PHASE7_SOURCE_OUT"
jq '{phase,result,evidence_status,failed:[.checks[]|select(.pass==false)]}' \
  "$GRAPHX_PHASE7_SOURCE_OUT/phase7-report.json"
```

Expected: phase `7`, `PASS`, `final_verified`, and an empty `failed` array. The
workflow creates a completed synthetic source job; obtains the copied-IQ byte
estimate; exports reference-only and copied bundles; validates and replays
both; compares semantic receiver-result hashes; and captures four real browser
states (reference complete, copy complete, replay success, and safe failure).
It then runs named negative cases for every separated JSON artifact,
raw IQ, semantic SigMF fields, datatype/sample count, manifest version and
inventory, traversal/sibling/out-of-root paths, symlink/hard-link/FIFO inputs,
collision, and preservation of the earlier committed bundle.

A second public executable instance enables the startup-only
`--dashboard-investigation-qualification` profile. Its fixed sequence proves
quota and copied-IQ size-limit failures, deterministic ENOSPC, cancellation
during hashing and copying and immediately before rename, deterministic
timeout, bounded active-export shutdown, and cleanup after every transition.
The option is visibly marked non-production and is never enabled by the normal
dashboard command or any request field.

## 4. Independently inspect the bundle

```sh
export GRAPHX_PHASE7_BUNDLES="$GRAPHX_PHASE7_SOURCE_OUT/phase5-job-artifacts/fhss-investigations"
find "$GRAPHX_PHASE7_BUNDLES/phase7-reference" -maxdepth 1 -type f -print | sort
find "$GRAPHX_PHASE7_BUNDLES/phase7-copied" -maxdepth 1 -type f -print | sort
jq '{iq_mode,self_contained,synthetic_only,receiver_truth_access,datatype,iq_bytes,sample_count,iq_sha512}' \
  "$GRAPHX_PHASE7_BUNDLES/phase7-reference/manifest.json"
jq '{iq_mode,self_contained,synthetic_only,receiver_truth_access,datatype,iq_bytes,sample_count,iq_sha512}' \
  "$GRAPHX_PHASE7_BUNDLES/phase7-copied/manifest.json"
shasum -a 256 "$GRAPHX_PHASE7_BUNDLES/phase7-reference/manifest.json"
cat "$GRAPHX_PHASE7_BUNDLES/phase7-reference/manifest.sha256"
shasum -a 512 "$GRAPHX_PHASE7_BUNDLES/phase7-copied/recording.sigmf-data"
jq -r '.global["core:sha512"]' \
  "$GRAPHX_PHASE7_BUNDLES/phase7-copied/recording.sigmf-meta"
test -z "$(find "$GRAPHX_PHASE7_BUNDLES/phase7-reference" -type f -name '*.sigmf-data' -print -quit)"
jq -e '[..|objects|keys[]?]|all(. != "messages" and . != "truth" and
  . != "comparison" and . != "generator" and
  . != "active_frequency_indices")' \
  "$GRAPHX_PHASE7_BUNDLES/phase7-reference/receiver-config.json"
jq -n --argjson bytes "$(jq .iq_bytes "$GRAPHX_PHASE7_BUNDLES/phase7-copied/manifest.json")" \
  --argjson samples "$(jq .sample_count "$GRAPHX_PHASE7_BUNDLES/phase7-copied/manifest.json")" \
  --arg datatype "$(jq -r .datatype "$GRAPHX_PHASE7_BUNDLES/phase7-copied/manifest.json")" \
  '$bytes == $samples * (if $datatype=="cf32_le" then 8 else 16 end)'
find "$GRAPHX_PHASE7_BUNDLES/phase7-copied" -type f -exec shasum -a 256 {} \; \
  >"$GRAPHX_PHASE7_SOURCE_OUT/phase7-bundle-files.sha256"
```

The reference bundle must be non-self-contained and contain no
`recording.sigmf-data`; the copied bundle must be self-contained and contain
it. The detached SHA-256 and exact IQ SHA-512 pairs must match. Truth,
observation, comparison, receiver configuration, and receiver result must have
distinct paths, schemas, and hashes. `receiver-config.json` must contain no
messages, schedules, truth, comparison, generator metadata, or
`active_frequency_indices`.

Validate the metadata independently with the pinned upstream schema:

```sh
"$GRAPHX_PHASE7_PYTHON" -c '
import json, os, pathlib
from jsonschema import Draft202012Validator
r=pathlib.Path("examples/DSP/dashboard/sigmf/official-v1.2.6/sigmf-schema.json")
s=json.loads(r.read_text())
for p in pathlib.Path(os.environ["GRAPHX_PHASE7_BUNDLES"]).glob("*/recording.sigmf-meta"):
    Draft202012Validator(s).validate(json.loads(p.read_text())); print("PASS", p)
'
```

### Adversarial matrix and exact checks

Run destructive checks only on a copied scratch bundle. For a semantic edit,
canonicalize the edited JSON, recompute that artifact's `bytes`, SHA-256, and
SHA-512 entry, canonicalize `manifest.json`, and replace `manifest.sha256`.
This helper sequence is the required pattern (replace `FILE` and the jq edit):

```sh
export GRAPHX_PHASE7_SCRATCH="$GRAPHX_PHASE7_BUNDLES/manual-negative"
cp -R "$GRAPHX_PHASE7_BUNDLES/phase7-copied" "$GRAPHX_PHASE7_SCRATCH"
export FILE=provenance.json
jq '.graph_generation += 1' "$GRAPHX_PHASE7_SCRATCH/$FILE" | jq -cS . \
  >"$GRAPHX_PHASE7_SCRATCH/$FILE.new"
mv "$GRAPHX_PHASE7_SCRATCH/$FILE.new" "$GRAPHX_PHASE7_SCRATCH/$FILE"
bytes="$(wc -c <"$GRAPHX_PHASE7_SCRATCH/$FILE" | tr -d ' ')"
sha256="$(shasum -a 256 "$GRAPHX_PHASE7_SCRATCH/$FILE" | awk '{print $1}')"
sha512="$(shasum -a 512 "$GRAPHX_PHASE7_SCRATCH/$FILE" | awk '{print $1}')"
jq --arg f "$FILE" --argjson b "$bytes" --arg h "$sha256" --arg h512 "$sha512" \
  '(.artifacts[]|select(.path==$f)) |= (.bytes=$b|.sha256=$h|.sha512=$h512)' \
  "$GRAPHX_PHASE7_SCRATCH/manifest.json" | jq -cS . \
  >"$GRAPHX_PHASE7_SCRATCH/manifest.json.new"
mv "$GRAPHX_PHASE7_SCRATCH/manifest.json.new" "$GRAPHX_PHASE7_SCRATCH/manifest.json"
shasum -a 256 "$GRAPHX_PHASE7_SCRATCH/manifest.json" | awk '{print $1}' \
  >"$GRAPHX_PHASE7_SCRATCH/manifest.sha256"
```

Submit `manual-negative` to import validation and replay. Both must fail and
the replay result must contain no receiver-construction/rebuild state. Repeat
from a fresh scratch copy for every row:

| Case | Concrete mutation |
|---|---|
| official SigMF only | add unnamespaced `.unexpected=true` to `recording.sigmf-meta`, then recompute all hashes |
| metadata semantics | set datatype `ci16_le`, rate `0`, frequency `-1`, capture start beyond sample count, annotation count beyond sample count, wrong SHA-512, version `9.9.9`, and `core:metadata_only=true`, one at a time |
| separated documents | append one byte independently to truth, observation, comparison, receiver config, receiver result, provenance, actions, and build/API manifest; do not recompute hashes |
| cross-identity | independently alter provenance source job id, source request id, controller epoch, or scenario correlation id while leaving the manifest commitment unchanged; also increment generation/run/config/sample count or alter config etag/build/OpenAPI/schema digest; recompute all artifact and manifest hashes for every case |
| raw IQ | flip byte zero in `recording.sigmf-data`; do not recompute hashes |
| manifest | change sample count/version/schema; use an absolute path, `../` sibling path, duplicate entry, missing declared file, and extra undeclared file |
| file type | replace truth independently with `ln -s`, `ln` hard link, and `mkfifo`; each must fail without blocking |
| reference containment | set external reference components to `["..","outside.cf32"]`, recompute all hashes |

The maintained external workflow additionally executes individually named
quota, copied-size, ENOSPC, timeout, hash-cancel, copy-cancel, pre-rename
cancel, shutdown, collision, and committed-bundle-preservation cases. Prove
that none was skipped:

```sh
jq -r '.checks[]|select(.name|startswith("Phase7 external"))|[.name,.pass]|@tsv' \
  "$GRAPHX_PHASE7_SOURCE_OUT/phase7-report.json"
jq -e '[.checks[]|select(.name|startswith("Phase7 external"))|.pass]|length>=8 and all' \
  "$GRAPHX_PHASE7_SOURCE_OUT/phase7-report.json"
find "$GRAPHX_PHASE7_SOURCE_OUT/phase5-job-artifacts/fhss-investigations" \
  -maxdepth 1 -name '.tmp-*' -print | wc -l
```

The final count is zero. Query the quota endpoint before and after two bundle
exports and verify `retained_bundle_bytes + remaining_bundle_bytes` equals the
advertised total and retained bytes increase by the exact published file
sizes.

### Browser evidence checklist

Open the printed URL in Firefox, select Graph, scroll the Investigation card
into the viewport, and select each required operation: reference complete,
copy complete, replay success, safe failure. Each PNG must visibly show the
card, operation ID, state, datatype/count/bytes/hash where applicable, and
must have a matching state JSON with the same operation ID, browser session,
context, URL, visible bounding rectangle, and PNG SHA-256:

```sh
for c in reference-completed copy-completed replay-success safe-failed; do
  p="$GRAPHX_PHASE7_SOURCE_OUT/phase7-investigation-$c.png"
  s="$GRAPHX_PHASE7_SOURCE_OUT/phase7-investigation-$c.json"
  test "$(shasum -a 256 "$p"|awk '{print $1}')" = "$(jq -r .screenshot_sha256 "$s")"
  jq -e '.operation_id as $id | .visible and (.panel_text|contains($id))' "$s"
done
```

## 5. Installed-tree workflow

```sh
cmake --install "$GRAPHX_PHASE7_BUILD" --prefix "$GRAPHX_PHASE7_PREFIX"
test -f "$GRAPHX_PHASE7_PREFIX/share/graphx/fhss-dashboard/sigmf/official-v1.2.6/sigmf-schema.json"
"$GRAPHX_PHASE7_PYTHON" \
  "$GRAPHX_PHASE7_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py" exercise \
  --phase 7 --build-dir "$GRAPHX_PHASE7_PREFIX" \
  --output-dir "$GRAPHX_PHASE7_INSTALL_OUT"
"$GRAPHX_PHASE7_PYTHON" \
  "$GRAPHX_PHASE7_PREFIX/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py" verify \
  --phase 7 --output-dir "$GRAPHX_PHASE7_INSTALL_OUT"
jq '{result,evidence_status,failed:[.checks[]|select(.pass==false)]}' \
  "$GRAPHX_PHASE7_INSTALL_OUT/phase7-report.json"
```

The installed workflow must not read dashboard assets, schemas, operator code,
or the SigMF schema from the source tree.

## 6. Focused and regression gates

```sh
"$GRAPHX_PHASE7_BUILD/examples/DSP/test/test_dsp_example_unit" \
  --gtest_filter='FhssDashboardInvestigationBundleTest.*:DspFhssIqGeneratorExecutableTest.*'
ctest --test-dir "$GRAPHX_PHASE7_BUILD" --output-on-failure
git diff --check
```

Also run the repository's fresh ASan+UBSan and TSAN profiles. ASan+UBSan must
cover export/import/replay and fault cleanup. TSAN must cover export,
cancellation, publication, and shutdown. A sanitizer skip is not a pass.

Concrete fresh-tree commands (choose new empty directories) are:

```sh
cmake -S . -B ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-asan \
  --target test_dsp_example_unit
${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-asan/examples/DSP/test/test_dsp_example_unit \
  --gtest_filter='FhssDashboardInvestigationBundleTest.*'
cmake -S . -B ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON -DENABLE_TSAN=ON
cmake --build ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-tsan \
  --target test_dsp_example_unit
${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase7-tsan/examples/DSP/test/test_dsp_example_unit \
  --gtest_filter='FhssDashboardInvestigationBundleTest.*'
```

Troubleshooting is fail-closed: HTTP 403 means Origin mismatch; 409 means an
idempotency key or bundle name was reused with different intent; 428 means
copy confirmation is absent; 429 means aggregate quota is exhausted; a stuck
operation is cancelled by its exact ID and the recorded PID is terminated;
schema/digest/cross-identity failures require a fresh export, never hand-editing
the evidence claimed as passing.

## 7. Interactive browser inspection

Run `serve --phase 7` with a new owned output or retain the URL printed during
exercise. On the Graph tab, verify:

- reference-only is the default and says not self-contained;
- copy mode enables an explicit confirmation and displays estimated bytes;
- operation state and cancel control remain bounded;
- bundle name, manifest hash, IQ SHA-512, bytes, datatype, sample count, and
  synthetic-only status are visible; and
- truth, receiver observation, evaluator comparison, receiver-minimal config,
  and receiver result are labeled as separate models.

No panel may describe reference-only metadata as a complete capture or imply
that receiver execution consumed truth.

## 8. Record the result

```text
Dashboard scope: FHSS-specific
Input evidence: synthetic IQ only
HWIL/conducted/OTA evidence: unavailable and deferred
Receiver truth isolation: PASS/FAIL
Focused tests: passed/total
Full regressions: passed/total
Browser/operator workflow: PASS/FAIL
Source workflow: PASS/FAIL
Installed workflow: PASS/FAIL
Production RF qualification: NOT QUALIFIED
Official pinned SigMF/API schemas: PASS/FAIL
Descriptor-bound artifact/path-swap matrix: PASS/FAIL
Cross-artifact identity matrix: PASS/FAIL
Aggregate quota current/remaining accounting: PASS/FAIL
Build/API/executable manifest: PASS/FAIL
Reference bundle recursive raw-IQ absence: PASS/FAIL
Copied datatype stride and sample count: PASS/FAIL
Reference/copy semantic replay equality: PASS/FAIL
Every named negative and cleanup case: PASS/FAIL
Four visibly rendered browser captures: PASS/FAIL
ASan+UBSan: passed/total
TSAN: passed/total
Evidence report SHA-256:
Source manifest SHA-256:
Installed manifest SHA-256:
```

Do not proceed to Phase 8 from this procedure.
