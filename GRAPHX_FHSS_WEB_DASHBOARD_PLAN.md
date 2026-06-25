# GraphX FHSS Embedded Web Dashboard Plan

Date: 2026-06-23  
Repository: `GraphX`  
Status: Updated implementation plan  
Primary rule: **Every implementation phase must leave a runnable embedded webserver.**

## 0. Executive Summary

Build the FHSS dashboard as an optional embedded HTTP/WebSocket application
backed by UI-neutral GraphX snapshot, configuration, and command services.

The implementation must grow as a sequence of runnable vertical slices. The
first displayable slice is not metrics or FHSS stepping: it is the effective
JSON graph itself.

The first browser experience must:

1. Start from `graphx-dsp-fhss-demo`.
2. Serve a local page from the embedded server.
3. Display the loaded effective graph as nodes and edges.
4. Let the user expand each node and inspect:
   - graph node id, name, and type;
   - lifecycle and capability metadata when available;
   - configured `node_config`;
   - current `IParameterized` values when available;
   - parameter descriptions/schema when available;
   - input/output ports and edge token types.
5. Expose the same graph and parameter information through versioned JSON APIs.
6. Support staging configuration changes by RFC 6901 JSON Pointer path.
7. Export the changed effective configuration.

The first implementation must not require a running graph. This is important:
configuration inspection and editing should work after the graph JSON has been
loaded and validated but before execution begins.

The recommended development sequence is:

```text
Server shell
  -> effective JSON graph viewer
  -> parameter inspection and staged JSON Pointer updates
  -> validated graph rebuild/run
  -> generic status and metrics
  -> FHSS message-at-a-time stepping
  -> WebSocket live updates
  -> advanced FHSS visualizations
```

The dashboard must not introduce a second canonical FHSS topology. One
user-visible step means releasing one configured FHSS protocol message and
allowing it to reach a terminal result through the existing canonical graph.

## 1. Review of the Previous Plan

### 1.1 Strong parts to preserve

The previous plan correctly proposed:

- an optional web adapter;
- immutable/versioned snapshots;
- a serialized command service;
- localhost-only binding by default;
- reuse of GraphX metrics, diagnostics, commands, and topology metadata;
- a source-side FHSS message gate rather than arbitrary node-level stepping;
- a small TypeScript frontend;
- artifact export and deterministic tests;
- explicit truth-in-labeling for the deterministic CPU FHSS fixture.

### 1.2 Identified gaps

#### Gap A: No runnable webserver until a late phase

The plan deferred HTTP until after several core refactors. This makes
integration risk accumulate and prevents early browser/API validation.

**Industry-standard solution:** use a walking-skeleton/vertical-slice delivery
model. Phase 1 starts a real server, serves real embedded assets, has a health
endpoint, and displays real graph data. Every later phase extends that same
server.

#### Gap B: The first visible product was too ambitious

The previous minimal frontend included status, metrics, diagnostics, commands,
artifacts, and FHSS schedule controls at once.

**Solution:** make the effective JSON graph the first displayable domain object.
It already exists, is deterministic, and naturally introduces topology,
configuration, parameter metadata, validation, and later execution.

#### Gap C: Configuration and runtime parameter state were conflated

GraphX has several related but distinct surfaces:

- the source graph JSON and `nodes[].node_config`;
- the patched effective graph JSON produced by the FHSS demo;
- `IConfigurable::Configure(JsonView)`;
- `IParameterized::GetParameters()`;
- descriptor/config-field validation;
- facade parameter helpers.

These do not prove that arbitrary parameters are safe to mutate while a node is
initialized or running.

**Solution:** model configuration as a versioned document with explicit apply
semantics:

```text
source config -> effective config -> staged config -> validated config
                                               \-> rebuild and run
```

Runtime mutation is forbidden by default. A future node may opt into a
separate, explicit runtime-mutable contract.

#### Gap D: “JSON path” was underspecified

JSONPath is primarily a query language and has multiple incompatible dialects.
It is a poor mutation contract.

**Solution:** use:

- RFC 6901 JSON Pointer to address one value;
- RFC 6902 JSON Patch for atomic multi-edit requests;
- JSON Merge Patch only for whole-object convenience operations, if needed.

Examples:

```text
/nodes/0/node_config/iq_center_frequency_hz
/nodes/2/node_config/receiver_frequency_indices/24
```

Human-friendly node-id addressing should be translated to canonical JSON
Pointer paths:

