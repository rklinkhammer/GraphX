# GraphX Generic Dashboard Phase 4 Operator Test

Phase 4 validates the single generic GraphX dashboard's typed command,
browser-export, and runtime-metric surfaces. All graphs and events used here
are synthetic or checked-in recorded data. No hardware-in-the-loop (HWIL), RF
hardware, or production-deployment qualification is available or claimed.

## 1. Fresh-clone prerequisite

Perform qualification from a newly downloaded clone so the build is from first
principles. Do not copy a prior build directory, `node_modules`, generated web
asset, virtual environment, or package cache into the clone.

```sh
git clone <authoritative-GraphX-repository-URL> GraphX-phase4-operator
cd GraphX-phase4-operator
git rev-parse HEAD
git status --short
node --version
npm --version
cmake --version
ninja --version
c++ --version
jq --version
openssl version
pkg-config --modversion openssl  # when pkg-config is available
/Applications/Firefox.app/Contents/MacOS/firefox --version  # macOS
firefox --version                                           # Linux
docker --version                                            # sanitizer run only
docker compose version                                      # sanitizer run only
```

Record the exact Node, npm, CMake, Ninja, C++ compiler, jq, OpenSSL, and Firefox
versions used. The qualification configuration below also requires Boost
1.74 or newer; record the Boost version reported by CMake. Record
Docker/Compose versions only when the sanitizer lane is invoked. Use the
supported host toolchain. If Boost, OpenSSL, or another required host package
is absent, stop and have the operator install it on the host; do not create a
Python or Node virtual environment and do not build in `/private/tmp` or a
related path.

## 2. Build from first principles

```sh
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts
cd ../../../..
cd libgraph/web
npm ci
cd ../..
cmake --fresh --preset ninja-debug -DGRAPHX_BUILD_WEB_DASHBOARD=ON
cmake --build build-ninja/ninja-debug --parallel 4
cd libgraph/web
npm run typecheck
npm test
npm run build
npm run check:assets
cd ../..
ctest --test-dir build-ninja/ninja-debug --output-on-failure
git diff --check
```

Record every command, exit status, and any disabled CTest with its exact stated
reason. A disabled data- or hardware-dependent test is not a pass.

Both `npm ci` commands are lockfile-pinned. A genuine fresh clone normally
needs registry access for them; do not add `--offline` unless the operator has
already established that the exact lockfile closure is cached. CMake performs
an offline dependency-tree check for the legacy frontend, so its install must
precede configure.

`GRAPHX_BUILD_WEB_DASHBOARD` defaults to `OFF` and controls the legacy embedded
FHSS dashboard, not the generic `graphx-dashboard` executable or its static
assets, which build and run unconditionally. Phase 4 sets it to `ON` only to
register and exercise the documented legacy-backed compiled-live qualification
matrix. Confirm CTest reports exactly 70 registered tests: 49 enabled and 21
disabled. A different inventory means this procedure is not running against
the documented Phase 4 configuration.

The configured Phase 4 inventory has 21 intentionally disabled tests. Record
them as **disabled**, not passed, with these repository reasons:

- `phase2a_configuration_discover`: its discovery helper is GTest-specific;
  the owned test binary uses Catch2 and is exercised by
  `phase2a_configuration` instead.
- `fhss_dashboard_external_operator`,
  `fhss_dashboard_installed_tree_operator`,
  `fhss_dashboard_phase2_external_operator`,
  `fhss_dashboard_phase2_installed_tree_operator`,
  `fhss_dashboard_phase3_external_operator`,
  `fhss_dashboard_phase3_installed_tree_operator`,
  `fhss_dashboard_phase4_external_operator`,
  `fhss_dashboard_phase4_installed_tree_operator`,
  `fhss_dashboard_phase5_external_operator`,
  `fhss_dashboard_phase5_installed_tree_operator`,
  `fhss_dashboard_phase7_external_operator`,
  `fhss_dashboard_phase7_installed_tree_operator`,
  `fhss_dashboard_phase8_external_operator`, and
  `fhss_dashboard_phase8_installed_tree_operator`: these 14 tests select the
  deleted prototype FHSS DOM; the enabled compiled-live and generic Phase 4
  lanes replace their shared API/lifecycle/browser coverage.
- `fhss_dashboard_phase8_security_static`: this scans the deleted prototype's
  handcrafted JavaScript sink model and is not applicable to the compiled
  generic dashboard.
- `sar_local_only`: the aggregate lane is host-local and may consume optional
  external SAR tooling/data; portable validation uses the enabled owned SAR
  suites.
- `sar_local_only_discovery`: discovery of that same gated host-local binary is
  disabled with its parent lane.
