# GraphX Generic Graphical Dashboard Plan

**Status:** Normative staged implementation plan
**Scope:** Extend the existing generic Graph Management dashboard with a
read-only graphical topology display  
**Supersedes:** The graphical-frontend portions of
`docs/archive/2026-07-dashboard-consolidation/dsp/fhss-dashboard-v2.md` and
`docs/archive/2026-07-dashboard-consolidation/dsp/fhss_dashboard_implementation_plan.md`  
**Does not supersede:** FHSS DSP, synthetic-IQ, truth-isolation, or receiver
validation requirements

## 1. Decision

GraphX will have one graph-management implementation and one dashboard:

- `graph::GraphCoordinator` remains the in-memory graph inspection and
  parameter-editing boundary.
- `graph::GraphHttpServer` remains the HTTP adapter.
- Phase 0 gives `graph::GraphHttpServer` the stable metrics capability handle;
  the designated graphical-dashboard metrics phase makes it a direct
  `app::metrics::IMetricsSubscriber`.
- Executor commands and metric subscription capabilities are available before
  `GraphExecutor::Init()`.
- `GraphExecutor` is always present in `graphx-dashboard`; initialization and
  execution remain operator-controlled.
- `GraphExecutorBuilder` creates a configured executor shell without loading
  plugins, instantiating nodes, or building a `GraphManager`.
- `GraphCoordinator` supplies immutable, revisioned configuration snapshots
  but does not construct or execute graphs.
- The interactive command surface lives in the existing dashboard page.
- `graph-cli` and `graph::GraphCli` are deprecated migration surfaces and are
  removed only after web and REST automation parity is verified.
- `graphx-dashboard` remains the generic launcher.
- `libgraph/resources/web/index.html` remains the single dashboard entry point.
- `GET /api/v1/graph` remains the authoritative topology input.
- Existing `/api/v1/nodes/*` and `/api/v1/execution/*` routes remain the only
  management and lifecycle routes.

The graphical display is a presentation feature inside that dashboard. It is
not a second management stack, FHSS application, API namespace, configuration
authority, runtime owner, or executable.

The initial graphical implementation may use React Flow, ELK.js, and
TypeScript as recommended by the earlier FHSS analysis, but those technologies
must be integrated into the generic `libgraph` web resource and build. They
must not depend on or extend the legacy `examples/DSP/dashboard` product
structure.

## 2. Findings From The Original FHSS Plan

The original plan contained several strong display ideas:

- explicit source and target port handles;
- stable node and edge identities;
- layered directed-graph layout;
- pan, zoom, selection, minimap, and fit-to-view;
- compound/hierarchical groups for repeated graph stages;
- collapsed bundle edges that preserve their authoritative member edges;
- bounded rather than per-message animation;
- a semantic table/tree alongside the canvas;
- reduced-motion and keyboard support; and
- raw JSON as a diagnostic view rather than the primary display.

Those ideas should be retained generically.

The original plan also assumed or implemented a parallel FHSS management
architecture:

- `EmbeddedDashboardServer` rather than `GraphHttpServer`;
- application-owned `/api/v1/fhss/*` graph, configuration, runtime, job,
  observation, spectrum, investigation, and event resources;
- `GraphRuntimeSession` and an FHSS runtime owner separate from the generic
  optional `GraphExecutor`;
- an FHSS configuration service and policy beside `GraphCoordinator`;
- a frontend and installed asset tree under `examples/DSP/dashboard`; and
- FHSS-specific grouping, heatmap, job, truth, and receiver semantics in the
  dashboard product.

Those elements are rejected for this plan. They mixed a useful topology
renderer with a second management product. FHSS remains an important example
graph and acceptance fixture, but it receives no privileged dashboard
architecture.

## 3. Architectural Boundary

```mermaid
flowchart LR
  Browser["Operator browser"]
  Launcher["graphx-dashboard"]
  Server["GraphHttpServer"]
  Coordinator["GraphCoordinator"]
  Document["Authoritative graph JSON"]
  Executor["Configured GraphExecutor"]
  Commands["Pre-Init CommandCapability"]
  Metrics["Pre-Init MetricsCapability"]
  Entry["Existing index.html"]
  Adapter["Generic display adapter"]
  Layout["Generic layout engine"]
  Canvas["Read-only topology canvas"]
  Inspector["Existing node inspector/editor"]
  Semantic["Semantic table/tree"]

  Browser --> Launcher
  Launcher --> Server
  Server --> Coordinator
  Coordinator --> Document
  Server -. "typed lifecycle command" .-> Commands
  Commands -. "serialized control" .-> Executor
  Executor --> Metrics
  Metrics -. "IMetricsSubscriber events" .-> Server
  Server --> Entry
  Entry --> Adapter
  Adapter --> Layout
  Layout --> Canvas
  Adapter --> Semantic
  Canvas --> Inspector
  Semantic --> Inspector
```