```text
node:channelizer/node_config/iq_center_frequency_hz
    -> /nodes/2/node_config/iq_center_frequency_hz
```

The persisted/audited form remains the canonical pointer.

#### Gap E: Node identity and provenance were incomplete

Array positions are unstable after graph edits. The UI needs stable node ids
and must distinguish configured, runtime-reported, default, and staged values.

**Solution:** every node view includes:

- stable graph node `id`;
- configured `node_config`;
- runtime-reported parameters, if available;
- descriptor/config schema;
- value provenance;
- staged differences;
- whether a rebuild is required.

#### Gap F: Parameter update concurrency was not defined

Two browser tabs or CLI/UI operations could overwrite one another.

**Solution:** use optimistic concurrency:

- every effective/staged graph has a monotonically increasing `config_revision`;
- write commands include `expected_revision`;
- stale updates return HTTP `409 Conflict`;
- command/audit history records old value, new value, path, revision, and source.

#### Gap G: Schema coverage is partial

Not every node necessarily implements `IParameterized`, and descriptor metadata
may be incomplete for complex nested objects.

**Solution:** gracefully layer the sources:

1. configured JSON is always displayable;
2. `IParameterized` supplies current values/descriptions where available;
3. descriptor fields supply basic type/required metadata;
4. unknown nested JSON remains editable in raw JSON mode;
5. no UI field is presented as runtime-mutable without an explicit capability.

#### Gap H: Rebuild, reset, and failure recovery were vague

Configuration edits can invalidate the graph or require plugin reconstruction.

**Solution:** make rebuild a first-class command with a transactional boundary:

1. validate staged JSON;
2. attempt construction in a new executor/session;
3. retain the previous valid effective config if validation/build fails;
4. swap sessions only after successful construction;
5. report validation errors with node id and JSON Pointer.

#### Gap I: API hardening details were incomplete

**Solution:** add standard HTTP behavior:

- `/healthz` and `/readyz`;
- API content type checks;
- request/body limits;
- structured errors;
- `ETag`/revision support for graph resources;
- CSP and other local-web security headers;
- no arbitrary filesystem or shell access;
- ephemeral-port support for tests.

## 2. Repository-Aware Foundations

Reuse these existing GraphX surfaces:

- `fhss_demo.cpp` for source/effective configuration and executor creation;
- `GraphManager::GetNodes()`, `GetEdges()`, edge metadata, and metrics;
- `NodeFacadeAdapterWrapper::GetName()`, `GetType()`, and adapter access;
- `IParameterized` for parameter values and descriptions;
- `IConfigurable` for configuration support;
- node descriptors and config-field validation;
- `IDiagnosable` for versioned node diagnostics;
- `GraphManager::DisplayGraphDOT()` only as an export aid, not the browser data
  model;
- `CommandRegistry`/command capability concepts, while replacing placeholder
  status output with real snapshot-backed commands.

Do not use RTTI wrapper names as graph labels when stable graph ids, configured
names, or facade names are available.

## 3. Architectural Boundaries

```text
                         Browser / CLI
                              |
                    HTTP, WebSocket, commands
                              |
                 EmbeddedDashboardServer adapter
                              |
        +---------------------+----------------------+
        |                     |                      |
 ConfigurationService   SnapshotService      DashboardCommandService
        |                     |                      |
 effective/staged JSON   topology/status/      serialized mutations
 validation/revisions    metrics/diagnostics   and execution control
        +---------------------+----------------------+
                              |
                    GraphRuntimeSession owner
                              |
             GraphExecutor / GraphManager / FHSS controller
```

### 3.1 Generic GraphX components

Suggested namespace and paths:

```text
libgraph/include/graph/dashboard/
  DashboardSnapshots.hpp
  GraphConfigurationService.hpp
  GraphSnapshotCollector.hpp
  DashboardCommandService.hpp
  GraphRuntimeSession.hpp
  ArtifactExportService.hpp

libgraph/src/dashboard/
  GraphConfigurationService.cpp
  GraphSnapshotCollector.cpp
  DashboardCommandService.cpp
  GraphRuntimeSession.cpp
  ArtifactExportService.cpp
```

Generic responsibilities:

- load, retain, patch, validate, diff, and export graph JSON;
- expose topology and node-parameter views;
- own one runtime session and its lifecycle;
- serialize commands;
- collect immutable snapshots;
- export generic artifacts.

### 3.2 FHSS-specific components

