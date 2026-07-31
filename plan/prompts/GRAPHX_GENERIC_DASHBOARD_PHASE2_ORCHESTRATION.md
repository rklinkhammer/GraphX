# GraphX Generic Dashboard Phase 2 Orchestration

Implement Phase 2 of:

- `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`

Use these as normative supporting descriptions:

- `docs/graphx_dashboard.md`
- `plan/Phase2B_Generic_Graph_Management.md`
- `plan/Phase2B_Corrected_Implementation_Specification.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE0_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE1_ACCEPTANCE_CHECKLIST.md`
- `docs/graphx_dashboard_phase1_operator_test.md`

Phase 0 and Phase 1 are complete and independently verified. Treat their
runtime, topology, frontend, packaging, HTTP, lifecycle, and validation
contracts as the baseline. Do not redesign or bypass them while adding
hierarchy and large-graph navigation.

## Verified baseline

The Phase 2 implementation starts from these verified facts:

- `GraphCoordinator` remains the sole graph-document and revision authority.
- `GET /api/v1/graph` is the sole authoritative topology input.
- The page uses one client-side graph document for table, canvas, and
  inspectors.
- The existing generic React Flow/ELK TypeScript frontend lives under
  `libgraph/web`; generated JS/CSS assets are pinned, reproducible, self-hosted,
  and served below the one generic resource root.
- `libgraph/resources/web/index.html` is the sole dashboard entry point.
- Source-tree and installed-tree dashboards select one sticky resource root;
  a missing entry point returns the established `503 ui_unavailable`, a
  missing child asset returns `404`, and neither borrows from a developer
  source tree.
- The canvas preserves exact numeric/named port identities, deterministic
  full-tuple edge identities, shared selection, PATCH refresh, keyboard
  operation, visible error states, and structural read-only behavior.
- Phase 1 independently rendered minimal 2/1, split 4/3, merge 4/3, complex
  9/9, SAR 21/23, and FHSS 75/137 topologies, including all 128 detector-bank
  edges.
- `SimpleHttpServer` uses 8 joinable workers plus a bounded 16-client pending
  queue, rejects overload, survives reset writes without SIGPIPE, selects one
  static root, and shuts down safely during active requests and destruction.
- Phase 1 passed clean dependency installation, typecheck, 26/26 frontend
  tests, asset reproduction, 20/20 HTTP tests, 7/7 sanitizer lifecycle tests,
  source/installed Firefox validation, C++26/`-Werror`, 37/37 enabled CTests,
  and `git diff --check`.
- `graph-cli` remains deprecated but built, installed, warned, and tested.

Preserve all of those behaviors and their regression coverage.

## Agent model assignments

- **Orchestrator:** `gpt-5.6-sol`, `max` reasoning. This role owns the exact
  generic hierarchy schema, bounds, collapse/bundle semantics, phase scope,
  and acceptance decisions.
- **Implementer:** `gpt-5.6-sol`, `xhigh` reasoning. This role implements the
  presentation schema adapter, compound rendering, navigation, fixtures,
  browser behavior, tests, generated assets, and operator documentation.
- **Verifier:** `gpt-5.6-sol`, `ultra` reasoning with independent context. This
  role performs adversarial hierarchy-validation, identity-preservation,
  collapse/bundle, accessibility, packaging, browser, and regression review
  and does not edit implementation files.

Do not substitute a faster general-purpose model for the verifier. Collapsed
presentation must remain provably reversible to exact authoritative nodes,
edges, and ports.

## Orchestrator

1. Audit current Phase 2, hierarchy, minimap, and grouping artifacts. Do not
   recreate completed work and do not import the legacy FHSS dashboard.
2. Convert every Phase 2 requirement and every preserved Phase 0/1 invariant
   into a file-level acceptance checklist.