The browser obtains the complete graph from the existing
`GET /api/v1/graph`. A generic client-side adapter validates and converts that
document into display nodes, ports, edges, and optional groups. Layout and
collapse are presentation operations only.

Node configuration edits continue to use
`PATCH /api/v1/nodes/{id}`. Execution controls continue to use the existing
generic execution routes. Internally, those routes use the same typed command
capability as other operator adapters; `GraphHttpServer` does not invoke
executor lifecycle methods directly. The canvas never talks directly to
`GraphManager`, node objects, queues, DSP services, or an FHSS application.

`GraphExecutorBuilder` installs `CommandCapability` and `MetricsCapability`
before returning the executor. It creates an executor shell in `CONFIGURED`
state from a `GraphConfigurationSnapshot`; it does not load plugins,
instantiate nodes, or construct a `GraphManager`. `CommandPolicy` and
`MetricsPolicy` bind to those existing services during executor initialization
rather than creating them there. This removes the circular requirement that
the command path must already have executed `Init` before it can accept
`configure` or `init`.

`GraphCoordinator` owns its graph document so no external alias can bypass its
lock or revision accounting. `Snapshot()` returns an immutable document,
monotonically increasing revision, and deterministic content identity. A
successful content-changing `UpdateNodeConfig` increments the revision; a
failed or no-op update does not. The coordinator remains a configuration
authority only: it does not load plugins, own executor threads, or instantiate
graph nodes.

The executor lifecycle is:

```text
ConfigureGraph → Init → Start → Run → Stop → Join
```

`ConfigureGraph` validates and records a snapshot atomically but performs no
plugin, node, graph-manager, thread, or node-lifecycle work. `Init` loads
providers, constructs the `GraphManager`, discovers node capabilities, and
initializes nodes. Reconfiguration is allowed only before initialization or
after stop and join have completed. `Join` leaves the executor stopped.
Coordinator edits never mutate an instantiated graph; differing coordinator
and active revisions are reported as `configuration_dirty`.

The command transition contract is fixed:

| Command | Allowed state | Completion and next state |
|---|---|---|
| `configure` | `CONFIGURED`, or `STOPPED` after joined teardown | Synchronous `200`; atomically records the latest coordinator snapshot, advances generation, clears prior generation telemetry/operations, remains/enters `CONFIGURED` |
| `init` | `CONFIGURED` with no newer coordinator revision | Synchronous `200`; builds and initializes the exact configured snapshot, records `active_revision`, enters `INITIALIZED` |
| `start` | `INITIALIZED` and not dirty | Synchronous `200`; enters `RUNNING` |
| `run` | `RUNNING` and not dirty, with no active run worker | Asynchronous `202`; the joinable worker runs the graph and then performs the sole `Stop` and `Join` teardown, ending in `STOPPED` |
| `stop` | `INITIALIZED` or `RUNNING` | Calls `RequestStop()` immediately and returns `202`; the existing run worker, or a newly created teardown worker when no run worker exists, performs the sole `Stop` and `Join` sequence |
| `join` | `STOPPING` | Returns `202` for an operation that completes with the active teardown worker; in already joined `STOPPED` it is an idempotent synchronous `200` |

All other command/state combinations return `409`. A natural run completion
and a requested stop use the same worker teardown path. A stop operation
completes when teardown finishes; the associated run operation completes as
`completed` after natural completion or `cancelled` after a stop request.
`join` never starts a second teardown. Configure and init failures leave the
previous configured snapshot usable when rollback succeeds; failed cleanup
enters terminal `ERROR`. In `ERROR`, state inspection and process shutdown are
the only permitted operations; recovery requires constructing a new executor
or restarting the dashboard.

A coordinator edit after configure makes the runtime dirty. `init`, `start`,
and `run` reject dirty state. An already-running generation may finish or be
stopped against its recorded active revision, but it never consumes the newer
document.