```text
libdsp/include/dsp/fhss/dashboard/
  FHSSScenarioController.hpp
  FHSSMessageGate.hpp
  FHSSDashboardSnapshots.hpp
  FHSSDashboardCollector.hpp

libdsp/src/dsp/
  FHSSScenarioController.cpp
  FHSSMessageGate.cpp
  FHSSDashboardCollector.cpp
```

Responsibilities:

- schedule validation and progress;
- one-message release;
- message terminal-state correlation;
- channel, pulse, decoder, and capture snapshots.

### 3.3 Optional web adapter

```text
libgraph/include/graph/web/
  EmbeddedDashboardServer.hpp
  DashboardHttpApi.hpp
  DashboardWebSocketHub.hpp
  EmbeddedAssets.hpp

libgraph/src/web/
  EmbeddedDashboardServer.cpp
  DashboardHttpApi.cpp
  DashboardWebSocketHub.cpp

web/dashboard/
  package.json
  src/
  public/
```

The web layer reads immutable snapshots and submits commands. It never mutates
`GraphManager` or nodes directly.

## 4. Configuration and Parameter Model

### 4.1 Configuration states

```text
SourceGraph
    |
FHSS message/capture patching
    v
EffectiveGraph revision N
    |
JSON Pointer/Patch edits
    v
StagedGraph revision N+1
    |
validation + executor construction
    v
ActiveGraph revision N+1
```

### 4.2 Node parameter view

Each topology node returns:

```json
{
  "id": "channelizer",
  "name": "ChannelizerNode",
  "type": "ChannelizerNode",
  "lifecycle_state": "not_built",
  "capabilities": {
    "configurable": true,
    "parameterized": true,
    "diagnosable": false,
    "runtime_mutable": false
  },
  "parameters": {
    "configured": {},
    "runtime_reported": {},
    "staged": {},
    "descriptions": {},
    "provenance": {
      "iq_center_frequency_hz": "effective_config"
    }
  },
  "ports": {
    "inputs": [],
    "outputs": []
  }
}
```

The node dropdown initially shows a compact summary and expands to tabs:

- Parameters
- Raw `node_config`
- Parameter metadata/schema
- Ports and connected edges
- Diagnostics, when later available
- Configuration diff

### 4.3 CLI parameter commands

Add configuration-oriented commands to the FHSS demo or a reusable GraphX CLI
wrapper:

```bash
# Read one value
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --get-config /nodes/2/node_config/iq_center_frequency_hz \
  --no-run

# Stage one JSON value. The value is parsed as JSON, not as a plain string.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --set-config /nodes/2/node_config/iq_center_frequency_hz=1240000000.0 \
  --write-effective-config /tmp/effective.json \
  --no-run

# Prefer a stable node-id alias for humans.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --set-node-config channelizer:/iq_center_frequency_hz=1240000000.0 \
  --write-effective-config /tmp/effective.json \
  --no-run

# Apply an atomic RFC 6902 patch file.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --config-patch changes.json \
  --validate-config \
  --write-effective-config /tmp/effective.json \
  --no-run
```

Recommended parsing rule:

```text
PATH=JSON_VALUE
```

Everything after the first `=` is parsed as JSON. This preserves booleans,
numbers, arrays, objects, strings, and null without type guessing.

Do not use shell-like dotted paths as the canonical format. If a convenience
syntax is added, translate it to JSON Pointer and show the translated path.

### 4.4 Parameter update API

```text
GET   /api/v1/config
GET   /api/v1/config/value?pointer=/nodes/2/node_config/...
GET   /api/v1/nodes/{nodeId}
GET   /api/v1/nodes/{nodeId}/parameters
PATCH /api/v1/config
POST  /api/v1/config/validate
POST  /api/v1/config/rebuild
POST  /api/v1/config/export
```

Single update:

```json
{
  "schema": "graphx.dashboard.config_update.v1",
  "command_id": "cmd-config-17",
  "expected_revision": 4,
  "pointer": "/nodes/2/node_config/iq_center_frequency_hz",
  "value": 1240000000.0,
  "apply": "staged"
}
```

Atomic JSON Patch:

```json
{
  "schema": "graphx.dashboard.config_patch.v1",
  "command_id": "cmd-config-18",
  "expected_revision": 4,
  "operations": [
    {
      "op": "replace",
      "path": "/nodes/2/node_config/iq_center_frequency_hz",
      "value": 1240000000.0
    }
  ]
}
```

Response:

```json
{
  "schema": "graphx.dashboard.config_result.v1",
  "command_id": "cmd-config-18",
  "status": "staged",
  "old_revision": 4,
  "new_revision": 5,
  "rebuild_required": true,
  "validation": {
    "valid": true,
    "errors": []
  }
}
```

### 4.5 Apply modes

- `staged`: update the staged document only.
- `validate`: stage and run graph/config validation.
- `rebuild`: validate and construct a replacement runtime session.
- `runtime`: unavailable unless a future explicit runtime-mutable capability
  declares the path safe for the current lifecycle state.

`IConfigurable` alone is not evidence that running-node mutation is safe.

## 5. First Displayable Dashboard

The initial page is a graph/configuration explorer.

```text
+---------------------------------------------------------------+
| GraphX FHSS Dashboard | config revision 1 | not running       |
+-------------------------------+-------------------------------+
| Graph canvas/list             | Selected node                 |
|                               | channelizer                   |
| source -> downconverter       | [Parameters v]                |
|        -> channelizer         | [Raw JSON v]                  |
|        -> detector_00 ...     | [Schema v]                    |
|                               | [Ports/Edges v]               |
+-------------------------------+-------------------------------+
| Validation: valid | staged changes: 0 | Export | Build & Run  |
+---------------------------------------------------------------+
```

For the 75-node FHSS graph, the UI must support:

- pan/zoom or a virtualized hierarchical list;
- collapse of the 64 repeated detector nodes into a visual group;
- expansion to individual detector nodes;
- search/filter by id or type;
- deterministic layout;
- raw JSON fallback.

The API—not the rendered SVG—is the source of truth.

## 6. Initial API

Available in the first runnable phase:

```text
GET  /healthz
GET  /readyz
GET  /
GET  /api/v1/version
GET  /api/v1/graph
GET  /api/v1/config
GET  /api/v1/nodes/{nodeId}
GET  /api/v1/nodes/{nodeId}/parameters
PATCH /api/v1/config
POST /api/v1/config/validate
POST /api/v1/config/export
```

Later additions:

```text
POST /api/v1/config/rebuild
GET  /api/v1/operations/{operationId}
GET  /api/v1/status
GET  /api/v1/metrics
GET  /api/v1/metrics/edges
GET  /api/v1/diagnostics
GET  /api/v1/fhss/scenario
GET  /api/v1/fhss/messages
GET  /api/v1/fhss/channels
GET  /api/v1/fhss/pulses

POST /api/v1/commands/start
POST /api/v1/commands/stop
POST /api/v1/commands/step-message
POST /api/v1/commands/continue
POST /api/v1/commands/reset
POST /api/v1/commands/capture
POST /api/v1/commands/export

WS   /api/v1/events
```

### 6.1 Command execution contract (automation default)

For command-style endpoints (`/api/v1/commands/*`, rebuild, export, capture),
use one consistent asynchronous contract:

- server returns `202 Accepted` with `operation_id`, `command_id`, `status`,
  and `submitted_revision`;
- `GET /api/v1/operations/{operation_id}` returns terminal status, result
  summary, and structured failure details when applicable;
- WebSocket events mirror operation state transitions (`queued`, `running`,
  `succeeded`, `failed`, `cancelled`);
- idempotency key behavior is preserved through `command_id` for safe retries;
- short synchronous completion is allowed, but the response schema remains the
  same and includes a terminal status.

Commands must never block HTTP worker threads for long-running execution.

## 7. FHSS Message-Stepping Model

Definitions:

- A scheduled FHSS message is a logical entry in `messages[]`.
- A GraphX token is a transport object on one graph edge.
- A pulse is one preamble/body pulse within a scheduled message.
- An executor lifecycle operation is not a dashboard step.

The current source emits one whole generated fixture. Refactor the canonical
source so normal mode preserves that behavior and dashboard mode can release
one scheduled message:

```cpp
if (!message_gate) {
    return ProduceWholeFixtureOnce();
}

auto message = message_gate->AcquireReleasedMessage();
if (!message) {
    return std::nullopt;
}

return GenerateSyntheticIqForMessage(config, *message);
```

Message state:

```text
Pending -> Released -> InFlight -> Completed
                              \-> Rejected
                              \-> TimedOut
```

A `step-message` command succeeds only when exactly one released message reaches
a terminal state at the FHSS sink/controller. Source emission alone is not
completion.

Do not advertise arbitrary node single-step, executor pause, or live topology
mutation.

## 8. Generic Dashboard Panels

After the graph/config explorer is stable, add:

1. Run status
2. Aggregate and per-edge metrics
3. Node diagnostics
4. Logs and command history
5. Artifact export/browser

All generic panels consume reusable versioned snapshots.

## 9. FHSS-Specific Panels

Add incrementally:

1. Message schedule and step controls
2. 64-channel activity heatmap
3. Expected/detected pulse timeline
4. CPSM/Viterbi decoder view
5. Selected-channel IQ/spectrum view

Always display:

> Deterministic GraphX CPU FHSS fixture. Not a production RF receiver or
> production channelizer.

## 10. Thread and Ownership Model

Rules:

1. One `GraphRuntimeSession` owns executor lifecycle.
2. One serialized command queue performs mutations.
3. HTTP threads only parse requests, read immutable snapshots, and enqueue
   commands.
4. Configuration document updates are protected by revisioned transactions.
5. Snapshot collection never blocks graph worker threads.
6. WebSocket clients receive bounded, rate-limited snapshots/events.
7. Shutdown order:
   - stop accepting HTTP/WebSocket work;
   - close event subscribers;
   - stop snapshot timer;
   - reject/flush commands;
   - stop and join runtime session;
   - join server threads.

## 11. Technology Recommendation

Use a permissively licensed C++ HTTP/WebSocket library selected through a short
spike. Boost.Beast remains the conservative recommendation when an optional
Boost dependency is acceptable because it provides explicit lifecycle,
HTTP/WebSocket support, limits, and testable asynchronous behavior.

For the frontend:

- TypeScript;
- Vite;
- Cytoscape.js for topology;
- no large application framework for the first slices;
- embed production assets into the executable;
- keep a development mode that serves assets from disk for rapid iteration.

Dependency versions and licenses must be recorded in the repository.

## 12. Build and Runtime Integration

```cmake
option(GRAPHX_BUILD_WEB_DASHBOARD
       "Build the optional embedded GraphX web dashboard"
       OFF)
```

Development run:

```bash
cmake -S . -B build-dashboard \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_BUILD_EXAMPLES_DSP=ON

cmake --build build-dashboard --target dsp_fhss_demo

./build-dashboard/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_cpsm_channelized_fixture_500msps.json \
  --plugin-dir build-dashboard/plugins \
  --dashboard \
  --dashboard-port 8080 \
  --dashboard-no-run
```

Naming consistency: CMake target `dsp_fhss_demo` produces executable
`graphx-dsp-fhss-demo`; use the executable name in user-facing run examples.

Required options:

```text
--dashboard
--dashboard-bind 127.0.0.1
--dashboard-port 8080
--dashboard-port 0              # ephemeral test port
--dashboard-run-dir PATH
--dashboard-no-run              # inspect/edit graph before execution
--dashboard-open-browser        # optional convenience, default off
```

If dashboard support was not compiled, `--dashboard` returns a clear error.

## 13. Runnable Vertical-Slice Plan

Every phase below must satisfy the common “server remains runnable” contract:

- executable starts with `--dashboard`;
- static page loads;
- `/healthz` returns 200;
- `/readyz` accurately reports readiness;
- server supports port `0`;
- clean shutdown is tested;
- existing headless FHSS behavior remains available.

### Step 1 — Server shell and effective JSON graph display

Deliver:

- optional server dependency/build target;
- embedded minimal HTML/CSS/JS;
- health/readiness/version endpoints;
- load and retain the FHSS effective graph without executing it;
- `/api/v1/graph` and `/api/v1/config`;
- graph node/edge display;
- per-node dropdown with configured parameters/raw JSON/ports;
- deterministic grouping of 64 detector nodes;
- real graph ids and types.

CLI:

- `--dashboard-no-run`
- `--get-config`
- `--write-effective-config`

Acceptance:

- browser displays the canonical JSON graph;
- selecting every node is safe, including nodes without parameters;
- curl can retrieve the same graph JSON;
- server is runnable and useful before executor integration.

### Step 2 — Parameter metadata and staged configuration editing

Deliver:

- `IParameterized` values/descriptions where available;
- descriptor/config-field metadata;
- JSON Pointer read/update;
- JSON Patch transaction support;
- config revision and `409` stale-write handling;
- staged diff, undo last edit, discard all edits;
- validation errors tied to node id and pointer;
- export staged/effective JSON.

CLI:

- `--set-config PATH=JSON`
- `--set-node-config NODE_ID:POINTER=JSON`
- `--config-patch FILE`
- `--validate-config`

Acceptance:

- the same update produces equivalent output through CLI and HTTP;
- invalid type/unknown field is rejected by existing descriptor/config
  validation where metadata exists;
- no running node is silently reconfigured.

### Step 3 — Transactional graph build and generic runtime status

Deliver:

- `GraphRuntimeSession`;
- validate/build/rebuild commands;
- retain previous valid config on failure;
- real lifecycle status;
- start/stop/execute behavior appropriate to FHSS completion;
- browser Build & Run controls.

Acceptance:

- edited config can build and run;
- invalid rebuild does not destroy the last valid configuration/session;
- runtime status is no longer placeholder text.

### Step 4 — Generic metrics, topology activity, and diagnostics

Deliver:

- aggregate GraphMetrics snapshot;
- per-edge metrics using edge metadata;
- node diagnostics enumeration;
- topology edge coloring;
- CLI commands backed by the same snapshot services.

Acceptance:

- browser and CLI report the same snapshot values;
- no dashboard-only metrics implementation;
- server remains runnable with graph stopped or completed.

### Step 5 — FHSS scenario controller and one-message stepping

Deliver:

- message gate/controller;
- one configured message per step;
- message state correlation to terminal sink result;
- reset and continue;
- non-web tests and browser controls.

Acceptance:

- one step releases exactly one scheduled FHSS message;
- canonical topology remains singular;
- default non-dashboard run preserves whole-fixture behavior.

### Step 6 — WebSocket live snapshots

Deliver:

- `/api/v1/events`;
- status, metrics, diagnostics, command, and FHSS progress events;
- bounded subscriber queues, coalescing, and rate limits;
- reconnect with latest snapshot.
- event envelope fields: `schema`, `event_type`, `sequence`, `timestamp`,
  optional `revision`, and event `payload`.
- reconnect cursor via last-seen `sequence` with explicit resync-required
  signaling when retention is exceeded.

Acceptance:

- sequence numbers are monotonic;
- slow clients cannot block runtime threads;
- clean shutdown closes clients.

### Step 7 — FHSS schedule, channel heatmap, and pulse timeline

Deliver:

- schedule panel;
- 64-channel summary;
- expected/detected timeline;
- confidence/rejection visualization;
- pagination/windowing.

Acceptance:

- deterministic fixture renders consistently;
- snapshot sizes and refresh rates remain bounded.

### Step 8 — Decoder and signal investigation views

Deliver:

- branch/path margin and Viterbi diagnostics;
- selected-channel preview;
- opt-in SigMF capture;
- artifact bundle.

Acceptance:

- no raw full-run IQ is streamed through JSON;
- capture paths stay inside approved run directory;
- fixture truth-in-labeling remains visible.

## 14. Test Strategy

### Common server tests for every step

- starts on ephemeral port;
- `/healthz`;
- `/readyz`;
- root asset load;
- malformed request handling;
- request size limit;
- clean shutdown and thread join;
- dashboard-disabled build/headless regression.

### Graph/config tests

- topology JSON matches effective graph ids/edges;
- detector grouping does not alter API data;
- parameter dropdown handles missing `node_config`;
- JSON Pointer escaping (`~0`, `~1`);
- arrays and nested objects;
- atomic patch rollback;
- stale revision conflict;
- CLI/HTTP equivalence;
- export/reload round trip;
- descriptor validation error path.
- deterministic replay controls: fixed seed, fixed clock source, UTC timezone,
  and stable locale for CI runs.

### Runtime tests

- transactional rebuild;
- previous valid session retained on failure;
- real status transitions;
- metrics/diagnostics snapshot consistency.

### FHSS tests

- one step equals one scheduled message;
- ordered continue;
- timeout/rejection;
- reset;
- canonical topology unchanged;
- existing FHSS decode regression.

### Browser tests

Use a small browser automation smoke suite:

- load graph;
- select node;
- expand parameter dropdown;
- stage a value;
- observe diff;
- validate;
- export/rebuild;
- later step one message and inspect completion.
- reconnect/gap test: force sequence gap and verify required HTTP resync before
  stream resume.

## 15. Security and Operational Standards

- bind to `127.0.0.1` by default;
- require explicit external binding;
- warn prominently on external binding;
- no shell execution;
- no arbitrary file browsing;
- artifact/config paths restricted to approved roots;
- CSP header default:
  `default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' ws:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'`.