3. Before assigning implementation, lock and record:
   - the exact `presentation.groups` JSON schema;
   - node/group membership and nesting rules;
   - invalid/overlap/cycle handling;
   - bundle-edge identity and authoritative-membership rules;
   - selection behavior across collapse/expand/isolate/raw-mode transitions;
   - deterministic semantics for `layered`, `grid`, `fanout`, and `fanin`;
   - numeric bounds for group count, depth, direct members, total
     memberships, bundle membership, layout work, and visible detail; and
   - the fallback behavior when metadata or a bound is invalid.
4. Preserve unrelated worktree changes.
5. Assign all implementation to one implementer agent.
6. After implementation and focused tests, assign independent review to one
   verifier agent with fresh context.
7. Route every verifier finding back to the implementer.
8. Repeat implementation and verification until every Phase 2 criterion is
   PASS and no blocking or high-severity finding remains.
9. Do not commit, push, open a PR, proceed to Phase 3, remove `graph-cli`,
   implement metrics subscription/overlays, add topology mutation, or create a
   parallel dashboard architecture.

## Required Phase 2 implementation

### Optional generic presentation schema

Define this execution-neutral top-level shape, retaining the Phase 1 graph
shape unchanged when it is absent:

```json
{
  "presentation": {
    "groups": [
      {
        "id": "processing-bank",
        "label": "Processing bank",
        "members": ["node-a", "node-b"],
        "parent": "optional-parent-group-id",
        "layout": "layered",
        "collapsed_by_default": false
      }
    ]
  }
}
```

Required schema rules:

- `presentation` and `groups` are optional.
- Group `id` is a non-empty unique string and must not collide with a graph
  node ID.
- `label` is a non-empty presentation string.
- `members` is a non-empty array of unique existing graph node IDs.
- A node has at most one direct group membership. Overlapping direct
  membership is invalid.
- `parent` is optional and, when present, references another group ID.
- Parent relationships form a bounded acyclic forest. Self-parenting, unknown
  parents, and cycles are invalid.
- `layout` is exactly one of `layered`, `grid`, `fanout`, or `fanin`.
- `collapsed_by_default` is boolean.
- Unknown, malformed, duplicate, overlapping, cyclic, out-of-bound, or
  unknown-member metadata is visibly rejected by the presentation adapter.
- Invalid presentation metadata never changes graph execution, never changes
  node configuration, and never removes the exact Phase 1 raw topology.

Do not add a group-management REST endpoint. Do not make
`GraphCoordinator`, `GraphExecutor`, graph construction, or node plugins
interpret presentation groups. The JSON is retained as ordinary
execution-neutral document metadata and interpreted only by the generic
frontend presentation adapter.

Add execution-isolation tests proving the same graph constructs and executes
identically with valid, absent, and invalid `presentation.groups` metadata.
Invalid presentation metadata must remain a visible dashboard problem while
the graph's execution semantics remain unchanged.

### Generic group presentation model

Extend the Phase 1 display model with domain-neutral groups:

- stable group identity from authored group `id`;
- label, direct members, descendants, parent, depth, layout mode, and
  collapsed state;
- deterministic direct and transitive authoritative node/edge membership;
- validation diagnostics that cite the offending group/member/field; and
- immutable separation between authoritative graph data and local
  presentation state.

Do not infer groups from FHSS node classes, prefixes, frequency indices,
detector counts, DSP configuration, SAR stages, or any domain rule. Explicit
metadata is authoritative. A future generic sibling-inference convenience is
not required in this phase.

### Compound rendering and layout

- Render authored groups as React Flow compound/parent nodes without replacing
  authoritative node or edge identities.
- Support nested groups through the validated parent forest.
- Implement deterministic group-local layout modes:
  - `layered`: port-aware ELK layered layout;
  - `grid`: deterministic row/column placement ordered by stable identity;
  - `fanout`: generic internal topology-degree layout from source-like members
    toward consumers, with deterministic fallback when no unique root exists;
  - `fanin`: generic reverse topology-degree layout from producers toward
    sink-like members, with deterministic fallback when no unique sink exists.
- Keep the global compound layout deterministic for the same graph,
  presentation metadata, bounds, and collapse state.