There is one owning configuration path:

```cpp
auto coordinator =
    std::make_shared<graph::GraphCoordinator>(std::move(graph_document));
auto executor = graph::GraphExecutorBuilder()
                    .WithGraphSnapshot(coordinator->Snapshot())
                    .WithPluginDirectory(plugin_directory)
                    .Build();
graph::GraphHttpServer server(coordinator,
                              executor->GetCapability<CommandCapability>(),
                              executor->GetCapability<MetricsCapability>(),
                              port, index_path);
```

`WithGraphSnapshot` is the dashboard builder input. `WithJsonConfig` remains a
non-dashboard compatibility convenience that loads exactly one initial
snapshot. `GraphHttpServer` receives shared ownership of the one coordinator
and shared capability handles; it receives no raw executor pointer and creates
no second graph document. Its `configure` handler takes exactly one atomic
snapshot from that coordinator and submits it as a typed command.

The command capability accepts typed operations, not UI command lines. It
serializes state transitions, assigns operation identities, exposes
accepted/completed/failed results, and owns a joinable execution worker for
blocking `Run`. A concurrent stop request uses
`GraphExecutor::RequestStop()`; the execution worker performs the sole graph
teardown sequence. It keeps a weak executor binding so registration does not
create an executor → capability → executor ownership cycle.

`CommandProcessorCapability` remains an optional parser for terminal command
text, and `DashboardCapability` remains an optional UI queue. Both adapt to
the typed `CommandCapability`; neither owns lifecycle state. HTTP uses typed
requests directly, while CLI parsing remains an adapter responsibility.

The page provides a generic command palette/console backed by structured HTTP
requests. It discovers supported operations and argument metadata, renders
forms and completion state, and retains only bounded presentation history.
It is not a shell and cannot execute host commands. Existing execution buttons
and the console are two views of the same typed operations.

The deprecated CLI is not a permanent second adapter. During migration it
emits a warning and receives compatibility fixes only. No new examples,
features, or integrations may depend on it. Loading and plugin selection move
to launcher options; inspection and editing use existing HTTP resources;
server-side `save` becomes an explicit browser export/download; and headless
automation uses documented REST requests.

In the designated metrics phase, `GraphHttpServer` registers itself as an
`IMetricsSubscriber` before execution starts. Phase 0 establishes only the
stable capability handle and ownership boundary. The later callback performs
only a bounded copy into server-owned snapshot state; HTTP response generation
reads that snapshot outside the callback, and the server unregisters before
its storage or the executor can be destroyed.

## 4. Non-Negotiable Guardrails

### 4.1 One management implementation

The implementation must not add:

- another coordinator, HTTP server, runtime session, runtime owner, or graph
  configuration service;
- another dashboard executable or root page;
- `/api/v1/fhss`, `/api/v2`, or another route namespace for topology display;
- an `examples/DSP/dashboard` frontend or install tree;
- a frontend-selected management backend;
- a compatibility switch between generic and FHSS dashboards; or
- a second source of graph state.

Legacy files under `libgraph/src/dashboard`, `libgraph/include/graph/dashboard`,
or `examples/DSP/dashboard` are not dependencies for this work. If retained
for unrelated historical builds, tests must prove the generic dashboard does
not link, serve, or call them.

### 4.2 Authoritative topology

The graph document owns:

- node IDs and types;
- node configuration;
- edge endpoints;
- numeric or named ports; and
- any explicitly authored generic presentation hierarchy.

The renderer must not repair missing edges, connect unused ports, create
receiver nodes, infer execution semantics, or alter the document when arranging
the display.

### 4.3 Read-only structure

The graphical canvas permits selection, navigation, local layout, and
group expansion. It does not permit:

- adding or deleting nodes;
- connecting, reconnecting, or deleting edges;
- changing node IDs or types;
- persisting dragged positions into graph configuration implicitly; or
- suggesting that a visual bundle is an executable edge.

Existing `node_config` editing remains available through the existing editor.
Topology remains structurally read-only.

### 4.4 Generic behavior

No display rule may depend on an FHSS class name, frequency index, detector
count, message schedule, IQ metadata, or DSP-specific configuration field.
The same adapter and renderer must display minimal test graphs, split/merge
graphs, SAR graphs, and FHSS graphs.

## 5. Generic Display Contract

### 5.1 Input

The first implementation consumes the existing graph response unchanged:

```text
GET /api/v1/graph
  -> data.nodes[]
  -> data.edges[]
```

No new topology endpoint is required. Failure responses use the current
generic HTTP response contract.

The adapter must accept both supported port forms:

- `source_port` / `target_port`; and
- `source_port_name` / `target_port_name`.

It must reject or visibly report malformed endpoints instead of silently
dropping them.

### 5.2 Presentation model

The browser owns a domain-neutral model:

```typescript
type PortKey =
  | { kind: "index"; value: number }
  | { kind: "name"; value: string };

interface DisplayNode {
  id: string;
  graphNodeId: string;
  type: string;
  label: string;
  inputPorts: DisplayPort[];
  outputPorts: DisplayPort[];
  groupId?: string;
}

interface DisplayPort {
  id: string;
  key: PortKey;
  direction: "input" | "output";
}

interface DisplayEdge {
  id: string;
  sourceNodeId: string;
  sourcePort: PortKey;
  targetNodeId: string;
  targetPort: PortKey;
}

interface DisplayGroup {
  id: string;
  label: string;
  members: string[];
  layout: "layered" | "grid" | "fanout" | "fanin";
  collapsedByDefault: boolean;
}
```

This model is internal presentation state. It does not replace
`GraphConfig`, `GraphCoordinator`, or the REST document.

### 5.3 Identity

Node identity is the graph's stable string `id`.

Before implementation, Phase 0 must settle a generic edge-identity rule:

1. Prefer an explicitly authored generic edge ID if GraphX adopts one.
2. Otherwise use a canonical encoding of source node ID, exact source port,
   target node ID, and exact target port.
3. Do not use array position as persistent identity.
4. If GraphX permits duplicate edges with identical endpoints and ports, add a
   generic authoritative identity field rather than inventing a browser-only
   ordinal.

Port handle IDs must encode both direction and exact numeric-or-named port
identity. Numeric port `0` and named port `"0"` must not collide.

### 5.4 Generic hierarchy

Hierarchy is optional presentation metadata, not domain inference. The
preferred future graph-document shape is a top-level, execution-neutral
section such as:

```json
{
  "presentation": {
    "groups": [
      {
        "id": "acquisition-bank",
        "label": "Acquisition bank",
        "members": ["detector_00", "detector_01"],
        "layout": "grid",
        "collapsed_by_default": true
      }
    ]
  }
}
```

The exact schema is decided in Phase 2. It must be generic, optional, validated,
and ignored by execution. Invalid metadata must not alter graph construction.

The renderer may offer an unpersisted “group similar siblings” convenience
view based only on generic topology equivalence, but explicit metadata wins.
It must not contain FHSS type-name or prefix rules.

### 5.5 Collapsed groups and bundle edges

A bundle edge is presentation-only and contains the IDs of every authoritative
member edge it summarizes. It has no invented source or target port and never
appears in GraphX JSON.

Expanding a group restores every original node, edge, and exact port mapping.
Selection and inspection always resolve to authoritative node or edge
identities.

## 6. Frontend And Packaging Policy

The dashboard retains one entry point:

```text
libgraph/resources/web/index.html
```

If compiled frontend assets are introduced, they live under the same generic
resource root and are served by the same `GraphHttpServer`, for example:

```text
libgraph/resources/web/
  index.html
  assets/
    graphx-dashboard-<hash>.js
    graphx-dashboard-<hash>.css
```

Editable TypeScript sources and their lockfile may live under a generic
`libgraph/web/` source directory. They must not live under an FHSS example.

`GraphHttpServer` may be extended to serve bounded, contained static assets
from this one resource root. That is an extension of the existing adapter, not
a new server.

Recommended toolchain policy:

- React Flow and ELK.js are acceptable for rich nodes, ports, compound graphs,
  and layered layout.
- Dependencies are pinned and self-hosted; no CDN is used.
- Node.js is a maintainer/build-time tool, not a deployed runtime.
- The ordinary C++ user build must remain possible without downloading
  frontend dependencies.
- If compiled assets are checked in, CI verifies that rebuilding from the
  lockfile produces the checked-in inventory.
- Source-tree and installed-tree dashboards serve the same asset inventory.
- A dashboard-disabled or minimal library build must not acquire a Node.js
  runtime dependency.

Phase 0 may choose a no-framework SVG implementation only if it demonstrates
that exact-port routing, compound layout, accessibility, and the expected graph
sizes remain maintainable. It must not create an interim UI that is discarded
in the next phase.

