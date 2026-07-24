# FHSS dashboard Phase 3 operator test

This procedure evaluates synthetic software data only. It provides no HWIL,
conducted-RF, OTA, live-RF, or production-RF qualification. Record human
observations separately; do not mark them complete from automated output.

## Fresh first-principles checkout

Clone or download a new repository checkout. Record `git rev-parse HEAD`,
compiler, CMake, Ninja, Node, npm, Docker Engine/Desktop, and Compose versions.
Use new checkout-local `build-phase3`, `install-phase3`, and
`.graphx-operator/phase3` directories. Do not reuse another checkout's
`node_modules`, compiled frontend, CMake cache, installation, or test output.
Do not place builds, dependencies, installation, or evidence under `/tmp`,
`/private/tmp`, or related ephemeral paths.

## Native host lane

An ordinary dashboard-disabled GraphX build has no Node/npm dependency:

```bash
cmake -S . -B build-phase3-off -G Ninja -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF
cmake --build build-phase3-off
```

For a dashboard-enabled build, use Node/npm from the host `PATH`. Versions must
fall within `examples/DSP/dashboard/frontend/toolchain.json`; an exact patch is
not required. Do not create a Python environment for Node, automatically
download a toolchain, or install a missing package. If a host tool or locked
package is absent, stop and ask the operator to install it on the host.

```bash
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts --offline
cd ../../../..
cmake -S . -B build-phase3 -G Ninja -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON -DGRAPHX_BUILD_EXAMPLES_DSP=ON
cmake --build build-phase3 --target dsp_fhss_demo dsp_fhss_iq_generator
cmake --install build-phase3 --prefix "$PWD/install-phase3"
```

Generate the receiver input from the architecture-conformant generator. These
commands deliberately keep raw IQ, truth, and SigMF metadata in separate
files:

```bash
mkdir -p .graphx-operator/phase3/captures
build-phase3/examples/DSP/graphx-dsp-fhss-iq-generator \
  --message-json examples/DSP/fixtures/fhss_demo_messages.json \
  --iq-output .graphx-operator/phase3/captures/fhss_input.cf32 \
  --truth-output .graphx-operator/phase3/captures/fhss_input.truth.json \
  --sigmf-meta .graphx-operator/phase3/captures/fhss_input.sigmf-meta \
  --sample-format cf32_le --force
test -s .graphx-operator/phase3/captures/fhss_input.cf32
test -s .graphx-operator/phase3/captures/fhss_input.truth.json
test -s .graphx-operator/phase3/captures/fhss_input.sigmf-meta
```

The checked-in graph reads `captures/fhss_input.cf32`. Copy only the raw IQ to
that receiver path, then launch the source-built executable with every
repository dependency named explicitly:

```bash
mkdir -p captures .graphx-operator/phase3/runtime
cp .graphx-operator/phase3/captures/fhss_input.cf32 captures/fhss_input.cf32
build-phase3/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --plugin-dir build-phase3/plugins \
  --dashboard-assets build-phase3/examples/DSP/fhss-dashboard-dist \
  --dashboard-artifact-root .graphx-operator/phase3/runtime \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 18083
```

In a separate terminal, prove the stopped → rebuilt → started lifecycle and a
reproducible unsupported-method response:

```bash
BASE=http://127.0.0.1:18083
curl -fsS "$BASE/healthz"
curl -fsS "$BASE/api/v1/fhss/status" | jq -e '.lifecycle_state == "not_built"'
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data '{"expected_revision":1,"command_id":"phase3-rebuild-1"}' \
  "$BASE/api/v1/fhss/config/rebuild" | jq .
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data '{"command_id":"phase3-start-1"}' \
  "$BASE/api/v1/fhss/commands/start" | jq .
test "$(curl -sS -o .graphx-operator/phase3/unsupported.json \
  -w '%{http_code}' -X DELETE "$BASE/api/v1/fhss/metrics")" = 405
```

Stop the source executable with Ctrl-C. Run the installed executable from the
repository root so its IQ path remains explicit. The current install places
the executable, configuration, and compiled dashboard assets in the install
prefix; dynamic plugins remain a build-tree artifact and are therefore named
explicitly:

```bash
install-phase3/bin/graphx-dsp-fhss-demo \
  --graph-config install-phase3/share/graphx/config/fhss_phase2_binary_iq_receiver.json \
  --plugin-dir build-phase3/plugins \
  --dashboard-assets install-phase3/share/graphx/fhss-dashboard \
  --dashboard-artifact-root .graphx-operator/phase3/installed-runtime \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 18084
```

