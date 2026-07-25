# FHSS dashboard bounded-activity operator test

This Phase 4 procedure uses synthetic IQ only. No HWIL, conducted RF,
channel-emulator, independently recorded RF, OTA, live-RF, or production-RF
qualification is available or implied. Screenshots are optional human
evidence, never automated correctness oracles.

## Fresh first-principles build

Download or clone GraphX into a new directory. Do not reuse a developer build,
CMake cache, `node_modules`, installation, or evidence directory. Record:

```sh
git rev-parse HEAD
git status --short
c++ --version
cmake --version
ninja --version
node --version
npm --version
docker version
docker compose version
```

Keep all output under this clone, for example `build-phase4`,
`install-phase4`, and `.graphx-operator/phase4`. Do not use `/tmp`,
`/private/tmp`, or related paths. Use the Node and npm on the host `PATH` when
they satisfy `examples/DSP/dashboard/frontend/toolchain.json`. If any host
tool or locked package is missing, stop and have the operator install it on
the host. Do not silently provision it or create an ad hoc environment.

The non-dashboard native build remains supported and requires no Node:

```sh
cmake -S . -B build-phase4-off -G Ninja -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=OFF
cmake --build build-phase4-off
```

Build the dashboard and an independent installed tree:

```sh
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts --offline
npm run format:check
npm run typecheck
npm test
cd ../../../..
cmake -S . -B build-phase4 -G Ninja -DCMAKE_CXX_STANDARD=26 \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON -DGRAPHX_BUILD_EXAMPLES_DSP=ON
cmake --build build-phase4 --target graphx-dsp-fhss-demo \
  graphx-dsp-fhss-iq-generator test_dsp_example_unit
cmake --install build-phase4 --prefix "$PWD/install-phase4"
```

Generate architecture-conformant raw IQ while keeping evaluator truth and
SigMF metadata separate:

```sh
mkdir -p .graphx-operator/phase4/captures captures
build-phase4/examples/DSP/graphx-dsp-fhss-iq-generator \
  --message-json examples/DSP/fixtures/fhss_demo_messages.json \
  --iq-output .graphx-operator/phase4/captures/fhss_input.cf32 \
  --truth-output .graphx-operator/phase4/captures/fhss_input.truth.json \
  --sigmf-meta .graphx-operator/phase4/captures/fhss_input.sigmf-meta \
  --sample-format cf32_le --force
cp .graphx-operator/phase4/captures/fhss_input.cf32 captures/fhss_input.cf32
```

Start the source-tree dashboard on loopback:

```sh
mkdir -p .graphx-operator/phase4/runtime
build-phase4/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --plugin-dir build-phase4/plugins \
  --dashboard-assets build-phase4/examples/DSP/fhss-dashboard-dist \
  --dashboard-artifact-root .graphx-operator/phase4/runtime \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 18083
```

After stopping the source-tree process with Ctrl-C, run the installed
executable from the fresh clone root so the checked-in relative IQ path remains
defined. Dynamic plugins remain an explicit build-tree input:

```sh
mkdir -p .graphx-operator/phase4/installed-runtime
install-phase4/bin/graphx-dsp-fhss-demo \
  --graph-config install-phase4/share/graphx/config/fhss_phase2_binary_iq_receiver.json \
  --plugin-dir build-phase4/plugins \
  --dashboard-assets install-phase4/share/graphx/fhss-dashboard \
  --dashboard-artifact-root .graphx-operator/phase4/installed-runtime \
  --dashboard --dashboard-host 127.0.0.1 --dashboard-port 18084
```

Record the working directory, executable path, configuration path, plugin
directory, asset directory, artifact root, loopback URL, HTTP status for `/`,
and the asset filenames returned by the installed `index.html`. Confirm the
same single page, activity controls, semantic activity, exact-port identities,
stopped-unavailable state, and post-Rebuild/Start behavior as the source tree.
Record pass/fail for source/install asset-inventory equivalence; differing
content-hashed filenames or missing self-hosted assets are failures.

Docker is an additional reproducible lane, not a replacement for native use:

```sh
docker compose -f containers/dashboard-operator/compose.yaml build
docker compose -f containers/dashboard-operator/compose.yaml up --force-recreate
docker compose -f containers/dashboard-operator/compose.yaml ps
```

Require the published mapping to be `127.0.0.1:8080->8080/tcp`. Open
`http://127.0.0.1:8080/`. Image dependencies remain inside the image and
evidence remains beneath the clone.

## Manual demonstrations

Record expected result, actual result, pass/fail, browser/version, commands,
and optional screenshots for every item.

1. Before Rebuild, confirm the stopped runtime labels activity unavailable;
   it must not render unavailable counters or rates as zero.
2. Rebuild and Start the deterministic binary-IQ receiver. Confirm there is
   one dashboard at `/` and one `/api/v1/fhss` namespace.
3. Observe bounded exact-edge activity through the pipeline. Every available
   rate must state message class, message delta, server interval, and
   `message/s`; no byte rate or generic latency may appear. The displayed
   interval is the full difference between retained server-monotonic sample
   timestamps, so a skipped browser capture expands both the counter delta and
   divisor consistently.
4. Expand the 8×8 detector bank. Inspect detector 0 and detector 63. Confirm
   there are 64 child detectors and the expanded semantic view has 137 exact
   edges.
5. Select `channelizer:63->detector_63:0` and
   `detector_63:0->merge:64`. Confirm those exact numeric ports and identities
   remain intact.
6. Collapse the bank. Confirm each bundle says it is a presentation aggregate,
   enumerates 64 authoritative members, and withholds the total if even one
   member is unavailable.
7. Select **Pause motion**. Confirm semantic counters/rates continue changing
   after refresh or runtime events and GraphX execution continues.
8. Change presentation speed among 0.5×, 1×, and 2×. Confirm displayed metric
   values are unchanged.
9. Enable **Reduce motion explicitly**, then repeat with the operating-system
   reduced-motion setting. Confirm motion stops while class labels, rates,
   availability, badges/text, and selection remain.
10. Exercise deterministic high activity by repeatedly running the synthetic
    fixture or the focused `activity.test.ts` stress fixture. Confirm at most
    32 edges animate, one pattern appears per edge, and the status reports how
    many active edges were represented without motion. Confirm the status also
    reports superseded/coalesced updates and whether one latest update awaits
    promotion.
11. Refresh, disconnect/reconnect, and force a replay gap using the existing
    Phase 3 transport procedure. Confirm coherent resynchronization and that
    stale generation/run/configuration evidence never attaches.
12. Use only the keyboard to select nodes and edges, expand/collapse the bank,
    operate motion controls, and preserve focus during updates. At a 320 CSS
    pixel viewport, confirm semantic content reflows without horizontal page
    loss.
13. Repeat representative checks against the installed-tree dashboard and
    compare its compiled assets and behavior with the source-tree build.
14. Repeat items 1–12 in the Docker lane. Confirm all artifacts and claims
    remain synthetic-only and raw IQ is separate from truth/SigMF metadata.

Inspect the browser DOM during sustained activity and record that topology
node count, edge count, and retained presentation state remain bounded. Metric
updates must not display “Computing deterministic ELK layout”; only initial
load, topology expansion/collapse, and explicit Reset layout may invoke it.

Stop native processes with Ctrl-C. Stop Compose with:

```sh
docker compose -f containers/dashboard-operator/compose.yaml down
```