- Layout remains local presentation state. It must never issue a PATCH,
  mutate graph JSON, change revisions, dirty the executor, or persist positions
  implicitly.
- Invalid or over-bound group layout must show a diagnostic and preserve the
  ungrouped/raw topology rather than yielding a blank page.

### Collapse, expansion, and bundle edges

- Add keyboard- and pointer-operable collapse/expand controls for groups.
- A collapsed group is a presentation node only. It is not a GraphX node.
- Collapsing must retain a sorted, unique authoritative membership list for
  every hidden node and edge.
- Edges crossing a collapsed boundary may be summarized into presentation-only
  bundle edges.
- Bundle IDs must be deterministic from visible endpoints plus the sorted
  authoritative member-edge IDs.
- Every bundle records all and only the authoritative edges it represents.
- Bundle rendering may use explicit presentation boundary handles, but it must
  not invent an authoritative source/target port or serialize the bundle into
  graph JSON.
- Bundle inspection must show its authoritative member count and allow the
  operator to inspect each original edge's exact source node/port and target
  node/port.
- Internal edges hidden by collapse remain represented in the group
  membership/inspector.
- Expanding restores every original node, edge, exact port handle, and
  selection identity with no loss or duplication.
- Repeated collapse/expand cycles are deterministic and idempotent.

### Navigation and selection

Add:

- hierarchy breadcrumbs from root to selected/isolated group;
- isolate-group and return-to-parent/all-topology navigation;
- React Flow overview/minimap;
- explicit grouped and **Raw topology** modes; and
- visible group/member/bundle counts.

Selection rules:

- Selected authoritative node/edge identity remains selected when it becomes
  hidden by a collapsed ancestor.
- The collapsed ancestor indicates that it contains the selection; the
  inspector continues to identify the authoritative selected object.
- Expanding makes the selected authoritative object visible again.
- Selecting a bundle selects the bundle presentation object without replacing
  the last authoritative selection; choosing a member edge selects that exact
  authoritative edge.
- PATCH refresh preserves valid group/collapse/isolate/selection state by
  stable identity and truthfully clears invalidated state.
- Switching to raw mode retains authoritative selection and displays the exact
  Phase 1 topology.
- All collapse, expand, isolate, breadcrumb, raw/grouped, minimap, bundle, and
  selection operations must work with keyboard and pointer input, visible
  focus, and semantic labels.

### Bounds and deterministic failure behavior

Before code changes, the orchestrator must record concrete numeric limits in
the Phase 2 checklist. At minimum bound:

- total groups;
- nesting depth;
- direct members per group;
- total memberships;
- authoritative edges per bundle;
- nodes plus edges submitted to each layout operation;
- total compound-layout work; and
- visible node/edge/bundle detail.

The limits must comfortably exceed current minimal, complex, SAR, and
75-node/137-edge FHSS fixtures while remaining testable.

Exceeding a bound:

- emits a specific visible diagnostic;
- performs no partial grouping;
- preserves raw-topology inspection;
- performs no graph PATCH or execution command;
- does not hang, exhaust memory, or create unbounded browser work; and
- produces deterministic results across repeated loads.

### Generic and FHSS graph metadata

Add an independently authored non-FHSS grouped split/merge fixture that proves:

- generic group metadata;
- at least one nested group;
- grid, fanout, fanin, and layered modes across the fixture set;
- crossing and internal edges;
- collapse bundles with independently specified member-edge IDs; and
- raw-mode equality with its source JSON.

Add the FHSS detector-bank group to:

- `libdsp/config/fhss_phase2_binary_iq_receiver.json`

and to its authoritative generator, if the file is generated:

- `examples/DSP/tools/generate_fhss_phase2_topology.py`

The FHSS document may use an FHSS-meaningful group ID/label because metadata is
authored data. Generic `libgraph` production source must not interpret that
meaning.

The FHSS group must contain exactly the 64 detector nodes. Collapsed
presentation must retain complete membership for:

- the 64 channelizer-to-detector edges; and
- the 64 detector-to-merge edges.