## 7. Phased Delivery

Each phase uses one orchestrator, one implementer, and one independent verifier.
Stop after each phase for authorization.

### Phase 0 — Runtime authority, architecture lock, and baseline

**Goal:** Establish one configured runtime authority before graphical work and
prevent recurrence of the parallel FHSS architecture.

Implementation:

- Record this plan as the graphical-dashboard authority.
- Inventory the current `GraphCoordinator`, `GraphHttpServer`,
  `graphx-dashboard`, `index.html`, install rules, and dashboard tests.
- Inventory legacy dashboard sources and prove whether they are excluded from
  the default generic build.
- Decide React Flow/ELK versus a durable no-framework renderer using a small
  spike against both `minimal_graph.json` and the 75-node/137-edge FHSS graph.
- Define canonical generic edge and port identities.
- Lock the pre-`Init` command and metrics capability contracts, including
  ownership, registration, unregistration, state transitions, asynchronous
  run completion, and stop behavior.
- Add `GraphConfigurationSnapshot` with immutable graph JSON, monotonic
  revision, and deterministic content identity.
- Make `GraphCoordinator` own its document, produce atomic snapshots, and
  increment its revision only after successful content-changing mutation.
  Failed and no-op mutations do not advance the revision.
- Add `GraphExecutor::ConfigureGraph(snapshot)` and a distinct `CONFIGURED`
  state before `INITIALIZED`. Define valid transitions, failure atomicity,
  reconfiguration rules, and graph-generation reset behavior.
- Refactor `GraphExecutorBuilder` to return a configured executor shell without
  loading plugins, instantiating nodes, or constructing `GraphManager`.
  Preserve non-dashboard `Build()` plus `Execute()` behavior by configuring
  the initial snapshot before returning the shell.
- Move provider loading, graph construction, capability discovery, and node
  initialization into the executor `Init` sequence in a documented order.
- Register typed `CommandCapability` and `MetricsCapability` before the
  executor is returned. Policies consume those exact instances rather than
  replacing them during `Init`.
- Define typed command request/result/discovery contracts, operation identity,
  HTTP status mapping, bounded result retention, and asynchronous completion
  lookup under the existing `/api/v1/execution/*` namespace.
- Route `GraphHttpServer` lifecycle requests through `CommandCapability` and
  remove every direct call from the server to executor lifecycle methods.
- Run blocking execution on a joinable command worker. Do not hold transition
  serialization across `Run`; `stop` uses `RequestStop()` as an independent
  fast path and teardown occurs exactly once on the execution thread.
- Make `graphx-dashboard` always construct the executor and remove
  `--enable-execution`. Loading the page performs no initialization or
  execution; a later `init` failure does not prevent topology inspection.
- Add `configure` to the typed command and existing `/api/v1/execution/*`
  lifecycle surface. The server supplies the latest coordinator snapshot.
- Track coordinator revision, configured revision, active revision, graph
  generation, and `configuration_dirty`; never mutate an initialized graph
  implicitly. Successful configure advances generation and atomically clears
  stale metric schemas, metric values, and operation results. `start` and
  `run` reject dirty configuration until a permitted reconfigure succeeds.
- Define these generic command resources:
  `GET /api/v1/execution/commands`,
  `POST /api/v1/execution/commands/{name}`, and
  `GET /api/v1/execution/operations/{operation_id}`. Typed JSON arguments
  replace terminal strings. Synchronous success returns `200`; accepted
  asynchronous work returns `202` plus `Location`; invalid transitions return
  `409`; unknown commands or operation IDs return `404`.
- Record the `graph-cli` deprecation and a command-by-command migration matrix.
- Remove the current direct `GraphHttpServer` → `GraphExecutor` lifecycle path;
  do not create a second runtime session to hide it.
- Define frontend source, generated asset, lockfile, and install locations.
- Capture current node-table/editor and execution-control behavior as
  regression requirements.
- Add architecture scans rejecting new FHSS routes, dashboard executables,
  coordinators, servers, runtime owners, and `examples/DSP/dashboard`
  dependencies in the generic target.

Acceptance:

- One architecture diagram maps every planned UI action to the existing
  management component and route.
- The architecture diagram shows HTTP and CLI using one typed command
  capability during migration, the web console as the supported interactive
  surface, and `GraphHttpServer` subscribing to `MetricsCapability`.
