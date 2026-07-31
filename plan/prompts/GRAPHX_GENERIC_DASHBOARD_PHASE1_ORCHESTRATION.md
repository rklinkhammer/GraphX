# GraphX Generic Dashboard Phase 1 Orchestration

Implement Phase 1 of:

- `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`

Use these as normative supporting descriptions:

- `docs/graphx_dashboard.md`
- `plan/Phase2B_Generic_Graph_Management.md`
- `plan/Phase2B_Corrected_Implementation_Specification.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE0_ACCEPTANCE_CHECKLIST.md`

Phase 0 is complete and independently verified. Treat its configuration,
lifecycle, capability, ownership, HTTP, and launcher contracts as the baseline;
do not redesign or bypass them while adding the graphical view.

## Verified Phase 0 baseline

The Phase 1 implementation starts from these verified facts:

- `GraphCoordinator` is the sole owner and revision authority for graph JSON.
- `GET /api/v1/graph` returns the authoritative topology document.
- `graphx-dashboard` owns one configured, lazy `GraphExecutor`.
- `GraphHttpServer` receives the shared coordinator and stable command/metrics
  capabilities; it has no executor pointer and makes no direct executor
  lifecycle calls.
- The lifecycle is
  `ConfigureGraph → Init → Start → Run → Stop → Join`.
- Loading the dashboard leaves the executor in `CONFIGURED` and performs no
  provider loading, node construction, initialization, or execution.
- Node configuration changes continue through
  `PATCH /api/v1/nodes/{id}` and make the executor configuration dirty until
  the operator explicitly reconfigures.
- `graph-cli` is deprecated but remains built, installed, warned, and tested.
- The final Phase 0 tree passed the C++26 build, 1,067/1,067 libgraph tests,
  37/37 enabled configured CTests, focused loopback tests, and
  `git diff --check`.

Preserve those behaviors and their tests.

The Phase 0 verifier also recorded one pre-existing medium issue:
`SimpleHttpServer` retains completed request-thread objects until shutdown.
Phase 1 increases browser use of the HTTP server, so bound or retire completed
request workers without detaching threads or weakening shutdown guarantees.
Add a repeat-request regression proving retained worker bookkeeping remains
bounded. Do not turn this into general network-security hardening.

## Agent model assignments

- **Orchestrator:** `gpt-5.6-sol`, `max` reasoning. This role owns phase scope,
  renderer/toolchain decisions, architecture boundaries, and acceptance.
- **Implementer:** `gpt-5.6-sol`, `xhigh` reasoning. This role implements the
  generic frontend, static-resource integration, browser behavior, fixtures,
  tests, and the bounded request-worker correction.
- **Verifier:** `gpt-5.6-sol`, `ultra` reasoning with independent context. This
  role performs adversarial topology-fidelity, browser, install-tree,
  accessibility, architecture, and regression verification and does not edit
  implementation files.

Do not substitute a faster general-purpose model for the verifier. Exact port
identity, topology fidelity, installed assets, and interaction state require
independent visual and structural review.

## Orchestrator

1. Audit what Phase 1 or renderer-spike work already exists. Do not recreate
   completed work or import the legacy FHSS dashboard frontend.
2. Convert every Phase 1 requirement into a file-level acceptance checklist,
   including the verified Phase 0 invariants.
3. Resolve the renderer decision before implementation:
   - use a durable no-framework SVG implementation only if the repository
     already contains a maintainable generic spike that supports exact ports,
     layered layout, pan/zoom, selection, and the expected graph sizes; or
   - use pinned, self-hosted frontend dependencies under a generic
     `libgraph` source/resource tree.
   Record the evidence and decision. Do not create an interim renderer that a
   later phase must discard.
4. Preserve unrelated worktree changes.
5. Assign all implementation to one implementer agent.
6. After implementation and focused tests, assign independent review to one
   verifier agent with fresh context.
7. Route every verifier finding back to the implementer.
8. Repeat implementation and verification until every Phase 1 criterion is
   PASS and no blocking or high-severity finding remains.
9. Do not commit, push, open a PR, proceed to Phase 2, remove `graph-cli`, add
   hierarchy/grouping semantics, or implement the later metrics subscriber
   phase.

## Required Phase 1 implementation

### One existing dashboard and one topology source

- Extend the existing generic dashboard rooted at
  `libgraph/resources/web/index.html`.
- Add a **Topology** view within that page. Do not add another root page,
  dashboard executable, server, coordinator, runtime session, configuration
  service, or API namespace.
- Fetch topology with one initial `GET /api/v1/graph`. Do not reconstruct the
  graph by combining `/nodes` calls and do not add a new topology endpoint.
- Keep the existing management header, executor state and lifecycle controls,
  node search/table, node inspector, and node-config editor.
- Use one client-side graph document and one shared selection model for the
  canvas, table, and inspector.
