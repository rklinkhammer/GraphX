# GraphX FHSS Dashboard V2 Recommendation

## Status

This document records the repository-aware evaluation of the proposed richer
GraphX visualization described in the shared analysis:

<https://chatgpt.com/share/6a60175d-0f80-83ea-b007-1adc8897faf3>

The recommendation is to adopt React Flow, ELK.js, and TypeScript for a new
dashboard frontend while retaining the existing GraphX C++ dashboard server,
versioned APIs, runtime ownership, event transport, and FHSS evidence
boundaries.

The existing `examples/DSP/dashboard/index.html` is a useful prototype and
behavioral reference. It is not the intended long-term rendering architecture
for the modernized dashboard.

### Naming and single-implementation rule

`V2` is the name of this modernization initiative and document only. It is not
an API version, a second product, a compatibility lane, or a separately served
dashboard. GraphX will have exactly one FHSS dashboard implementation in any
source, build, install, and runtime configuration.

The modernization must therefore:

- retain the existing `/api/v1/fhss` namespace and authoritative OpenAPI
  document;
- serve one dashboard at `/` from one installed asset inventory;
- replace the prototype frontend in place rather than shipping `legacy` and
  `v2` entrypoints;
- avoid a UI selector, compatibility adapter, duplicate route tree, or
  `/api/v2` namespace; and
- use source control and previously qualified release artifacts for rollback,
  not a second implementation compiled into or served by the executable.

### Current repository baseline

The repository state has moved beyond an early dashboard prototype. Current
documentation describes a mature FHSS dashboard stack with implemented
configuration authority, runtime ownership, bounded event transport,
observation/evaluation surfaces, message-job control, investigation workflows,
and qualification/security policy.

This recommendation therefore defines a compatibility-first frontend
modernization track, not a restart of dashboard architecture.

## Executive decision

Adopt this stack:

```text
React Flow + ELK.js + TypeScript
Existing C++ HTTP/WebSocket server
Existing OpenAPI, snapshot, lifecycle, and event contracts
FHSS-specific presentation adapter
Collapsed detector bank + heatmap + optional 8x8 expansion
Bounded aggregate activity animation
Read-only topology by default
```

Do not introduce:

- a second WebSocket protocol;
- frontend-only runtime/configuration identity inference;
- literal animation of every GraphX message transfer;
- unqualified latency or throughput displays;
- CDN dependencies;
- prefix-only detector grouping; or
- premature extraction of a generic dashboard framework.

The work should be treated as an FHSS frontend modernization and targeted
GraphX telemetry extension, not as a new dashboard backend.

### Compatibility-first policy

The modernization must preserve current authoritative contracts and behavior by
default:

- keep the existing C++ runtime owner, lifecycle boundaries, and loopback-only
  deployment model;
- keep the `/api/v1/fhss` namespace, ordered event-envelope model, and
  polling/streaming resynchronization behavior unless a versioned contract
  change is explicitly approved;
- preserve evidence and qualification boundaries (synthetic-only, no HWIL/OTA,
  no production-RF claims);
- replace the UI in place behind explicit characterization and parity gates.

Any API or semantic behavior change beyond additive optional fields must follow
the established compatibility/versioning policy. Required additive fields or
event types extend the existing contract; they do not create a new API.

### Architecture decision alignment

Historical planning material used Cytoscape.js as an initial topology-library
default. This recommendation intentionally updates that frontend choice to
React Flow plus ELK.js for richer typed node content, explicit port handles,
and grouped FHSS operator workflows.

That change must be recorded as a Phase 0 architecture decision so planning,
qualification, and operational documentation remain synchronized.

## Relationship to the existing dashboard

### Components retained

The modernized dashboard retains the existing:

- `EmbeddedDashboardServer` and loopback-only security model;
- OpenAPI 3.1 and JSON Schema contracts;
- graph and configuration resources;
- strong-ETag RFC 6902 configuration updates;
- transactional runtime rebuild/start/stop ownership;
- graph, node, edge, metric, and diagnostic snapshots;
- bounded WebSocket publisher, polling fallback, sequence numbers, replay,
  gap detection, and coherent resynchronization;
- FHSS message-job controller;
- synthetic IQ generation and truth-free receiver execution;
- expected-truth, receiver-observation, and evaluator-comparison separation;
- investigation export, validation, and replay services; and
- current security, accessibility, installed-tree, and qualification gates.

### Component replaced

The current page renders topology as expandable node JSON and raw edge JSON.
The modernization replaces that frontend implementation in place with one
compiled TypeScript/React application.