- `graphx-dashboard` has one configured executor without requiring
  `--enable-execution`, while loading the page performs no node initialization
  or execution.
- `ConfigureGraph` performs no plugin loading, node construction, thread
  creation, or node lifecycle calls and rolls back atomically on failure.
- `Init` consumes exactly the configured immutable snapshot and reports its
  active revision and graph generation.
- Editing configuration after `Init` reports `configuration_dirty`; execution
  cannot silently consume the newer coordinator document.
- Reconfiguration is rejected until stop and join complete.
- Command and metrics capability object identity is preserved before, during,
  and after `Init`.
- Command discovery, submission, asynchronous completion, and state responses
  have documented schemas, bounded storage, and include command/operation ID,
  status, executor state, configured and active revisions, graph generation,
  and diagnostic message.
- No `GraphHttpServer` code directly calls executor lifecycle methods.
- A blocking run does not block HTTP handling or prevent cooperative stop, and
  each execution attempt enters graph teardown exactly once.
- Existing non-dashboard executor call sites and full regression tests pass
  after migration to the configured lifecycle.
- The chosen renderer displays exact numeric and named ports in the spike.
- The decision includes build/install behavior without a deployed Node runtime.
- Focused generic dashboard tests and `git diff --check` pass.

Operator example:

```bash
graphx-dashboard \
  --graph libgraph/test/config/topologies/minimal_graph.json \
  --port 8080
```

The Phase 0 report records the existing table behavior and the renderer spike;
the spike is not installed as a second dashboard.

### Phase 1 — Generic read-only topology canvas

**Goal:** Add the first graphical representation to the existing dashboard.

Implementation:

- Replace the node-only initial load with one call to `GET /api/v1/graph`.
- Keep the existing management header, execution controls, search, node table,
  and node-config editor.
- Add a “Topology” view within the same `index.html`.
- Render every node and authoritative edge with exact port handles.
- Add deterministic layered layout, pan, zoom, fit-to-view, selection, and
  reset-layout.
- Add a node/edge inspector sourced from the same in-memory response.
- Keep topology interactions read-only.
- Add empty, malformed, disconnected, cyclic, numeric-port, named-port,
  split, merge, and fanout fixtures.
- Generalize the existing static-resource install/smoke tests if the asset set
  becomes multi-file.

Acceptance:

- The minimal graph renders two nodes, one edge, and all four endpoint fields.
- Generic split and merge fixtures show every port and edge.
- The full FHSS graph renders its authoritative cardinalities without
  FHSS-specific frontend code.
- Selecting a canvas node and selecting the table row opens the same inspector.
- Node-config edits still use the existing PATCH route and refresh both views.
- No structural mutation gesture is available.
- Source-tree and installed-tree dashboard smoke tests pass.

Operator example:

Open the minimal, complex-network, SAR, and FHSS graph files with the same
`graphx-dashboard --graph ...` command and record node/edge counts plus a
screenshot for each.

### Phase 2 — Generic hierarchy and large-graph navigation

**Goal:** Make repeated and high-fanout graphs understandable without encoding
FHSS semantics in management code.

Implementation:

- Define and validate optional generic `presentation.groups` metadata.
- Add compound group rendering, collapse/expand, breadcrumbs, isolate
  selection, and an overview/minimap.
- Add grid, fanout, fanin, and layered group layout modes.
- Implement presentation-only bundle edges with complete authoritative
  membership.
- Preserve selection when a group is collapsed or expanded.
- Provide a generic ungrouped/raw-topology mode.
- Add bounds for group count, members, layout work, and visible detail.
- Author FHSS detector-bank grouping in the FHSS graph's optional generic
  presentation metadata, not in dashboard code.

Acceptance:

- Invalid, overlapping, cyclic, or unknown-member groups fail visibly and do
  not change execution.
- Collapsing a group does not lose any authoritative edge identity.
- Expanding the FHSS detector group reveals all 64 nodes and all exact
  channelizer/detector/merge port mappings.
- The same group mechanism works for a non-FHSS split/merge fixture.
- Ungrouped mode exactly matches the graph document.
- No `FHSS`, detector count, or frequency rule exists in `libgraph` frontend
  or server code.

Operator example:

Use the same dashboard to collapse and expand one generic split/merge group and
the FHSS detector-bank group, then compare the inspector's authoritative member
counts with the source JSON.