- `X-Content-Type-Options: nosniff`;
- `X-Frame-Options: DENY`;
- `Referrer-Policy: no-referrer`;
- strict `Content-Type` validation for request and response payloads;
- request body limit: 1 MiB default for JSON endpoints;
- WebSocket frame limit: 256 KiB; max reassembled message: 1 MiB;
- normalized/validated URL paths;
- structured audit log for mutation commands;
- generated command ids when omitted;
- idempotency handling for repeated command ids.

If HTTPS is enabled in any non-localhost deployment profile, WebSocket
connections must use `wss:` and the CSP `connect-src` policy must be updated to
`'self' wss:` for that profile.

Authentication is unnecessary for localhost-only MVP. If external binding is
ever supported beyond trusted development, add token authentication and CSRF
protection before treating it as generally safe.

## 16. Artifact and Audit Model

Each run/export directory may contain:

```text
manifest.json
source-config.json
effective-config.json
staged-config.json
config-patch.json
config-audit.jsonl
topology.json
topology.dot
status.json
metrics.json
edge-metrics.json
diagnostics.json
fhss-scenario.json
summary.json
captures/
```

The manifest records:

- schema version;
- GraphX revision/build information;
- source/effective config hashes;
- active config revision;
- command history hash;
- artifact hashes;
- start/end timestamps;
- truth-in-labeling statement.

## 17. Decisions Required Before Implementation

1. HTTP/WebSocket library: use Boost.Beast.
2. Frontend topology library: use Cytoscape.js.
3. Generic core placement under `libgraph`: yes.
4. JSON Pointer and JSON Patch as canonical mutation standards.
5. Rebuild-required default for all configuration edits.
6. Localhost-only MVP.
7. CLI configuration commands live directly in the FHSS demo for now.
8. Whether the server waits for manual browser shutdown after a completed run,
  or exits automatically unless `--dashboard-persist` is supplied: server
  exits automatically unless `--dashboard-persist` is supplied.

### 17.1 Finalized practical defaults

The following decisions are now explicit defaults for implementation:

- Decision 9: Session ownership model is single-session. One process owns one
  active dashboard/runtime session.

- Decision 10: Snapshot delivery practical default (industry-standard hybrid):
  - Delivery model: hybrid baseline+stream.
    - Clients load canonical baseline state via HTTP snapshot endpoints.
    - Clients then subscribe to `/api/v1/events` for incremental live updates.
    - If stream is unavailable or interrupted, clients fall back to polling
      snapshots until stream is restored.
  - Event contract:
    - Every event includes `schema`, `event_type`, monotonic `sequence`,
      `timestamp`, optional `revision`, and `payload`.
    - Clients track last-seen `sequence` and detect gaps/reordering.
  - Backpressure/drop behavior:
    - Use bounded per-client event queues with coalescing of high-frequency
      snapshot updates.
    - Default per-client queue depth: 128 events.
    - Default coalescing window for snapshot-style updates: 100 ms.
    - Slow clients must not block runtime/command threads.
    - When queue limits are exceeded, drop stale intermediate updates and emit
      an explicit resync-required signal.
  - Gap and reconnect behavior:
    - Reconnect may provide last-seen `sequence`; server resumes only if data is
      still available in retention window.
    - Default event retention window: 120 seconds.
    - Otherwise, server returns/announces resync-required and client reloads
      canonical HTTP snapshots before resubscribing.
  - Operational guardrails:
    - Server exposes counters for dropped/coalesced events and reconnects.
    - CI and soak tests must verify that slow subscribers cannot degrade command
      latency or runtime throughput.

- Decision 11: Configuration persistence policy is memory-only until explicit
  export.

- Decision 12: Mutation conflict practical default (industry-standard
  optimistic concurrency):
  - All mutation requests must include `expected_revision`.
  - If `expected_revision` != current revision, server returns `409 Conflict`
    and applies no partial changes.
  - `409` response includes current `revision`, conflict `code`, and optional
    conflicting `pointer` paths when known.
  - Client flow on conflict: refetch latest config, rebase intent, and retry
    with a new `expected_revision`.
  - Automatic server-side merge is disabled by default in v1 to preserve
    deterministic, auditable behavior.
  - Optional future merge helper may be added only for explicitly proven
    non-overlapping JSON Patch operations and must still produce a single new
    revision plus full audit record.

- Decision 13: API error schema and compatibility policy:
  - Use canonical error schema `graphx.dashboard.error.v1` for all non-2xx
    API responses.
  - Error payload fields: `schema`, `status`, `code`, `message`, `details`,
    `request_id`, optional `command_id`, optional `revision`, optional
    `pointer`, and `retriable`.
  - Compatibility policy for `/api/v1/*`: additive-only changes in v1
    (new optional fields/endpoints). No removals or semantic breaks in v1.
    Any breaking change requires `/api/v2/*`.