The deployed application would still be served by the same executable and at
the same loopback URL. A typical build product would be:

```text
examples/DSP/dashboard/dist/
  index.html
  assets/dashboard-<hash>.js
  assets/dashboard-<hash>.css
```

CMake would install those files under:

```text
share/graphx/fhss-dashboard/
```

No Node.js runtime would be required on the deployed target. Node and the
frontend toolchain would be build-time dependencies only.

### In-place migration policy

There is one implementation throughout the modernization:

1. Capture the prototype's authoritative contracts and observable behavior as
   a hash-bound baseline before changing its source.
2. Replace the frontend in place. The executable continues to serve the one
   current implementation at `/`.
3. Preserve configuration, lifecycle, FHSS jobs, observations,
   investigations, reconnect behavior, security, and accessibility during
   every phase; an intermediate phase may not remove an inherited capability.
4. Package only the current compiled frontend. Do not install or serve the old
   page as a fallback.
5. Use a source-control reversion or a previously qualified release artifact
   if rollback is required.
6. Complete source-tree and clean installed-tree qualification before a
   modernized build is released.

Every milestone reruns the applicable automated source-tree and clean
installed-tree operator and contract-validation lanes currently used to
qualify the dashboard. Human accessibility evidence is renewed after material
UI changes and completed in full for the release candidate.

## Why React Flow and ELK.js fit GraphX

React Flow provides:

- explicit source and target handles that map naturally to GraphX ports;
- custom nodes capable of displaying execution state, queue metrics,
  diagnostics, warnings, controls, and small charts;
- custom SVG edges and bounded motion along edge paths;
- selection, pan, zoom, minimap, and fit-to-view behavior;
- TypeScript integration; and
- keyboard and screen-reader primitives that can be incorporated into the
  GraphX accessibility program.

ELK's layered layout supports explicit ports, orthogonal routing, and compound
graphs. It is suitable for the top-level directed pipeline. A custom repeated-
stage layout should handle the 64-detector bank rather than asking a generic
layout engine to make every branch equally prominent.

Cytoscape.js remains a credible alternative for a raw-topology view or much
larger graph-analysis workloads. React Flow is preferred for the modernized
implementation because GraphX nodes need rich HTML content and operator
inspection rather than only graphical shapes.

Primary upstream references:

- <https://reactflow.dev/learn/customization/custom-nodes>
- <https://reactflow.dev/learn/customization/handles>
- <https://reactflow.dev/examples/edges/animating-edges>
- <https://reactflow.dev/learn/advanced-use/performance>
- <https://reactflow.dev/learn/advanced-use/accessibility>
- <https://eclipse.dev/elk/reference/algorithms/org-eclipse-elk-layered.html>
- <https://js.cytoscape.org/>

## Product hierarchy

The modernized dashboard should expose two related levels.

### GraphX view

The reusable GraphX representation includes:

- topology;
- stable node and edge identities;
- exact input and output ports;
- execution and lifecycle state;
- queue and backpressure metrics;
- diagnostics;
- bounded aggregate edge activity;
- node and edge inspection; and
- configuration identity and provenance.

### FHSS view

The FHSS specialization adds:

- a collapsed detector bank;
- a 64-channel activity heatmap;
- an optional expanded 8x8 detector topology;
- pulse and message progression;
- expected-versus-observed evaluation;
- selected-channel spectrum;
- FHSS job controls; and
- investigation export, validation, and replay.

FHSS presentation remains a domain component. It must not leak generator truth
or schedules into receiver execution.

## FHSS display model

The Phase 2 binary-IQ receiver contains 75 nodes and 137 edges, including a
64-way channelizer fan-out, 64 parallel acquisition detectors, a 64-way merge,
and a final decoding pipeline.

The default operational representation should be:

```text
Binary IQ source
    -> Downconverter
    -> Channelizer
    -> Acquisition detector bank (64, collapsed)
    -> Pulse merge
    -> Candidate
    -> CPSM branch metric
    -> Viterbi decoder
    -> Word decoder
    -> Preamble detector
    -> Message assembler
    -> Sink
```

The detector-bank presentation should have four levels:

1. A collapsed bank node in the top-level pipeline.
2. A heatmap or table for operational channel activity.
3. An optional 8x8 expansion for structural diagnosis.
4. A detailed inspector for one selected detector.

Raw JSON remains an advanced fallback rather than the primary visualization.

## Presentation adapter

Do not send raw graph JSON directly into the renderer. Convert the current
GraphX resources into a typed display model such as:

```typescript
interface DisplayNode {
  id: string;
  graphNodeId: string;
  type: string;
  label: string;
  category: string;
  parentId?: string;
  inputPorts: DisplayPort[];
  outputPorts: DisplayPort[];
  configSummary: Record<string, string | number | boolean>;
}

interface DisplayEdge {
  id: string;
  graphEdgeId: string;
  source: string;
  sourceHandle: string;
  target: string;
  targetHandle: string;
  messageType?: string;
}

interface DisplayGroup {
  id: string;
  label: string;
  members: string[];
  layout: "pipeline" | "grid" | "fanout" | "fanin";
  collapsed: boolean;
}
```

Numeric GraphX ports become stable visual handle IDs such as `out-24` and
`in-25`. The renderer must preserve exact port numbers rather than normalizing
them. The Phase 2 graph, for example, connects detector 0 to merge input 1 and
detector 63 to merge input 64.

### Group detection

Prefix matching alone is not a durable grouping contract. Detector-bank
recognition should use deterministic structural evidence:

- exact detector node type;
- one common channelizer predecessor;
- one common merge successor;
- one-to-one channelizer output and merge input correspondence;
- equivalent configuration shape; and
- stable logical and physical channel identity.

Optional presentation metadata may override or clarify inferred groups, but it
must remain outside execution semantics. Invalid presentation metadata must
never change the GraphX graph.

## Identity prerequisite

The configuration graph uses stable string IDs, while current runtime metrics
use a mixture of node indices, node names, edge indices, and port indices.
Before metric overlays or animations are considered authoritative, GraphX must
define a canonical correlation contract:

```text
configuration node ID
    <-> runtime node identity
    <-> metric node identity
    <-> diagnostic source
    <-> visual node identity
```

Array position must not become a persistent identity. This reconciliation is
the most important backend prerequisite for the modernization.

## Telemetry and animation

### Reuse the existing event transport

Do not create independent `graph.snapshot`, `metrics.delta`, or `flow.events`
protocols. Extend the existing `/api/v1/fhss` resources and ordered event
envelopes only where the current schemas lack necessary information.

Every activity record must retain applicable:

- publisher epoch and sequence;
- graph generation and run epoch;
- configuration revision and ETag;
- stable node and edge identities;
- aggregation interval;
- metric unit and reset behavior; and
- dropped/coalesced-event reporting.

### Aggregate rather than trace every transfer

The server should aggregate display activity over a bounded interval. The
browser translates an aggregate into representative motion, edge intensity,
width, rate labels, or badges.

An illustrative event is:

```json
{
  "event_type": "graph.edge_activity",
  "window_start_ns": 47199000000,
  "window_end_ns": 47199200000,
  "edges": [
    {
      "edge_id": "channelizer:24->detector_24:0",
      "message_class": "channel_iq",
      "messages": 16,
      "bytes": 6710886
    }
  ]
}
```

This is a proposed schema, not an existing contract. It must go through the
normal OpenAPI/JSON Schema, bounds, negative-test, reconnect, and versioning
process before implementation.

### Missing metric semantics

The current collector provides cumulative message counters, rejected messages,
backpressure events, peak queue depth, and activity state. It does not expose
all proposed values such as execution latency, bytes per second, or invocation
rate.

Before displaying such values, either:

- derive rates from generation-bound monotonic counter deltas over explicit
  intervals; or
- extend the collector with exact units, interval, clock, reset, aggregation,
  and availability semantics.

Queue residence time, node processing time, end-to-end latency, and dashboard
delivery latency are different measurements and must not share an ambiguous
`latency` label.

### Bounded visual behavior

The implementation must impose explicit limits on:

- visible animated markers;
- markers per edge;
- event update rate;
- chart update rate;
- retained visual history;
- expanded group size;
- layout time; and
- browser memory growth.

Excess activity becomes heat, width, badges, or rates instead of additional
moving objects.

## Viewer and command boundary

React Flow is also an editing toolkit. The dashboard must default to a read-only
topology:

- no new connections;
- no edge reconnection;
- no node deletion;
- no topology mutation;
- no implication that dragging changes execution; and
- no direct node or edge access to GraphManager or queue state.

If node positions are draggable, they are local presentation state and must be
labelled and persisted separately. Runtime and configuration commands continue
through the existing authorized controls and validated services.

## Accessibility requirements

The graph canvas must not be the only way to understand or operate the graph.
The modernized dashboard requires:

- a semantic table or tree alternative;
- keyboard selection and group expansion;
- stable focus across live updates;
- text equivalents for edge activity;
- reduced-motion support;
- pauseable animation and speed controls;
- no status conveyed only by color or motion;
- meaningful shapes and labels for message classes;
- responsive 320 CSS-pixel reflow; and
- renewed human WCAG verification.

Making all 137 edges separately tabbable may produce a poor experience. The
collapsed operational model should reduce both visual and accessibility noise.

Existing automated and human accessibility gates remain applicable; adopting
React Flow does not itself prove WCAG conformance.

## Security, supply chain, and packaging

The frontend must be completely self-hosted. Do not load React, React Flow,
ELK, fonts, scripts, styles, or maps from a CDN.

The build should require:

- committed lockfiles;
- pinned and reviewed package versions;
- license and third-party notices;
- vulnerability review;
- reproducible or offline-capable frontend builds;
- hashes and inventory for installed bundles;
- CSP validation;
- safe DOM and dependency audits;
- source-tree and installed-tree tests; and
- a dashboard-disabled build that does not require Node tooling.

The current CSP accepts self-hosted scripts, styles, images, and connections.
If ELK runs in a Web Worker, the worker must be a self-hosted asset and tested
against the maintained CSP. Blob-based workers require an explicit security
decision rather than an incidental CSP relaxation.

React Flow is MIT licensed, but every transitive dependency must still be
inventoried and reviewed.

## Qualification boundary

The modernized dashboard remains:

- FHSS-specific for its first implementation;
- loopback-only;
- synthetic-IQ and software-evidence only;
- without HWIL, conducted-RF, OTA, or live-RF evidence; and
- not production-RF qualified.

Expected truth, receiver observations, and evaluator comparison remain
separate typed models and UI layers. Receiver execution receives binary IQ and
receiver-minimal configuration only.

The existing architecture recommendation to defer generic extraction still
applies. A second dashboard domain with independent requirements should inform
any future generic frontend framework.

## Recommended implementation sequence

### Phase 0: Baseline lock and modernization harness

#### Architecture and single-implementation decision

- Record the React Flow plus ELK.js choice in a formal architecture decision,
  including the reason for superseding the historical Cytoscape.js choice.
- Record that `V2` is a project label only: the executable serves one UI at
  `/`, installs one asset set, and retains `/api/v1/fhss` as the only API
  namespace.
- Prohibit side-by-side UI routes, runtime UI selectors, duplicate API route
  trees, and a packaged legacy implementation.

#### Baseline evidence

- Freeze and hash the pre-modernization OpenAPI document, schemas,
  representative API responses, event envelopes, reconnect/resynchronization
  transcripts, operator workflows, qualification outputs, current asset
  inventory, and installed-tree layout.
- Record feature-level expected behavior for configuration, runtime lifecycle,
  message jobs, observations, comparison, spectrum, investigations, event
  recovery, and shutdown.
- Store baseline manifests and expected results as reviewable test inputs; do
  not preserve the prototype as a second served application.

#### Qualification-harness generalization

- Replace the current two-file `index.html`/`fhss_transport_state.js` asset
  assumptions with a recursive, bounded frontend inventory that covers HTML,
  hashed JavaScript, CSS, worker files, licenses, and permitted debug-only
  source maps.
- Hash-bind source-tree and installed-tree inventories and require exact
  agreement for release assets.
- Scan all shipped HTML and JavaScript for unsafe DOM sinks, inline handlers,
  CSP violations, unexpected network references, and unapproved dynamic code.
- Version package/evidence schemas only where their data shape changes; this
  evidence-schema versioning does not create a new dashboard API.

#### Stable topology identity

- Define the minimum canonical mapping among configuration node IDs, exact
  source/target ports, configuration edge identity, runtime node identity,
  metric identity, diagnostic source, and visual identity.
- Phase 1 may render configuration topology only after this mapping has a
  deterministic contract and golden tests. Runtime overlays remain disabled
  until their Phase 3 correlation contract passes.
- Forbid array positions as persistent node or edge identity.

#### Frontend toolchain and asset budgets

- Select and pin the Node.js runtime, package manager, React, React Flow,
  ELK.js, TypeScript, Vite, and all resolved transitive dependencies.
- Commit the lockfile, third-party notices, license inventory, integrity
  records, and an offline or controlled-cache provisioning procedure.
- Define per-asset and total installed-size budgets. Every served file must fit
  the server's current 4 MiB response limit; production bundles must be split
  accordingly.