- `sar_example_sarpy_probe_lane`: this is an intentionally host-local contract
  probe of optional reference tooling (and succeeds when those packages are
  absent), not a portable CI or runtime dependency.
- `sar_example_sarpy_integration_lane`: requires local SarPy plus an existing
  CRSD selected by `GRAPHX_SARPY_CRSD_FILE` (and optional NumPy/Matplotlib
  comparison tooling).
- `sar_real_gotcha_local_validation`: requires the ten-product local CRSD
  layout selected by `GRAPHX_SAR_CRSD_ROOT`; no repository dataset or HWIL is
  substituted.

## 3. Start the source dashboard

Choose an unused loopback port and a checked-in graph. In terminal 1, start the
server and leave it running for all browser and REST checks:

```sh
build-ninja/ninja-debug/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --plugins build-ninja/ninja-debug/plugins \
  --port 8080
```

Open `http://127.0.0.1:8080/`. Confirm the title, authoritative node/edge
counts, topology, inspector, typed command palette, runtime-observation panel,
and graph-export control are visible. A configured graph may truthfully report
metrics as unavailable before initialization or before a sample; absence must
never appear as numeric zero.

Before issuing any command, `/api/v1/execution/state` must report exactly the
initial configured identity: `state: "CONFIGURED"`,
`coordinator_revision: 0`, `configured_revision: 0`,
`active_revision: null`, `graph_generation: 1`, and
`configuration_dirty: false`. A mismatch means the process is not the fresh
instance described by this procedure.

Use terminal 2 for every `curl`, `jq`, and browser-harness command below. Do not
background and immediately lose terminal 1's diagnostics. At the end of the
source run, return to terminal 1, press Ctrl-C once, and confirm the process
stops. Repeat that cleanup for the installed-tree server.

## 4. REST contract checks

```sh
curl -fsS http://127.0.0.1:8080/api/v1/graph | jq .
curl -fsS http://127.0.0.1:8080/api/v1/execution/commands | jq .
curl -fsS http://127.0.0.1:8080/api/v1/execution/state | jq .
curl -fsS http://127.0.0.1:8080/api/v1/metrics | jq .
```

Verify:

- `/api/v1/graph` contains the graph in `data` plus one `snapshot` object with
  `coordinator_revision` and deterministic `content_identity`.
- command discovery lists the lifecycle commands and their structured argument
  schemas; there is no shell, command line, path, URL, or raw-JSON command box.
- `/api/v1/metrics` has `schema_version: 1`, generation, active revision,
  sequence/time, availability/reason, schemas, values, and bounded diagnostics.
- `snapshot_time` and non-null `sample_time` are canonical UTC timestamps in
  `YYYY-MM-DDTHH:mm:ss.sssZ` form (exactly three fractional-second digits).
- signed and unsigned 64-bit metric values use canonical decimal strings with
  `scalar_encoding: "decimal_string"`; verify `INT64_MIN`, `INT64_MAX`, and
  `UINT64_MAX` remain exact rather than rounded JSON numbers.
- only `GET /api/v1/metrics` was added. There is no `/api/v2`, WebSocket, SSE,
  long-poll, domain, FHSS, or per-metric route.

## 5. Typed command behavior

Use the buttons and then the palette for valid transitions:

`Configure -> Init -> Start -> Run -> Stop -> Join`

Repeat the same contract outside the page so a rendering issue cannot hide a
server failure. Every body is a typed JSON object; these lifecycle commands
take no arguments:

```sh
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/configure
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/init
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/start
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/run
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/stop
curl -i -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/api/v1/execution/commands/join
```

For any `202 Accepted`, copy the exact relative `Location` response header and
poll it until `completed`, `failed`, or `cancelled`:

```sh
curl -fsS http://127.0.0.1:8080/api/v1/execution/operations/<operation-id> | jq .
```

Poll `/api/v1/metrics` before initialization, while running, after natural
completion, and after stop/join. For the checked-in `minimal_graph.json`,
verify exact `source_1` and `sink_1` identities and the declared typed
counter/gauge fields. Before a sample, after stop, or after reset, values must
be JSON `null` with a reason—not an invented zero. This is the canonical
synthetic generic-provider exercise; no RF or hardware data is involved.

For an accepted asynchronous operation, inspect browser Network traffic and
confirm submission uses `POST /api/v1/execution/commands/{name}` and follows
the returned `Location` under `/api/v1/execution/operations/{id}` at a bounded
cadence. Confirm polling stops at a terminal result. Trigger one invalid or
duplicate transition and confirm the server rejects it truthfully even if a UI
control happened to be enabled. Open command history and confirm it shows
command, operation, status, state, revision, and generation and retains no more
than 128 entries.

