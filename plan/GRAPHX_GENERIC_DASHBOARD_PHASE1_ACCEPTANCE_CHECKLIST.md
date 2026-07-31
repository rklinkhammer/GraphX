# GraphX Generic Dashboard Phase 1 Acceptance Checklist

This checklist operationalizes Phase 1 of
`plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md` and
`plan/prompts/GRAPHX_GENERIC_DASHBOARD_PHASE1_ORCHESTRATION.md`.

## Renderer decision

Repository audit found no durable generic graphical spike. The supported
frontend is one inline `libgraph/resources/web/index.html`; React Flow/ELK
sources exist only in the prohibited legacy FHSS dashboard tree.

Phase 1 therefore uses a new generic React Flow/ELK TypeScript frontend:

- editable sources and pinned lockfile under a generic `libgraph` directory;
- the existing `libgraph/resources/web/index.html` remains the sole entry
  point;
- generated JS/CSS assets are self-hosted below the same generic resource root;
- `GraphHttpServer` remains the only server;
- Node.js is build-time only and the normal C++ build can use checked-in assets
  without downloading packages; and
- no legacy `examples/DSP/dashboard` source, asset, route, or runtime component
  is imported or linked.

The audit used the compatible host tools already installed:
Node.js `v26.5.0` and npm `11.17.0`. Phase 1 must not impose the obsolete
Node.js 24.4.1 restriction.

## A. Phase 0 preservation

- [ ] A1. `GraphCoordinator` remains the sole graph-document and revision
  authority.
- [ ] A2. `GraphHttpServer` still receives the shared coordinator and
  command/metrics capabilities and has no executor pointer or direct lifecycle
  calls.
- [ ] A3. Page load leaves the one mandatory executor in `CONFIGURED` without
  provider loading, graph construction, initialization, or execution.
- [ ] A4. Phase 0 lifecycle, operation, revision/dirty, multi-generation,
  capability-identity, destruction, and HTTP tests remain green.
- [ ] A5. `graph-cli` remains deprecated, built, installed, warned, and tested.
- [ ] A6. No Phase 2 hierarchy/grouping or later metrics-subscriber work is
  introduced.

## B. One generic frontend and asset system

- [ ] B1. `libgraph/resources/web/index.html` remains the sole dashboard entry
  point and contains the existing management header, execution controls,
  search/table, inspector, and node-config editor.
- [ ] B2. Editable frontend sources and package metadata are generic
  `libgraph` artifacts, not legacy FHSS dashboard artifacts.
- [ ] B3. React Flow, ELK, and all transitive runtime assets are pinned and
  self-hosted; no CDN or runtime network code load exists.
- [ ] B4. Generated asset inventory is deterministic/reproducible from the
  lockfile and checked-in assets.
- [ ] B5. The standard C++ configure/build/test path requires neither Node.js
  nor package download; the installed dashboard requires no Node.js runtime.
- [ ] B6. Source-tree and installed-tree servers expose the same contained
  asset inventory with correct MIME types and 404/405 behavior.
- [ ] B7. Static path handling cannot escape the one generic resource root.
- [ ] B8. No second page, executable, server, coordinator, runtime,
  configuration authority, API namespace, or asset tree is added.

## C. Authoritative topology adapter

- [ ] C1. Initial display uses one `GET /api/v1/graph`, not a topology rebuilt
  from `/nodes` requests.
- [ ] C2. One client-side graph document feeds the table, canvas, and
  inspector.
- [ ] C3. Every authoritative node ID and type is preserved exactly once.
- [ ] C4. Every authoritative edge is preserved exactly once with a
  deterministic collision-free identity derived from its full endpoint tuple,
  not its array position.
- [ ] C5. Numeric `source_port`/`target_port` and named
  `source_port_name`/`target_port_name` are preserved with explicit kind and
  value.
- [ ] C6. Duplicate identities, malformed nodes/edges/ports, unknown
  endpoints, and unsupported values produce visible diagnostics and are not
  silently dropped or repaired.
- [ ] C7. Empty, disconnected, and cyclic graphs retain a useful semantic
  display even if layout cannot produce a normal layered drawing.
- [ ] C8. Tests derive expected identities and endpoint tuples independently
  from small fixtures/direct source JSON; the production adapter is not its
  own oracle.

## D. Read-only topology presentation

- [ ] D1. The existing page contains a clearly identified Topology view.
- [ ] D2. Every display node renders ID and type plus exact input/output port
  handles and labels.