Expanding must display all 64 detectors and all 128 exact bank mappings. The
remaining authoritative graph cardinality stays 75 nodes and 137 edges.

### Phase 0 and Phase 1 preservation

- Keep one initial authoritative graph fetch and one client-side graph model.
- Preserve node table/search, shared node/edge inspector, node-config PATCH
  refresh, execution controls, revision/dirty state, pan/zoom/fit/reset,
  malformed/empty/error states, and keyboard behavior.
- Preserve the configured executor lifecycle and typed command contracts.
- Preserve the 8-active + 16-pending HTTP admission bound, SIGPIPE-safe writes,
  root-sticky static serving, explicit/destructor shutdown, and sanitizer
  regressions.
- Preserve pinned frontend dependencies. Do not upgrade packages merely
  because newer versions exist.
- Keep checked-in self-hosted assets reproducible and identical between source
  and installed trees.
- Keep `graph-cli` deprecated and compatibility-tested.

## Tests and operator deliverables

Add layered validation:

1. **Schema/adapter unit tests**
   - absent and valid metadata;
   - duplicate IDs, node-ID collision, empty/duplicate/unknown members;
   - overlap, unknown parent, self-parent, cycle, and excessive depth;
   - invalid layout/boolean/field shapes;
   - every numeric bound and deterministic raw fallback.
2. **Hierarchy model tests**
   - direct/transitive membership;
   - nested groups and breadcrumbs;
   - deterministic compound layout for all four modes;
   - collapse/expand idempotence;
   - exact bundle membership and IDs;
   - raw-mode source equality; and
   - selection preservation across collapse, expansion, isolation, refresh,
     and mode switches.
3. **Renderer/component tests**
   - compound nodes, minimap, grouped/raw modes, counts, diagnostics,
     inspectors, and no structural mutation;
   - keyboard and pointer collapse/expand/isolate/breadcrumb/bundle/member
     operations; and
   - bounded failure states with no blank page.
4. **Execution-isolation and C++ tests**
   - graph construction/execution is identical with absent, valid, and invalid
     presentation metadata;
   - PATCH preserves top-level presentation metadata;
   - existing API/static/worker/lifecycle tests remain green; and
   - no new topology/group API exists.
5. **Real-browser source and installed tests**
   - non-FHSS nested split/merge grouping;
   - FHSS detector-bank collapsed and expanded;
   - exact bundle/member/node/edge counts;
   - breadcrumbs, isolate, minimap, grouped/raw modes, selection preservation,
     keyboard operations, PATCH refresh, diagnostics, and console errors; and
   - repeated collapse/expand and refresh stability.
6. **Architecture scans**
   - reject domain rules in generic frontend/server code;
   - reject legacy dashboard imports and parallel management components;
   - reject new group/topology routes;
   - reject serialization/PATCH of local layout/collapse state; and
   - preserve one entry point and one static-resource root.

Tests must not use the production hierarchy adapter, bundler, or renderer as
their only oracle. Independently author expected:

- group/member/parent relations;
- authoritative node/edge/port identities;
- bundle member-edge sets;
- collapsed/expanded cardinalities;
- raw-mode equality;
- FHSS 64-member/128-edge bank mappings; and
- invalid/bound diagnostics.

Create:

- `docs/graphx_dashboard_phase2_operator_test.md`

The operator document must start from a fresh clone and cover the native
non-Docker workflow first:

- configure, build, test, and install from first principles;
- launch source-tree and installed `graphx-dashboard`;
- open the grouped non-FHSS split/merge fixture;
- inspect nested groups in grouped and raw modes;
- exercise collapse, expand, breadcrumbs, isolate, minimap, bundle inspection,
  selection preservation, keyboard operation, and PATCH refresh;
- open the FHSS graph and verify 75/137 raw topology, the 64-member detector
  group, and all 128 bank-edge memberships;
- compare displayed counts and sample exact ports with direct source JSON;
- verify invalid/over-bound group diagnostics preserve raw topology and do not
  affect execution;