If `Init` reports that a plugin cannot be loaded, confirm `--plugins` points to
the current build's `plugins` directory and that the graph node types exist
there. If the page title renders but the topology does not, inspect the browser
console and `/api/v1/graph` before retrying. If metrics remain unavailable
during execution, inspect `availability.reason`, target identities, generation,
sample times, and `diagnostics.rejected`; do not weaken validation or
substitute zero. A `409` lifecycle response normally means the transition is
invalid for the state shown by `/api/v1/execution/state`.

If operation polling returns `404`, the operation may have expired from the
bounded 128-record server retention window; capture command history and rerun
the single command instead of inventing a result. If export fails, inspect the
same `/api/v1/graph` response for its revision/content identity, browser
download permission, the 16 MiB client bound, and console errors; do not use a
second graph fetch as an export oracle. If `graph_generation` changes, discard
all previously displayed metric values and wait for schemas/values bearing the
new generation. Old-generation data must be rejected and must not be rebound.

Use these objective recovery checks for common runtime failure modes:

| Symptom | Likely cause | Operator action | Recovery evidence |
| --- | --- | --- | --- |
| The target or metric is absent and diagnostics report schema rejection | The descriptor was not registered for the active generation, or its exact target tuple differs | Inspect `/api/v1/metrics` schemas, `graph_generation`, and the authoritative graph identity; then configure and initialize with the correct plugins | The exact descriptor and target appear for the current generation and `schema_contract` stops increasing |
| `/api/v1/execution/state` reports `configuration_dirty: true` | The coordinator revision changed after the executor snapshot was configured | Issue `Configure`, wait for its terminal operation, then issue `Init`; do not reuse the old active graph | `configured_revision` equals `coordinator_revision`, `configuration_dirty` is false, and initialization advances generation exactly once |
| Values become unavailable with reason `execution_stopped` | `Stop`/`Join` ended active execution | Start a new valid `Configure -> Init -> Start` sequence and run only after Start succeeds | Execution is running and only fresh current-generation samples become available; stopped-generation values never reappear |
| The page remains visibly paused and Network shows no metric requests | **Pause runtime updates** is still active | Activate **Resume runtime updates** once; do not restart GraphExecutor | The pause label clears, exactly one bounded polling loop resumes, and current state/metrics recover without duplicate requests |

Exercise two valid sequences. The first is
`Configure -> Init -> Start -> Stop -> Join`. The second is
`Configure -> Init -> Start -> Run (accepted) -> Stop -> Run terminal -> Join`.
The harness expands the test fixture's bounded message count before the second
sequence so Run remains in flight, submits cooperative Stop through the same
server-authoritative command API, and then requires both operations to reach a
terminal status. Do not issue Stop after a naturally completed finite Run and
then describe the expected `409` as a failure.

### Finite generic browser matrix

Run the condition-based Firefox oracle against each representative synthetic
graph. Only the minimal graph enables lifecycle execution because it is the
generic runtime-provider fixture. Other rows validate generic rendering,
identity, export, metrics-unavailable behavior, responsive layout, actual
Firefox 200% page zoom, text spacing, and both motion preferences without
requiring domain data files.

| Case | Fixture | Nodes/edges | Lifecycle |
| --- | --- | ---: | --- |
| Minimal | `libgraph/test/config/topologies/minimal_graph.json` | 2 / 1 | yes |
| Nested | `libgraph/test/config/topologies/generic_nested_semantic.json` | 4 / 3 | no |
| Complex | `libgraph/test/config/topologies/complex_network.json` | 9 / 9 | no |
| SAR | `examples/SAR/config/sar_stripmap_fanout.json` | 21 / 23 | no |
| FHSS | `libdsp/config/fhss_phase2_binary_iq_receiver.json` | 75 / 137 | no |

From `libgraph/web`, run the source-tree matrix:

```sh
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/graphx-dashboard --graph ../test/config/topologies/minimal_graph.json --plugins ../../build-ninja/ninja-debug/plugins --port 18200 --screenshot ../../build-ninja/ninja-debug/phase4-browser/source-minimal.png --expect-nodes 2 --expect-edges 1 --exercise-lifecycle true
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/graphx-dashboard --graph ../test/config/topologies/generic_nested_semantic.json --port 18201 --screenshot ../../build-ninja/ninja-debug/phase4-browser/source-nested.png --expect-nodes 4 --expect-edges 3 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/graphx-dashboard --graph ../test/config/topologies/complex_network.json --port 18202 --screenshot ../../build-ninja/ninja-debug/phase4-browser/source-complex.png --expect-nodes 9 --expect-edges 9 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/graphx-dashboard --graph ../../examples/SAR/config/sar_stripmap_fanout.json --port 18203 --screenshot ../../build-ninja/ninja-debug/phase4-browser/source-sar.png --expect-nodes 21 --expect-edges 23 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/graphx-dashboard --graph ../../libdsp/config/fhss_phase2_binary_iq_receiver.json --port 18204 --screenshot ../../build-ninja/ninja-debug/phase4-browser/source-fhss.png --expect-nodes 75 --expect-edges 137 --exercise-lifecycle false
```