### Phase 3 — Semantic alternative and operator usability

**Goal:** Make the topology understandable and operable without relying on the
canvas.

Implementation:

- Convert the existing node table into a semantic topology tree/table that
  includes endpoints, exact ports, group membership, and structural warnings.
- Synchronize selection among canvas, semantic view, search results, and
  inspector.
- Add keyboard navigation, visible focus, screen-reader labels, non-color
  status, reduced-motion preference, and stable focus during refresh.
- Save layout, zoom, and collapse preferences only as local presentation state,
  clearly separated from graph configuration.
- Support narrow viewport reflow without hiding graph information.

Acceptance:

- Every authoritative node and edge is available in the semantic view.
- All inspection and group operations are keyboard accessible.
- Canvas motion and color are never the only representation of state.
- Local layout changes never issue PATCH requests or modify graph JSON.
- Focus and selection remain stable after node-config refresh.
- Focused automated accessibility checks and a documented human keyboard/reflow
  pass complete successfully.

### Phase 4 — Generic execution and metrics overlays

**Goal:** Overlay truthful generic runtime information without creating another
runtime-management path.

Prerequisite:

The existing generic Graph Management layer must expose a stable mapping from
graph node/edge identity to existing runtime metrics. If it cannot, this phase
stops for a generic identity-contract decision.

Implementation:

- Continue using the existing execution-state endpoint and configured
  `GraphExecutor` established in Phase 0.
- Make `GraphHttpServer` implement `IMetricsSubscriber`; register it with the
  mandatory executor capability and unregister it deterministically before
  teardown.
- Route CLI lifecycle operations through the same capability. Terminal string
  parsing may adapt to typed requests but is not the capability contract.
- Add the command palette/console to the existing `index.html`; use structured
  operations and argument forms rather than host-shell or terminal semantics.
- Add bounded command history, asynchronous operation status, keyboard access,
  and non-color success/failure output.
- Add explicit browser graph export/download from the authoritative
  coordinator snapshot. It performs no implicit server-side filesystem write.
- Add only the smallest additive generic metric snapshot route needed, under
  `/api/v1`, on `GraphHttpServer`.
- Populate the route from the server's bounded subscriber snapshot and the
  schemas supplied by `MetricsCapability`.
- Source metrics from existing GraphX metric facilities; do not depend on the
  legacy embedded dashboard publisher, `GraphRuntimeSession`, or FHSS event
  schemas.
- Define units, monotonic/reset behavior, sample interval, availability, and
  graph-generation identity before displaying a value.
- Overlay node state, queue depth, backpressure, and aggregated edge activity
  only where the source contract supports them.
- Bound update frequency, retained history, animated edges, and layout work.
- Never rerun layout for metric-only updates.
- Provide pause, reduced-motion, and text equivalents.

Acceptance:

- `CommandCapability` accepts `init` before executor initialization.
- HTTP and CLI lifecycle requests produce equivalent typed command results.
- Buttons and the web command console produce equivalent typed command
  requests and results.
- The console cannot invoke arbitrary host commands or bypass supported
  command discovery.
- Browser export preserves the authoritative graph document and revision and
  is verified in source-tree and installed-tree browser tests.
- No `GraphHttpServer` code directly calls `GraphExecutor` lifecycle methods.
- A blocking run does not block HTTP handling or prevent a cooperative stop.
- Concurrent or duplicate lifecycle commands are serialized and rejected or
  completed according to one documented executor state machine.
- `GraphHttpServer` is registered as an `IMetricsSubscriber` exactly while its
  event storage is alive.
- Metric callbacks perform no socket I/O, JSON serialization, layout work, or
  calls back into `MetricsCapability`.
- Subscriber removal, executor shutdown, and an in-flight metric publication
  are race-safe.
- Missing or uncorrelated metrics display as unavailable, never as zero.
- Metrics cannot affect GraphX scheduling, queueing, or execution.
- No per-message browser event stream is introduced.
- Collapsed groups aggregate only enumerated authoritative member metrics.
- Stale generations and counter resets cannot produce false rates.
- Inspection without initialization or execution remains fully functional with
  the executor in `CONFIGURED` state.

This phase does not add FHSS observations, expected truth, spectrum, jobs, or
investigation controls. Domain applications may provide those outside the
generic topology management surface if later authorized.

### Phase 5 — Packaging, performance, and release validation

**Goal:** Qualify the single generic graphical dashboard as a supported GraphX
surface.