- After a successful existing node-config PATCH, refresh the authoritative
  graph response and update the table, canvas, inspector, revision/dirty
  presentation, and selected node without creating a second client-side graph
  authority.

### Generic topology adapter

- Add a domain-neutral adapter from the existing graph response to display
  nodes, exact ports, and edges.
- Preserve every authoritative node ID, node type, edge identity, and endpoint.
- Support both port forms:
  - numeric `source_port` and `target_port`;
  - named `source_port_name` and `target_port_name`.
- Use deterministic, collision-free display identities derived from the full
  authoritative endpoint tuple. Do not use array position as identity.
- Visibly report malformed nodes, duplicate identities, missing endpoints,
  invalid port values, and unsupported graph shapes. Do not silently drop,
  repair, synthesize, or reconnect topology.
- Treat disconnected and cyclic graphs as displayable input. Layout failure
  must produce a visible diagnostic and retain a semantic fallback rather than
  an empty page.
- Keep the production renderer structurally read-only. There must be no
  add/delete node, connect/reconnect/delete edge, node rename/retype, or
  topology-persistence gesture.

### Topology presentation

- Render every node and authoritative edge, including explicit source and
  target port handles and labels.
- Implement deterministic layered layout with stable results for the same
  graph document. Layout is presentation-only and must not PATCH or otherwise
  mutate graph JSON.
- Implement:
  - pan;
  - zoom in/out;
  - fit-to-view;
  - reset layout;
  - canvas node and edge selection; and
  - clear selection.
- Provide a node/edge inspector sourced from the same in-memory graph response.
  The edge inspector must show source node, exact source port kind/value,
  target node, and exact target port kind/value.
- Selecting a canvas node or the corresponding node-table row must select the
  same node and open the same inspector/editor state.
- Retain selection across a successful graph refresh when the selected
  identity still exists; clear it truthfully when it no longer exists.
- Provide meaningful empty, loading, and error states. A fetch or adapter error
  must not leave a blank page with only the document title.
- Provide basic keyboard-operable view switching, selection, fit/reset
  controls, visible focus, and semantic labels. Full Phase 3 usability work is
  not part of this phase, but Phase 1 must not create mouse-only controls.

### Renderer and asset policy

- Do not use a CDN or load code from the network at runtime.
- If frontend dependencies are needed, pin them in a lockfile and keep editable
  sources under a generic `libgraph` frontend directory, never under
  `examples/DSP/dashboard`.
- Use the installed Node.js version on the host. Do not impose an exact Node
  patch/minor version when the installed compatible version can build the
  pinned frontend.
- Do not create a virtual Python/Node environment or use `/private/tmp` or
  related paths. If a required host package is missing, stop and ask the user
  to install it on the host.
- Node.js may be a maintainer/build-time tool only. The installed dashboard
  must not require Node.js.
- The ordinary C++ configure/build/test workflow must remain usable without
  downloading frontend packages. If generated assets are checked in, verify
  that rebuilding from the lockfile reproduces their inventory and content.
- If the resource set becomes multi-file, extend the existing
  `GraphHttpServer` static-resource handling and CMake install rules with:
  - bounded path handling and containment within the one generic resource root;
  - correct MIME types;
  - source-tree/installed-tree parity; and
  - explicit 404/405 behavior.
  Do not add another HTTP server.

### Required fixtures and independent oracles

Add or reuse small independently authored fixtures covering:

- empty graph;
- malformed node/edge/port input;
- disconnected graph;
- cyclic graph;
- numeric ports;
- named ports;
- split/fanout;
- merge/fanin; and
- combined split/merge.

At minimum exercise these repository graphs:

- `libgraph/test/config/topologies/minimal_graph.json`;
- `libgraph/test/config/topologies/split_simple.json`;
- `libgraph/test/config/topologies/merge_simple.json`;
- `libgraph/test/config/topologies/complex_network.json`;
- one representative SAR graph; and
- `libdsp/config/fhss_phase2_binary_iq_receiver.json`.

The FHSS fixture is an acceptance graph only. Verify its authoritative
75-node/137-edge cardinality and exact detector-bank port mappings from the
source JSON. Do not add `FHSS`, detector-count, frequency-index, IQ, message,
or receiver rules to the generic adapter, renderer, server, or routes.

Tests must not use the production adapter or renderer as their only oracle.
Expected node IDs, edge endpoint tuples, port kinds/values, and cardinalities
must come from independently authored fixture expectations and direct
source-JSON checks.

### Request-worker retention correction

- Replace the unbounded completed request-thread retention in
  `SimpleHttpServer` with bounded, join-safe retirement.
- Never detach request threads.
- Preserve prompt shutdown, loopback behavior, response timeouts, and clean
  joining of active workers.