After Section 9 installs the build, run the clean installed-tree matrix:

```sh
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard --graph ../test/config/topologies/minimal_graph.json --plugins ../../build-ninja/ninja-debug/plugins --port 18300 --screenshot ../../build-ninja/ninja-debug/phase4-browser/installed-minimal.png --expect-nodes 2 --expect-edges 1 --exercise-lifecycle true
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard --graph ../test/config/topologies/generic_nested_semantic.json --port 18301 --screenshot ../../build-ninja/ninja-debug/phase4-browser/installed-nested.png --expect-nodes 4 --expect-edges 3 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard --graph ../test/config/topologies/complex_network.json --port 18302 --screenshot ../../build-ninja/ninja-debug/phase4-browser/installed-complex.png --expect-nodes 9 --expect-edges 9 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard --graph ../../examples/SAR/config/sar_stripmap_fanout.json --port 18303 --screenshot ../../build-ninja/ninja-debug/phase4-browser/installed-sar.png --expect-nodes 21 --expect-edges 23 --exercise-lifecycle false
npm run test:browser:phase4 -- --dashboard ../../build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard --graph ../../libdsp/config/fhss_phase2_binary_iq_receiver.json --port 18304 --screenshot ../../build-ninja/ninja-debug/phase4-browser/installed-fhss.png --expect-nodes 75 --expect-edges 137 --exercise-lifecycle false
```

Every command must print `phase4 browser PASS`. The `--screenshot` value is an
evidence basename; the harness retains six nonempty PNGs (and validates their
dimensions) for that row:

- `<basename>-reduced-desktop.png`
- `<basename>-reduced-narrow-text-spacing.png`
- `<basename>-reduced-zoom-200.png`
- `<basename>-ordinary-desktop.png`
- `<basename>-ordinary-narrow-text-spacing.png`
- `<basename>-ordinary-zoom-200.png`

The zoom captures are taken only after the Firefox extension reports actual
2x page zoom; they do not use CSS `zoom`. Thus every source and installed row
retains desktop, narrow/text-spacing, and 200%-zoom evidence under both motion
preferences.
The harness fails on count or exact port-tuple mismatch, export mismatch,
unrevoked object URL, lifecycle/polling failure, polling or animation during
pause, page overflow, incorrect motion preference, request outside the exact
allowlist, or browser console/page error. A large graph intentionally not
initialized must show textual metric unavailability, never invented values.
If a row fails, rerun that exact command and retain its terminal output and
six screenshot paths before changing code.

The installed minimal row executes the same asynchronous Run/cooperative Stop
oracle through the installed executable and installed self-hosted assets. Its
explicit `--plugins` directory is a test input containing `SourceTestNode` and
`SinkTestNode`; those test-only fixtures are not copied into or discovered from
the clean installation prefix. The other four installed rows need no node
instantiation and therefore use no build-tree plugins.

## 6. Export behavior

Activate **Export graph snapshot** with the keyboard. Open the downloaded JSON
and verify exactly these wrapper fields:

```json
{
  "artifact": "graphx.graph-export",
  "version": 1,
  "coordinator_revision": 0,
  "content_identity": "...",
  "graph": {}
}
```

Confirm the filename follows
`graphx-graph-r<revision>-<first-12-identity>.json`; parsed `graph` equals the
authoritative `/api/v1/graph` `data`, including exact port kind/value,
`node_config`, and authored generic presentation fields. Confirm metrics,
execution state, command history, selection, and browser preferences are not
inside the export.

## 7. Runtime presentation

When using a graph/node fixture that declares Phase 4 metric descriptors,
confirm a node value appears only on its exact node ID, and an edge value only
on its complete source-node/source-port/target-node/target-port identity.
Duplicate node types or labels must not alias. Select exact nodes and edges and
confirm value, unit, availability/reason, and sample time have textual
equivalents in semantic/inspector surfaces. Collapse a group and inspect a
bundle: compatible declared values may aggregate; incompatible values show
member availability rather than an invented value.