- [ ] D3. Every display edge connects its exact source and target handles.
- [ ] D4. Deterministic layered layout produces stable positions/order for the
  same document.
- [ ] D5. Pan, zoom in/out, fit-to-view, reset-layout, node/edge selection, and
  clear-selection work.
- [ ] D6. Node and edge inspectors are sourced from the same graph document;
  edge inspection shows both node IDs and both port kind/value pairs.
- [ ] D7. Canvas-node and table-row selection share one selection/inspector
  state.
- [ ] D8. Successful PATCH refreshes the authoritative graph and synchronizes
  table, canvas, inspector, revision/dirty state, and valid selection.
- [ ] D9. Loading, empty, adapter-error, fetch-error, and layout-error states
  are visible; no failure yields a title-only blank page.
- [ ] D10. View, fit/reset, and selection controls are keyboard operable with
  visible focus and semantic labels.
- [ ] D11. No gesture or request adds/deletes/renames/retypes nodes,
  adds/deletes/reconnects edges, or persists layout positions.

## E. Generic fixture coverage

- [ ] E1. Independently authored fixtures cover empty, malformed,
  disconnected, cyclic, numeric-port, named-port, split/fanout, merge/fanin,
  and combined split/merge cases.
- [ ] E2. `minimal_graph.json` renders two nodes, one edge, and all four
  endpoint fields exactly.
- [ ] E3. `split_simple.json` renders every fanout edge and exact port.
- [ ] E4. `merge_simple.json` renders every fanin edge and exact port.
- [ ] E5. `complex_network.json` renders its source-authoritative
  cardinalities.
- [ ] E6. A representative SAR graph renders through the same generic adapter.
- [ ] E7. `fhss_phase2_binary_iq_receiver.json` renders exactly 75 nodes and
  137 edges, including exact detector-bank mappings.
- [ ] E8. Generic frontend/server code contains no FHSS, detector-count,
  frequency-index, IQ, message, receiver, or SAR presentation rule.

## F. Request-worker retention correction

- [ ] F1. Completed HTTP request workers are retired with bounded bookkeeping;
  threads are never detached.
- [ ] F2. Sequential requests substantially beyond the bound remain correct
  and do not grow retained completed workers without bound.
- [ ] F3. Overlapping requests substantially beyond the bound remain correct.
- [ ] F4. Shutdown interrupts/joins active workers promptly without self-join,
  use-after-free, deliberate leak, or hang.
- [ ] F5. Existing header/body bounds, timeouts, loopback binding, and response
  behavior remain intact.

## G. Tests and operator evidence

- [ ] G1. Adapter unit tests cover identities, port forms, malformed input,
  empty/disconnected/cyclic input, split/merge/fanout, and determinism.
- [ ] G2. Renderer/component tests cover counts, handles, layout, shared
  selection, inspectors, refresh, error states, keyboard controls, and
  structural read-only behavior.
- [ ] G3. C++ HTTP tests cover the graph resource, all static assets, MIME
  types, containment, missing resources, wrong methods, repeated requests, and
  shutdown.
- [ ] G4. A real-browser source-tree test covers render, pan/zoom/fit/reset,
  table/canvas selection, PATCH refresh, keyboard operation, and visible
  diagnostics.
- [ ] G5. The same real-browser behavior passes against an installed tree.
- [ ] G6. Architecture tests reject parallel management components, legacy
  dashboard dependencies, new route namespaces, CDN references, and executor
  internals in the frontend.
- [ ] G7. `docs/graphx_dashboard_phase1_operator_test.md` starts from a fresh
  clone and documents native source/installed workflows, four representative
  graphs, exact expected counts/ports, screenshots, console checks,
  read-only checks, and troubleshooting.
- [ ] G8. Operator documentation does not require Docker or the legacy FHSS
  container.

## H. Validation gates

- [ ] H1. Affected C++ builds in repository C++26 mode with warnings as errors.
- [ ] H2. Frontend build/type/component tests pass using the pinned lockfile.
- [ ] H3. Checked-in generated asset reproduction/inventory check passes.
- [ ] H4. Focused adapter, renderer, HTTP, worker-retention, architecture,
  browser, and operator tests pass.
- [ ] H5. Source-tree and installed-tree dashboard smoke/browser tests pass.
- [ ] H6. Full configured CTest passes with only documented
  hardware/data/local gates disabled or skipped.
- [ ] H7. `git diff --check` passes.
- [ ] H8. The independent verifier reports explicit PASS for every item above
  and no unresolved blocking or high-severity finding.