Implementation:

- Verify clean host builds on macOS and Linux.
- Verify the ordinary non-Docker workflow first.
- If a generic Docker operator image is authorized, package
  `graphx-dashboard`; do not reuse the legacy FHSS operator container.
- Validate source-tree and clean installed-tree asset inventories.
- Measure initial render, layout, interaction, and bounded metric-update costs
  on representative small, medium, FHSS, and SAR graphs.
- Document supported graph-size budgets and graceful degradation.
- Add a comprehensive generic dashboard operator manual with an FHSS example
  subsection.
- Document REST automation, graph export, and every `graph-cli` migration.
- After all deprecation gates pass, remove the `graph-cli` target,
  `graph::GraphCli`, its install rule, and CLI-specific tests in one change.
- Run proportional browser, accessibility, regression, and shutdown tests.

Acceptance:

- One executable, root page, API namespace, and asset inventory are installed.
- No generic dashboard command loads legacy embedded-dashboard files.
- Small and representative large graphs stay within documented budgets.
- The operator can build from a fresh clone, launch the installed dashboard,
  inspect minimal/SAR/FHSS graphs, and reproduce the report.
- Native GraphX build and dashboard operation remain available without Docker.
- No supported example, test workflow, or operator procedure requires
  `graph-cli`; the web page and REST examples cover its supported use cases.
- `git diff --check` and the affected/full regression suites pass.

## 8. Agent Workflow

### Orchestrator

For each authorized phase:

1. Inspect current repository state and preserve unrelated changes.
2. Convert the phase into a file-level acceptance checklist.
3. Identify every existing Graph Management component and route being reused.
4. Assign implementation to one implementer.
5. Assign completed work to an independent verifier.
6. Route every verifier finding back to the implementer.
7. Repeat until no blocking/high findings remain and every criterion passes.
8. Stop at the phase boundary without committing, pushing, or opening a PR
   unless explicitly requested.

### Implementer

The implementer must:

- audit existing behavior before editing;
- extend the existing generic components in place;
- avoid legacy embedded-dashboard dependencies;
- implement only the authorized phase;
- preserve node editing and operator-controlled execution behavior;
- add deterministic frontend, C++, install, and operator tests appropriate to
  the phase; and
- report changed files, commands, results, and limitations.

### Verifier

The verifier must not edit implementation files. It independently verifies:

- reuse of `GraphCoordinator`, `GraphHttpServer`, `graphx-dashboard`, and the
  existing `/api/v1` routes;
- absence of parallel management components and FHSS-specific generic code;
- exact node, edge, and port rendering;
- structural read-only behavior;
- source-tree and installed-tree parity;
- small, generic complex, SAR, and FHSS examples;
- focused and full test results; and
- explicit PASS/FAIL for each phase criterion.

## 9. Test Strategy

Tests should be layered without duplicating management logic:

1. **Adapter unit tests:** JSON-to-display conversion, port forms, identity,
   malformed inputs, groups, and bundles.
2. **Renderer component tests:** node/edge counts, handles, selection,
   collapse/expand, and read-only interaction.
3. **Generic C++ HTTP tests:** existing `/api/v1/graph`, static assets, MIME
   types, and containment.
4. **Browser smoke tests:** real `graphx-dashboard`, graph fetch, render,
   inspector, edit synchronization, keyboard flow, and installed assets.
5. **Architecture tests:** forbidden parallel routes/classes/dependencies and
   exactly one dashboard entry point.
6. **Operator examples:** fresh-clone native build, minimal graph, generic
   complex graph, SAR graph, and FHSS graph.

The production renderer must not be its own only oracle. Expected node IDs,
edge endpoint tuples, port identities, and group membership come from small,
independently authored fixtures and direct source-JSON checks.

## 10. Completion Criteria

The plan is complete only when:

- the existing generic Graph Management dashboard presents an interactive
  node-and-edge topology;
- nodes, edges, exact ports, and optional hierarchy remain traceable to the
  graph document;
- topology is structurally read-only;
- FHSS and SAR are examples, not management-layer special cases;
- no parallel dashboard server, coordinator, runtime owner, configuration
  service, API namespace, executable, or asset tree is introduced;
- the existing table/editor and execution controls continue to work;
- source and installed dashboards use the same generic assets;
- native non-Docker operation remains supported; and
- affected and full regression tests plus `git diff --check` pass.