- Add tests that perform substantially more sequential and overlapping
  requests than the retention bound and prove:
  - responses remain correct;
  - completed worker bookkeeping stays bounded;
  - active workers are joined during shutdown; and
  - no self-join, use-after-free, leak-by-design, or shutdown hang occurs.

## Tests and operator deliverables

Add layered validation:

1. **Adapter unit tests** for identities, both port forms, malformed input,
   empty/disconnected/cyclic graphs, split/merge/fanout, and deterministic
   conversion.
2. **Renderer/component tests** for node/edge/handle counts, deterministic
   layout, selection synchronization, inspector fields, refresh behavior, and
   absence of structural mutation gestures.
3. **C++ HTTP tests** for `/api/v1/graph`, all static assets, MIME types,
   containment, wrong methods, missing assets, repeat requests, and shutdown.
4. **Real-browser tests** against a launched `graphx-dashboard` for render,
   pan/zoom/fit/reset, table/canvas selection synchronization, node-config edit
   refresh, keyboard operation, and visible errors.
5. **Architecture scans** rejecting FHSS routes/rules, legacy dashboard
   dependencies, a second root page/executable/server/coordinator/runtime, CDN
   references, and direct frontend access to executor internals.
6. **Source-tree and installed-tree smoke tests** proving the same asset
   inventory and behavior.

Create:

- `docs/graphx_dashboard_phase1_operator_test.md`

The operator document must start from a fresh clone and cover the native
non-Docker workflow first:

- configure and build from first principles using the appropriate repository
  preset;
- install to a repository-local install prefix;
- launch both source-tree and installed `graphx-dashboard`;
- inspect the minimal, complex-network, SAR, and FHSS graphs;
- record authoritative versus rendered node/edge counts and exact sample port
  tuples;
- exercise pan, zoom, fit, reset, selection, inspector, and node-config edit
  refresh;
- verify the page remains read-only structurally;
- record screenshots and browser-console errors; and
- state expected results and troubleshooting steps.

Docker is not required and the legacy FHSS dashboard container must not be used
as the generic dashboard. Do not add a generic Docker image in this phase
unless separately authorized.

## Implementer report

Report:

- files changed and why;
- audited pre-existing Phase 1 work;
- renderer/toolchain decision and evidence;
- topology identity, adapter, layout, selection, and refresh design;
- static-resource source/install inventory;
- request-worker retention design and bound;
- exact fixture/oracle cardinalities;
- commands run and exact results;
- source-tree, installed-tree, and real-browser results;
- focused and full regression results; and
- known limitations reserved for Phase 2 or later phases.

Build affected C++ in repository C++26 mode with warnings treated as errors.
Use repository-local build/test paths. Preserve the existing native,
non-Docker build and dashboard workflow.

## Independent verifier

The verifier must not edit implementation, test, generated asset, or
documentation files. It must give explicit PASS or FAIL for every Phase 1
criterion with file and line evidence.

Verify especially:

- the existing `index.html`, `GraphHttpServer`, `GraphCoordinator`,
  `graphx-dashboard`, and `/api/v1` remain the only generic management path;
- the browser performs one initial authoritative graph fetch and does not
  create a second topology authority;
- every source edge and exact numeric/named endpoint is represented once;
- malformed input is visible rather than silently repaired or discarded;
- minimal, split, merge, complex, SAR, and 75-node/137-edge FHSS graphs render
  without domain-specific frontend code;
- deterministic layout is stable across repeated loads;
- canvas and table selection use the same inspector state;
- a node-config PATCH refreshes both views and preserves valid selection;
- no structural mutation gesture or persistence request exists;
- pan, zoom, fit, reset, selection, and keyboard controls work in a real
  browser;
- empty/loading/error states are visible and no tested failure produces a
  title-only blank page;
- frontend dependencies are pinned/self-hosted and installed output has no
  Node.js or CDN runtime dependency;
- source-tree and installed-tree assets and behavior match;
- completed HTTP request-worker bookkeeping remains bounded and shutdown joins
  active workers;
- Phase 0 lifecycle, command, metric-capability identity, CLI deprecation, and
  dirty-revision tests still pass;
- tests use independent fixture expectations rather than the production
  renderer as their only oracle;
- affected tests, full configured regression, and `git diff --check` pass.

Use direct browser inspection and screenshots in addition to DOM assertions.
Run the real dashboard rather than validating only static HTML or mocked
responses.

## Stop condition

Stop only when:

- the verifier reports no unresolved blocking or high-severity findings;
- every Phase 1 criterion is PASS;
- source-tree and installed-tree browser tests pass;
- focused adapter/renderer/HTTP/operator tests pass;
- the full configured regression suite passes;
- the C++26 warnings-as-errors build passes; and
- `git diff --check` passes.

Provide a Phase 1 completion report and wait for authorization. Do not proceed
to Phase 2 automatically.