- Require self-hosted external scripts, styles, and workers; prohibit CDN
  dependencies, unapproved `eval`, release source maps, and incidental CSP
  relaxation.
- Verify required MIME types and keep dashboard-disabled builds independent of
  Node and frontend packages.

#### Phase and release gates

- Require contract validation, frontend unit tests, C++ focused tests,
  source-tree operator tests, clean installed-tree tests, package inventory,
  CSP/safe-DOM checks, accessibility automation, and `git diff --check` in
  every implementation phase.
- Require focused human keyboard, focus, motion, and reflow review after each
  material UI change.
- Require full human WCAG evidence, reconnect/soak evidence,
  sanitizer/concurrency lanes, and full regression results for the release
  candidate.
- Define rollback as rebuilding or reinstalling the last qualified source or
  release artifact. Rollback must not depend on dormant legacy assets or a
  runtime implementation switch.

#### Phase 0 exit criteria

Phase 0 passes only when:

1. The React Flow/ELK.js architecture decision and single-implementation rule
   are reviewed and recorded.
2. A schema-valid, hash-bound baseline manifest covers the authoritative API,
   schemas, representative responses, event/reconnect transcripts, operator
   behavior, qualification outputs, asset inventory, and installed layout.
3. Qualification tooling recursively inventories and scans the current asset
   set without assuming that the dashboard consists of exactly two files.
4. Source-tree and clean installed-tree inventories match and every asset is
   within the declared per-file and total-size budgets.
5. Golden identity tests cover all 75 nodes, all 137 edges, and every exact
   source and target port in the Phase 2 binary-IQ receiver graph.
6. The pinned frontend toolchain, lockfile policy, licenses, integrity records,
   offline provisioning, CSP rules, MIME types, and source-map policy are
   documented and independently checkable.
7. Tests prove that one root dashboard and one `/api/v1/fhss` route tree are
   installed and served, with no alternate-UI selector, duplicate implementation,
   or `/api/v2` route.
8. Phase 0 makes no public dashboard API semantic change and all focused and
   full inherited regression and operator lanes pass.

### Phase 1: Read-only topology prototype

- Create the pinned TypeScript/Vite/React Flow build.
- Consume the existing graph API.
- Render stable nodes, exact ports, and edges.
- Add selection, pan, zoom, fit-to-view, and an inspector.
- Preserve all inherited dashboard panels and operator capabilities in the one
  current implementation.
- Add source-tree and installed-tree packaging tests.

### Phase 2: FHSS presentation adapter

- Add structurally detected detector-bank grouping.
- Render the collapsed default pipeline.
- Retain the existing heatmap/table behavior.
- Add optional 8x8 expansion and selected-detector inspection.
- Keep raw JSON as an advanced diagnostic view.

### Phase 3: Identity and metrics contracts

- Establish canonical configuration/runtime/metric/visual identities.
- Add any missing current-queue-depth contract.
- Define rate, interval, reset, and latency semantics.
- Update OpenAPI, schemas, negative tests, and compatibility policy.

### Phase 4: Bounded activity visualization

- Extend the existing publisher with aggregate edge activity.
- Add message-class shapes and text labels.
- Add pause, speed, reduced-motion, and overload suppression.
- Prove that instrumentation overhead is bounded and does not participate in
  scheduling.

### Phase 5: Feature parity and qualification

- Complete configuration, runtime, jobs, observations, spectrum, and
  investigation integration.
- Test reconnect, replay, gaps, resynchronization, and stale generations.
- Repeat CSP, safe-DOM, accessibility, keyboard, reflow, soak, sanitizer,
  concurrency, installed-tree, and full-regression validation.
- Release the modernized dashboard only after all acceptance criteria pass.

## Acceptance principles

The modernization is successful only if it:

1. Presents GraphX topology more richly without changing execution semantics.
2. Explains the FHSS processing structure rather than exposing only raw JSON.
3. Preserves exact ports and stable identities.
4. Keeps animation representative, bounded, and optional.
5. Preserves truth-free receiver execution and evidence separation.
6. Remains secure and functional without network-loaded assets.
7. Has an accessible non-canvas representation.
8. Preserves all current operator-facing capabilities throughout the in-place
   replacement.
9. Passes source-tree and clean installed-tree qualification.
10. Does not claim HWIL or production-RF qualification.
11. Preserves or explicitly versions the evidence schemas and validation
   workflows needed to prove source-tree/installed-tree equivalence.
12. Ships exactly one dashboard implementation and one `/api/v1/fhss` API
   namespace, with no alternate-UI runtime selector or duplicate implementation.