- record screenshots and browser-console errors; and
- provide expected results and troubleshooting.

Docker is not required. Do not use the legacy FHSS dashboard container or add a
generic Docker image unless separately authorized.

## Frontend and host-tool policy

- Continue using the pinned `libgraph/web` lockfile and compatible installed
  host Node.js/npm.
- Do not impose an obsolete exact Node version.
- Do not create Python/Node virtual environments or use `/private/tmp` or
  related build paths.
- Do not add or upgrade a project dependency unless the existing locked
  React Flow/ELK toolchain cannot implement the requirement. If a host-level
  package is missing, stop and ask the user to install it.
- Use no CDN or runtime network-loaded code.
- Node.js remains build-time only; ordinary C++ builds and installed dashboard
  operation require no Node runtime or package download.

## Implementer report

Report:

- files changed and why;
- audited pre-existing Phase 2 work;
- exact presentation schema and numeric bounds;
- validation/fallback behavior;
- compound-layout and bundle identity/membership design;
- collapse/expand/isolate/raw-mode and selection semantics;
- independently verified generic and FHSS group/member/edge counts;
- generated asset inventory and source/install hashes;
- commands run and exact frontend, C++, browser, sanitizer, focused, and full
  regression results;
- operator screenshot paths; and
- limitations reserved for Phase 3 or later phases.

Build affected C++ in repository C++26 mode with warnings treated as errors.
Use repository-local build/test paths and preserve native non-Docker operation.

## Independent verifier

The verifier must not edit implementation, tests, generated assets,
configuration fixtures, documentation, packages, or plans. Give explicit PASS
or FAIL for every Phase 2 checklist item with file/line and direct test/browser
evidence.

Verify especially:

- grouping is optional presentation metadata and is ignored by execution;
- invalid/overlapping/cyclic/unknown/out-of-bound groups fail visibly while raw
  topology and execution remain available;
- no partial grouping survives validation failure;
- all four layout modes are deterministic and domain-neutral;
- nested group, breadcrumb, isolate, minimap, grouped/raw, and keyboard
  behavior works in a real browser;
- collapsed bundles contain all and only independently expected authoritative
  edges, never invent authoritative ports, and never enter graph JSON;
- expansion restores every exact node, edge, and numeric/named port identity;
- authoritative selection survives collapse/expand/isolate/raw/refresh;
- raw mode exactly equals direct source JSON;
- the non-FHSS fixture proves the same mechanisms as FHSS;
- FHSS metadata alone names detector semantics; generic production code has no
  FHSS/detector/frequency/IQ/message/SAR rule;
- FHSS raw mode remains exactly 75 nodes/137 edges, detector membership is 64,
  and bank-edge membership is exactly 128 with all source/target ports;
- bounds prevent excessive work without blank pages, hangs, partial state,
  requests, or execution effects;
- one graph fetch/model, one entry point, one sticky resource root, exact asset
  parity, and no runtime Node/CDN remain true;
- Phase 0 lifecycle and Phase 1 HTTP worker/SIGPIPE/destructor/static-root
  regressions remain green;
- source and installed browser tests include direct visual inspection and
  screenshots, not DOM assertions alone;
- tests use independent group/bundle/cardinality/port oracles;
- C++26/`-Werror`, frontend clean install/typecheck/tests/build/reproduction,
  sanitizer lifecycle tests, focused tests, full configured CTest, and
  `git diff --check` pass.

## Stop condition

Stop only when:

- the verifier reports no unresolved blocking or high-severity findings;
- every Phase 2 criterion is PASS;
- generic split/merge and FHSS grouped/raw source and installed browser tests
  pass;
- exact collapsed/expanded bundle membership and raw-topology equality pass;
- invalid/bound/execution-isolation tests pass;
- Phase 0/1 regression, HTTP lifecycle, sanitizer, frontend, C++26, full
  configured CTest, asset reproduction/parity, and `git diff --check` pass.

Provide a Phase 2 completion report and wait for authorization. Do not proceed
to Phase 3 automatically.