Record the layout-invocation count, allow at least two metric polls, and confirm
the count does not change. Confirm a metric-only update causes no graph PATCH or
execution command. Activate **Pause runtime updates**: polling/animation stops,
the last capture remains visibly labelled paused, and GraphExecutor execution
does not pause. Resume and confirm recovery is announced once.

The automated identity oracle uses these independently authored, checked-in
structures and synthetic metric samples. These examples are test evidence only;
production parsing and rendering remain domain-neutral.

| Structure | Exact representative target | Synthetic sample | Expected text/aggregation |
| --- | --- | --- | --- |
| Minimal 2/1 | node `source_1`; edge `source_1` index `0` -> `sink_1` index `0` | node `queue_depth`, number, `items`, value 3; edge `activity`, number gauge/sum, `events/s`, value 2 | `queue_depth: 3 items; available`; `activity: 2 events/s; available`; aggregate `activity: 1/1 members available; sum 2 events/s` |
| Nested 4/3 | node `source`; edge `source` name `Data` -> `transform_a` index `0` | same metric contracts/values | same text and one-member aggregate, bound only to the named-port tuple |
| Complex 9/9 | node `source_1`; edge `source_1` name `Data` -> `merge_1` name `In0` | same metric contracts/values | same text and one-member aggregate, bound only to both named ports |
| SAR 21/23 | node `src`; edge `src` index `0` -> `window` index `0` | same metric contracts/values | same text and one-member aggregate; no SAR-specific parser rule |
| FHSS 75/137 | node `assembler`; edge `assembler` index `0` -> `sink` index `0` | same metric contracts/values | same text and one-member aggregate; no FHSS/detector/frequency rule |

For every row, change the node ID to an unknown ID while preserving the metric
descriptor. The snapshot must be rejected as non-authoritative, no overlay may
appear on a similarly named/type-matched element, and the later valid exact-ID
sample must recover normally.

## 8. Changed-path accessibility checks

This human worksheet is separate from automated browser/unit qualification. If
the project owner explicitly overrides the human gate, record the override,
date, scope, and authorizing person here; do not rewrite automated results as
human observations or mark unperformed rows `PASS`.

At desktop width, 320 CSS pixels, browser 200% zoom, both motion preferences,
and with WCAG text-spacing overrides, check:

| Check | Expected result | Status | Evidence path | Observation |
| --- | --- | --- | --- | --- |
| Keyboard | Palette, structured fields, pause, export, history, values, and inspector are reachable in order; focus is visible and retained after actions. |  |  |  |
| Names | Every new control has an unambiguous accessible name, role, state, and value. |  |  |  |
| Non-color | Metric identity/value/unit/time/availability and command result remain understandable without color or motion. |  |  |  |
| Reduced motion | Metric animation is disabled while textual values remain current. |  |  |  |
| Reflow/zoom | No page-level horizontal overflow or clipped control at 320 CSS pixels or 200% zoom; wide tables scroll locally. |  |  |  |
| Text spacing | Increased line/letter/word/paragraph spacing does not overlap, clip, or hide controls/status. |  |  |  |
| Contrast/targets | Text, focus, boundaries, statuses, and controls remain discernible; pointer targets are practical. |  |  |  |
| Announcements | Acceptance/completion, export, loss/recovery, pause/resume, and invalidation are concise; samples are not individually announced. |  |  |  |

Enter `PASS`, `FAIL`, or a justified `N/A`, an actual evidence path, and a
row-specific observation. Any `FAIL` is a Phase 4 finding, not a documentation
exception.

On 2026-08-04 the project owner explicitly overrode this required manual
WCAG/human evidence gate and directed Phase 4 to continue. Record its status as
**USER OVERRIDE**, not `PASS` or `N/A`. No independent manual evidence was
completed, and this disposition makes no WCAG conformance or certification
claim.

## 9. Installed-tree repeat

Install into a repository-local prefix, then repeat Sections 3-8 using the
installed executable and installed static assets:

```sh
cmake --install build-ninja/ninja-debug \
  --prefix build-ninja/ninja-debug/phase4-install
build-ninja/ninja-debug/phase4-install/bin/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --plugins build-ninja/ninja-debug/plugins \
  --port 8081
```

The installed executable and assets are under test, but this grouped lifecycle
fixture uses test-only `SourceTestNode`/`SinkTestNode` plugins that are not
installed. The explicit build-tree plugin path is required; verify `Init`
succeeds before continuing the installed-tree command checks.

Hash source-generated and installed `index.html`, JavaScript, and CSS. Record
that they are identical and self-hosted. Attach source and installed desktop,
narrow, zoom, reduced-motion, metric/pause, command-history, and export
evidence to the completed worksheet.