- Decision 14: Command/audit retention policy is fixed as follows: default
  in-memory command history is 100 entries (configurable); log files are never
  auto-deleted and must be deleted by the user; the application must indicate
  when a log file is written; no redaction is required.

- Decision 15: Web asset/toolchain practical default:
  - Build tool: Vite.
  - Embed strategy: split assets in development; embed one hashed production
    bundle in release artifacts.
  - Source maps: enabled in debug/development builds only; disabled in release
    builds.

- Decision 16: Browser/API security practical default for localhost mode:
  - CORS: disabled by default (no wildcard origins).
  - Same-origin: required for embedded page and dashboard API by default.
  - Dev-only override: allowed only behind an explicit development flag with a
    localhost-only allowlist and a prominent startup warning.

- Decision 17: Readiness semantics practical default (automation-friendly):
  - `/healthz`: returns 200 whenever the process/event loop is alive.
  - `/readyz`: returns 200 when the dashboard can accept API commands for the
    current mode; returns 503 during transient rebuild/swap windows and startup
    phases where command handling is not yet safe.
  - `--dashboard-no-run`: `/readyz` returns 200 after effective config and
    command services are initialized (execution not required).
  - Rebuild/swap: `/readyz` flips to 503 at rebuild start and back to 200 only
    after the new session/config is active (or the previous valid session is
    restored).

- Decision 18: Deterministic dashboard test baseline practical default
  (automation-friendly):
  - Golden comparison surface: compare canonical JSON API payloads only
    (`/api/v1/version`, `/api/v1/graph`, `/api/v1/config`, and selected stable
    status/snapshot fields), not rendered pixels or browser timing artifacts.
  - Canonicalization: JSON comparisons must sort object keys and ignore
    non-deterministic fields (`timestamp`, `request_id`, `command_id`, elapsed
    timings, ephemeral port values, and host-specific absolute paths).
  - Numeric tolerance defaults:
    - Integer/count fields: exact match required.
    - Boolean/string/enum fields: exact match required.
    - Floating-point fields: pass if `abs(actual - expected) <= 1e-9` for
      normalized/probability-scale values and `<= 1e-6` for signal/metric
      magnitudes unless a stricter endpoint-specific contract is declared.
  - Platform variance rules: macOS/Linux and supported compilers must produce
    identical canonical topology/config JSON; only the explicitly ignored
    non-deterministic fields above may differ.
  - Baseline workflow: maintain one checked-in golden fixture per schema version
    and compare via CI contract tests; intentional changes require an explicit
    golden update in the same PR with a short rationale.

## 18. Definition of Done

The dashboard initiative is complete when:

- every committed development step has a runnable server;
- the first shipped UI displays the real effective JSON graph;
- every node can expose a safe parameter/configuration dropdown;
- CLI and HTTP share JSON Pointer-based configuration services;
- graph edits are revisioned, validated, auditable, and transactional;
- generic status/metrics/diagnostics are reusable outside FHSS;
- one FHSS dashboard step means one scheduled message reaches a terminal state;
- the canonical FHSS graph remains singular;
- headless builds and existing tests remain supported;
- the embedded frontend requires no external network resources;
- shutdown, security limits, and artifact provenance are tested.

## 19. Recommended First Implementor Prompt

```text
Implement Step 1 only from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

The result must be a runnable embedded webserver in graphx-dsp-fhss-demo.
Add the optional GRAPHX_BUILD_WEB_DASHBOARD build flag and keep it OFF by
default. Support --dashboard, --dashboard-port 0, and --dashboard-no-run.

Serve an embedded minimal page, /healthz, /readyz, /api/v1/version,
/api/v1/graph, and /api/v1/config. Load the real effective FHSS graph without
executing it. Display graph nodes and edges. Each node must have an expandable
view showing its id, type, configured node_config, and ports/edges. Handle nodes
without node_config. Group the 64 detector nodes visually without changing the
API graph.

Also add CLI read-only configuration commands using RFC 6901 JSON Pointer:
--get-config and --write-effective-config. Do not implement parameter mutation,
metrics, FHSS stepping, or WebSockets yet.

Add ephemeral-port, health, graph API, static asset, clean-shutdown, and
headless-regression tests. Preserve the current canonical FHSS graph and all
existing working-tree changes.
```
