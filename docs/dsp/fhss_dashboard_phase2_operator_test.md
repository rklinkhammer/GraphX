# FHSS Dashboard Phase 2 Operator Test

## Scope and evidence boundary

This procedure lets an external operator examine the Phase 2 FHSS presentation
adapter in the single GraphX dashboard served at `/`. It checks presentation
grouping, exact GraphX identity, read-only behavior, detector/channel
cross-selection, and inherited operator controls.

Every input is synthetic. GraphX has no hardware-in-the-loop facility for this
qualification. This procedure provides no HWIL, conducted-RF, OTA, live-RF, or
production-RF evidence. Human keyboard, focus, and reflow observations must be
recorded by the person performing them; automation must not mark them complete.

## Begin from first principles

Download a new clone. Do not reuse another checkout's `node_modules`, generated
frontend, CMake cache, build directory, install prefix, captures, or test
evidence:

```sh
git clone https://github.com/rklinkhammer/GraphX.git graphx-phase2-operator
cd graphx-phase2-operator
git rev-parse HEAD
git status --short
```

Record the revision. The initial status must be empty. Keep build, install,
dependency, capture, and evidence directories within this clone or another
explicit, persistent operator-owned path. Do not use `/tmp`, `/private/tmp`, or
related ephemeral paths.

Record the available tools:

```sh
c++ --version
cmake --version
ninja --version
node --version
npm --version
docker --version
docker compose version
```

Only record tools for the lane being run. If a required host program is
missing, stop that lane and have it installed on the host. Do not create a
Python environment to supply Node.js, download a temporary executable, or
provision packages into an ephemeral prefix.

## Lane A: native host build and run

The dashboard is optional. First confirm an ordinary dashboard-disabled GraphX
configuration does not require Node.js or npm:

```sh
cmake -S . -B build-phase2-core -G Ninja \
  -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF
cmake --build build-phase2-core
```

For the dashboard-enabled build, Node.js and npm must already be on `PATH`.
`examples/DSP/dashboard/frontend/toolchain.json` defines supported version
ranges; one exact patch release is not required. Install the locked frontend
dependencies explicitly:

```sh
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts --offline
npm run format:check
npm run typecheck
npm test
npm run build
cd ../../../..
```

If the controlled host cache cannot satisfy `npm ci --offline`, stop and request
installation of the locked packages on the host. Do not silently use the
network or a temporary package tree.

Configure, build, test, and install in new directories:

```sh
cmake -S . -B build-phase2-dashboard -G Ninja \
  -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON
cmake --build build-phase2-dashboard --target \
  graphx-dsp-fhss-demo graphx-dsp-fhss-iq-generator
ctest --test-dir build-phase2-dashboard --output-on-failure \
  -R 'dashboard|fhss'
cmake --install build-phase2-dashboard \
  --prefix build-phase2-dashboard/operator-install
```

Generate architecture-conformant synthetic IQ. Raw IQ, evaluator truth, and
SigMF metadata are deliberately separate:

```sh
mkdir -p captures
build-phase2-dashboard/examples/DSP/graphx-dsp-fhss-iq-generator \
  --message-json examples/DSP/fixtures/fhss_demo_messages.json \
  --iq-output captures/fhss_input.cf32 \
  --truth-output captures/fhss_input.truth.json \
  --sigmf-meta captures/fhss_input.sigmf-meta \
  --sample-format cf32_le \
  --force
```

Start the native receiver and its embedded dashboard from the repository root:

```sh
build-phase2-dashboard/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 8080
```

The receiver graph reads binary IQ and contains no scheduled FHSS messages or
generator truth. Open `http://127.0.0.1:8080/`. Stop with Ctrl-C and confirm the
process releases port 8080.

For an installed-tree check, use a different available loopback port and the
installed graph:

```sh
build-phase2-dashboard/operator-install/bin/graphx-dsp-fhss-demo \
  --graph-config \
  build-phase2-dashboard/operator-install/share/graphx/config/fhss_phase2_binary_iq_receiver.json \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 8081
```

Repeat the cardinality, collapse/expand, exact-port, raw-diagnostic, and
inherited-control checks below. The installed assets must agree with the
source-build assets: one `index.html`, hashed JavaScript and CSS, no source
maps, no prototype or alternate implementation.

## Lane B: Docker/Compose first-principles environment

Docker Desktop or Docker Engine with Compose must already be running. Docker
is the canonical cross-host build environment, but it is additive: neither
native GraphX nor the native embedded dashboard requires Docker.

From the fresh clone:

```sh
export GRAPHX_REVISION="$(git rev-parse HEAD)"
mkdir -p .graphx-operator
docker compose -f containers/dashboard-operator/compose.yaml build
docker compose -f containers/dashboard-operator/compose.yaml up
```

The image installs declared dependencies inside the image and builds GraphX in
C++26 mode. It does not provision host tools. Compose publishes only
`127.0.0.1:8080`, and operator artifacts remain under `.graphx-operator` in the
clone.

Open `http://127.0.0.1:8080/`. When finished:

```sh
docker compose -f containers/dashboard-operator/compose.yaml down
```

## Manual presentation checks

Record each observation and whether it was performed in the native,
installed-tree, or Docker lane.

1. Confirm the page title is **GraphX FHSS Dashboard**, the page states the
   synthetic-only boundary, and there is one dashboard at `/`.
2. Confirm the topology reports **75 authoritative nodes** and **137
   authoritative exact-port edges**, while its default operational display has
   12 objects in this order:
   binary IQ source, downconverter, channelizer, acquisition detector bank,
   pulse merge, candidate, CPSM branch metric, Viterbi decoder, word decoder,
   preamble detector, message assembler, sink.
3. Confirm the detector bank is collapsed by default and is labelled as 64
   structurally recognized GraphX nodes. Select it and inspect its 64 member
   identities, common channelizer/merge boundaries, channel range, channelizer
   outputs 0–63, and merge inputs 1–64.
4. Activate **Expand 8×8 detector bank**. Confirm exactly 64 cells appear in
   deterministic physical-channel order. Each cell must provide channel,
   observation state, authoritative node ID, and merge input as text rather
   than color alone.
5. Select channel 0, channel 31, and channel 63. Confirm their detector
   inspector mappings are respectively merge inputs 1, 32, and 64. Confirm
   exact channelizer output, detector input/output, logical channel, physical
   channel, node type, configuration summary, and observation availability.
6. Select the same channels through the 8×8 view, Semantic topology, and
   receiver-spectrum channel selector. Confirm selection cross-references the
   same detector, canvas group, inspector, and physical channel.
7. Before receiver observations are available, confirm each channel says
   **Observation unavailable**, not zero. After observations exist, confirm
   channels with no observed pulses explicitly say **0 observed pulses**.
8. Collapse and re-expand the group. Refresh evidence. Confirm valid selection
   is preserved. If topology changes remove the selected detector, confirm
   selection is cleared deterministically instead of pointing to stale data.
9. Exercise pan, zoom, fit-to-view, minimap, selection, and **Reset layout**.
   Reload and confirm the same collapsed pipeline layout. Dragging may change
   local presentation, but no connect, reconnect, delete, topology-mutation, or
   lifecycle request may result from collapse, expansion, selection, or drag.
10. Open **Advanced diagnostic: full authoritative GraphX JSON**. Confirm the
    complete 75-node/137-edge graph is present as escaped text and is not the
    primary presentation.
11. Exercise representative inherited controls: Refresh all, configuration
    Validate, Rebuild, Start, Stop, Step message, selected-channel spectrum,
    and investigation validation. Confirm explicit success or failure status
    remains visible and a temporarily unavailable resource does not blank the
    page.
12. Confirm `GET /api/v1/fhss/graph` works. Confirm `/api/v2`, `/v2`, and
    `/legacy` return 404. Confirm there is no alternate-UI selector or packaged
    prototype fallback.

## Focused human accessibility and reflow review

These are human observations, not automatically completed conformance claims:

1. Use only Tab, Shift+Tab, Enter/Space, arrow keys, Home, and End to
   expand/collapse the bank and select detectors.
2. Confirm focus is always visible and remains on the selected detector across
   an ordinary evidence refresh.
3. Confirm the Semantic topology exposes the collapsed group and, while
   expanded, all 64 detector identities without making hidden authoritative
   boundary edges independently tabbable.
4. At a 320 CSS-pixel viewport, confirm the pipeline, controls, 64-channel
   detector grid/table, inspector, and raw diagnostic reflow without losing
   information or operation.
5. Confirm detector activity, unavailability, ports, and selection are
   understandable without relying on color or spatial position.

This focused review is not formal WCAG certification. Full human WCAG,
security-hardening, fuzzing, sanitizer/concurrency, soak, and release
qualification remain later roadmap activities.

## Expected Phase 2 limits

- Runtime/metric/diagnostic identity correlation and qualified rate, queue, and
  latency contracts remain Phase 3.
- Bounded edge-activity animation and motion controls remain Phase 4.
- Hardware and RF qualification are unavailable.