## Docker/Compose lane

Docker is the canonical cross-host first-principles environment, but it is not
a dependency of native GraphX. Image dependencies remain inside the image;
evidence remains under the fresh checkout.

```bash
docker compose -f containers/dashboard-operator/compose.yaml build
docker compose -f containers/dashboard-operator/compose.yaml up --force-recreate
```

Confirm `docker compose ... ps` reports `127.0.0.1:8080->8080/tcp`, then open
`http://127.0.0.1:8080/`. The image entrypoint creates its own separated raw
IQ/truth/SigMF artifacts, starts the receiver stopped, and prints the internal
dashboard URL. Use the same Rebuild and Start controls before expecting runtime
metrics.

## Manual identity and metric checks

1. Confirm there is one dashboard at `/`, one `/api/v1/fhss` namespace, and no
   `/api/v2`, `/v2`, or alternate UI.
2. Rebuild and start the synthetic binary-IQ receiver.
3. Select the source, detector 0, detector 31, detector 63, merge, and sink.
   Confirm configuration, metric, diagnostic, and visual `node_id` values
   agree.
4. Select exact-port edges at detector boundaries. Confirm detector 0, 31, and
   63 map to merge inputs 1, 32, and 64, and confirm each displayed `edge_id`
   is constructed from the visible canonical endpoints and ports.
5. Confirm runtime indices/names are labelled noncanonical and are never used
   when canonical evidence is missing.
6. Compare current and peak queue depth. Confirm current depth is an
   instantaneous message count, peak is a run-bound maximum, and unavailable
   is not rendered as zero.
7. Observe stop/start. Confirm the run epoch changes and old evidence no longer
   attaches. Rebuild and confirm the graph generation changes.
8. Inspect collection/rate availability. A rate may be shown only for a
   positive server-monotonic interval between compatible same-run counters.
9. Confirm transfer/service duration is not labelled queue residence,
   end-to-end, or generic latency.
10. Confirm topology remains read-only and metrics do not change receiver
    execution. Confirm there are no animated edges or moving markers.
11. Exercise the Phase 2 collapsed detector bank, 8x8 expansion, heatmap,
    semantic topology, spectrum, jobs, evidence, and investigation controls.
12. Stop the native process with Ctrl-C and relaunch the same mutation-capable
    dashboard in its initial not-built state. Do not use
    `--dashboard-no-run`, because that read-only server intentionally has no
    runtime owner or Rebuild/Start routes:

    ```bash
    build-phase3/examples/DSP/graphx-dsp-fhss-demo \
      --graph-config libdsp/config/fhss_phase2_binary_iq_receiver.json \
      --plugin-dir build-phase3/plugins \
      --dashboard-assets build-phase3/examples/DSP/fhss-dashboard-dist \
      --dashboard-artifact-root .graphx-operator/phase3/runtime \
      --dashboard --dashboard-host 127.0.0.1 --dashboard-port 18083
    ```

    On that same instance, before pressing Rebuild, run in a separate terminal:

    ```bash
    BASE=http://127.0.0.1:18083
    curl -fsS "$BASE/api/v1/fhss/metrics" |
      jq -e '.active_generation == 0 and
             .identity_availability.state == "unavailable" and
             .graph.availability == "unavailable" and
             .graph.graph_total_enqueued == null and
             (.nodes | length) == 0 and (.edges | length) == 0'
    ```

    Confirm the configured topology and operator workbench remain mounted in
    the browser, while the runtime inspector explicitly says unavailable and
    does not render unavailable counters as zero. Use Rebuild and Start on this
    same mutation-capable instance, then assert restoration:

    ```bash
    curl -fsS "$BASE/api/v1/fhss/metrics" |
      jq -e '.active_generation > 0 and
             .identity_availability.state == "available" and
             .graph.availability == "available"'
    ```

    Confirm the browser inspector returns to available for the same canonical
    topology.
13. Capture one `/api/v1/fhss/snapshot`. Confirm its top-level revision, ETag,
    generation, and run epoch agree with configuration, graph, runtime,
    metrics, and diagnostics; confirm the coherence capture IDs equal their
    nested metric and diagnostic capture IDs. Refresh or resync atomically;
    never correlate records from independent requests.

Perform separate keyboard/focus and 320 CSS-pixel reflow review. Record the
browser/version, steps, observations, failures, and screenshots without
claiming formal WCAG qualification.

Stop the native process with its documented shutdown signal or stop Compose:

```bash
docker compose -f containers/dashboard-operator/compose.yaml down
```
