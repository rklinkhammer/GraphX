# FHSS Dashboard Phase 1 Operator Test

## Scope

This procedure lets an external operator exercise the one compiled GraphX FHSS
dashboard delivered by Phase 1. It uses synthetic IQ only. It provides no HWIL,
conducted-RF, OTA, live-RF, or production-RF qualification.

Record observations separately. Do not mark keyboard, focus, or reflow review
complete unless a human performs it.

## Build

From the repository root, provision the pinned frontend from the controlled
offline cache and run its focused checks. Put the exact pinned executables on
`PATH` first and verify the reported versions are `v24.4.1` and `11.5.2`; do
not use an engine warning as qualification evidence:

```sh
cd examples/DSP/dashboard/frontend
node --version
npm --version
npm ci --ignore-scripts --offline
npm run format:check
npm run typecheck
npm test
npm run build
cd ../../../..
```

Configure and build GraphX in C++26 mode with the dashboard enabled:

```sh
cmake -S . -B build-phase1 -G Ninja \
  -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_DASHBOARD_NODE_EXECUTABLE=/absolute/path/to/node-24.4.1 \
  -DGRAPHX_DASHBOARD_NPM_CLI=/absolute/path/to/npm-11.5.2/bin/npm-cli.js
cmake --build build-phase1 --target graphx-dsp-fhss-demo
```

The CMake build performs the frozen offline frontend installation and creates a
single build-tree asset inventory. A dashboard-disabled build does not invoke
Node or npm.

## Run the synthetic demo

From the repository root, generate the exact `cf32_le` IQ path read by the
checked-in receiver graph. Truth and SigMF metadata remain separate from raw IQ:

```sh
mkdir -p captures
build-phase1/examples/DSP/graphx-dsp-fhss-iq-generator \
  --message-json examples/DSP/fixtures/fhss_demo_messages.json \
  --iq-output captures/fhss_input.cf32 \
  --truth-output captures/fhss_input.truth.json \
  --sigmf-meta captures/fhss_input.sigmf-meta \
  --sample-format cf32_le \
  --force
test -s captures/fhss_input.cf32
test -s captures/fhss_input.truth.json
test -s captures/fhss_input.sigmf-meta
```

Then start the truth-free binary-IQ receiver and dashboard, still from the
repository root so the graph's relative `captures/fhss_input.cf32` path resolves
to the generated file:

```sh
build-phase1/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 8080
```

Open `http://127.0.0.1:8080/`. Stop the demo with Ctrl-C and verify it releases
the port.

## Manual checks

1. Confirm the page identifies itself as `GraphX FHSS Dashboard`, states that
   evidence is synthetic-only, and reports event transport status.
2. Confirm the topology header reports 75 nodes and 137 exact-port edges.
3. Select `source`, `detector_0`, `detector_63`, and `merge`. Confirm the
   inspector reports stable configuration identity, node type, and exact input
   and output ports.
4. Select `detector_0:0->merge:1` and
   `detector_63:0->merge:64`. Confirm source and target ports in the edge
   inspector.
5. Exercise pan, zoom, fit-to-view, minimap, selection, and Reset layout.
   Reload and confirm Reset layout produces the same structure.
6. Drag a node. Confirm the page labels this as local presentation only. Confirm
   connection creation, edge reconnection, deletion, and topology mutation are
   unavailable.
7. Use the Semantic topology section to inspect the same nodes and edges. With
   the keyboard only, use Tab, Enter/Space, Up/Down, Home, and End. Confirm
   canvas and inspector selection remain synchronized and focus remains visible
   across ordinary refresh.
8. At a 320 CSS-pixel viewport, confirm content reflows without loss of
   controls or information. Confirm port and selection meaning is available in
   text and is not conveyed by color alone.
9. In the FHSS operator workbench, refresh configuration, runtime, job,
   observation, comparison, spectrum, investigation, health, readiness,
   diagnostic, and coherent-snapshot resources. Explicit unavailability is an
   acceptable receiver state before a job runs.
10. Exercise configuration Validate. Apply only an intended synthetic
    configuration change. Exercise Rebuild, Start, Stop, Step message, Continue
    2, and Reset; confirm result or error text is exposed in the live status.
11. Disconnect/reconnect the browser briefly. Confirm bounded polling fallback,
    WebSocket reconnect, ordered sequence handling, and coherent resync status.
12. Confirm `/api/v1/fhss/graph` works. Confirm `/api/v2`, `/legacy`, and `/v2`
    return 404 and that no alternate dashboard selector or prototype fallback
    exists.

## Installed-tree check

```sh
cmake --install build-phase1 --prefix /tmp/graphx-phase1-install
/tmp/graphx-phase1-install/bin/graphx-dsp-fhss-demo \
  --graph-config /tmp/graphx-phase1-install/share/graphx/config/fhss_phase2_binary_iq_receiver.json \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 8080
```

Repeat the root, topology cardinality, exact-port, and operator-workbench checks.
The installed frontend should contain one `index.html`, hashed JavaScript and
CSS, no source maps, and no prototype assets.

After testing, remove only the generated synthetic artifacts:

```sh
rm captures/fhss_input.cf32 \
  captures/fhss_input.truth.json \
  captures/fhss_input.sigmf-meta
```

## Expected Phase 1 limitations

- Detector-bank grouping, the collapsed FHSS pipeline, heatmap adapter, and
  optional 8x8 expansion remain Phase 2 work.
- Runtime/metric/diagnostic identity overlays remain Phase 3 work.
- Edge activity animation and motion controls remain Phase 4 work.
- Full release security, fuzz, sanitizer, concurrency, soak, regression, and
  human WCAG campaigns remain roadmap or release-candidate gates.
