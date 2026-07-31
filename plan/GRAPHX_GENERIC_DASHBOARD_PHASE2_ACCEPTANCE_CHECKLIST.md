# GraphX Generic Dashboard Phase 2 Acceptance Checklist

This checklist operationalizes Phase 2 of
`plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md` and
`plan/prompts/GRAPHX_GENERIC_DASHBOARD_PHASE2_ORCHESTRATION.md`.

## Locked hierarchy contract

### Schema

Phase 2 accepts optional execution-neutral metadata:

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

Only the generic frontend presentation adapter interprets this section.
Execution, graph construction, plugins, `GraphCoordinator`, and
`GraphExecutor` do not apply group semantics.

Group rules are:

- non-empty unique `id`, distinct from every node ID;
- non-empty `label`;
- non-empty unique `members`, each naming an existing graph node;
- at most one direct group membership per node;
- optional `parent` naming another group;
- parent relationships form an acyclic forest;
- `layout` is `layered`, `grid`, `fanout`, or `fanin`; and
- `collapsed_by_default` is boolean.

Unknown fields or malformed, duplicate, overlapping, unknown-member,
unknown-parent, self-parent, cyclic, or over-bound metadata invalidate the
entire presentation grouping atomically. Raw Phase 1 topology remains
available and execution remains unaffected.

### Numeric bounds

Centralize and export these limits from the hierarchy adapter:

| Limit | Value |
|---|---:|
| Total groups | 256 |
| Parent depth | 12 |
| Direct members per group | 2,048 |
| Total direct memberships | 10,000 |
| Authoritative member edges per bundle | 10,000 |
| Nodes + edges per layout invocation | 20,000 |
| Cumulative compound-layout work units | 100,000 |
| Visible nodes + edges + bundles | 25,000 |

A boundary violation produces one deterministic diagnostic, applies no
partial grouping, issues no HTTP mutation/execution request, and shows exact
raw topology.

### Bundle and selection semantics

- Map each authoritative edge through the current visible representation of
  its endpoints.
- Edges whose visible endpoints differ are grouped by that visible endpoint
  pair.
- A multi-edge collapsed crossing becomes a presentation bundle whose member
  list is the sorted unique authoritative edge-ID set.
- Bundle identity is a collision-free length-prefixed canonical encoding of
  visible source identity, visible target identity, and sorted member IDs. Do
  not use array position or an unchecked hash.
- An edge whose endpoints map to the same collapsed group is internal and
  remains in that group's authoritative hidden-edge membership, not as a
  self-bundle.
- A bundle has presentation boundary handles only; it has no authoritative
  port and is never serialized or PATCHed.
- Maintain separate authoritative and presentation selection. Collapsing or
  isolating never clears a valid authoritative node/edge selection. The
  containing group indicates hidden selection; expansion/raw mode restores its
  exact visible identity. Selecting a bundle changes presentation selection
  only until an exact member edge is chosen.

## A. Phase 0 and Phase 1 preservation

- [ ] A1. `GraphCoordinator` remains the sole graph-document/revision
  authority.
- [ ] A2. One mandatory lazy executor and the verified
  `ConfigureGraph → Init → Start → Run → Stop → Join` contract remain intact.
- [ ] A3. `GraphHttpServer` still has no executor pointer/direct lifecycle
  calls and no group/topology mutation route is added.
- [ ] A4. One initial `GET /api/v1/graph` and one client-side graph document
  still feed table, canvas, and inspectors.
- [ ] A5. Phase 1 exact node/edge/numeric/named-port identity rules remain
  unchanged in raw mode.
- [ ] A6. Search/table, inspectors, PATCH refresh, execution controls,
  revision/dirty state, pan/zoom/fit/reset, diagnostics, and keyboard behavior
  remain functional.
- [ ] A7. The 8-active + 16-pending HTTP bound, overload rejection,
  SIGPIPE-safe writes, one sticky resource root, and explicit/destructor
  shutdown remain green under sanitizer and stress tests.
- [ ] A8. Pinned self-hosted dependencies and reproducible source/install
  assets remain unchanged unless a documented necessity is independently
  verified.
- [ ] A9. `graph-cli` remains deprecated, built, installed, warned, and tested.
- [ ] A10. No Phase 3 semantic-view work or Phase 4 metrics/command-overlay
  work is introduced.

## B. Schema validation and execution isolation

- [ ] B1. Absent `presentation` or absent `groups` produces the exact Phase 1
  raw display without a warning.
- [ ] B2. Valid flat and nested group metadata produces the locked normalized
  hierarchy model.
- [ ] B3. Group IDs, labels, members, parents, layout values, and boolean
  values are type- and value-validated.
- [ ] B4. Duplicate group IDs and group/node ID collisions invalidate all
  grouping visibly.
- [ ] B5. Empty, duplicate, unknown, and overlapping node memberships
  invalidate all grouping visibly.
- [ ] B6. Unknown parent, self-parent, parent cycle, and excessive depth
  invalidate all grouping visibly.
- [ ] B7. Unknown fields and malformed `presentation`/`groups` structures
  invalidate all grouping visibly.
- [ ] B8. Every numeric bound has below/at/above-bound tests and a stable,
  field-specific diagnostic.
- [ ] B9. No invalid case leaves partial compound nodes, bundles, collapse
  state, or inferred group state.
- [ ] B10. Valid, absent, and invalid presentation metadata produce identical
  graph construction/execution behavior.
- [ ] B11. Node-config PATCH preserves the top-level presentation document and
  does not serialize local collapse/layout/isolation state.
- [ ] B12. Tests prove no group REST endpoint or execution-layer group
  interpretation exists.

## C. Hierarchy model and deterministic layouts

- [ ] C1. Each normalized group records stable ID, label, direct members,
  descendants, parent, children, depth, layout, collapsed default, and exact
  authoritative hidden-node/edge membership.
- [ ] C2. Direct and transitive membership is complete, unique, deterministic,
  and independently tested.
- [ ] C3. Parent forest and root ordering are deterministic by stable identity.
- [ ] C4. `layered` uses deterministic port-aware ELK group-local layout.
- [ ] C5. `grid` uses deterministic identity-ordered row/column placement.
- [ ] C6. `fanout` uses only generic internal topology degree/direction and has
  a deterministic fallback for zero or multiple roots.
- [ ] C7. `fanin` uses only generic reverse topology degree/direction and has a
  deterministic fallback for zero or multiple sinks.
- [ ] C8. Nested group and global compound layout is deterministic across
  repeated loads and collapse states.
- [ ] C9. Per-call, cumulative-work, and visible-detail limits are enforced
  before expensive work and fall back atomically to raw topology.
- [ ] C10. Layout never PATCHes, changes revisions, dirties the executor,
  issues an execution command, or mutates authoritative JSON.

## D. Collapse, expansion, and bundles

- [ ] D1. Pointer and keyboard controls collapse and expand flat and nested
  groups.
- [ ] D2. Collapsed groups are presentation nodes and never appear as GraphX
  nodes or in graph JSON.
- [ ] D3. Collapsing records every hidden authoritative node and internal edge
  exactly once.
- [ ] D4. Crossing edges map to deterministic visible endpoint pairs.
- [ ] D5. Bundle IDs use the locked collision-free canonical encoding.
- [ ] D6. Bundle member lists are sorted, unique, bounded, and contain all and
  only independently expected authoritative edges.
- [ ] D7. Internal edges do not become self-bundles and remain inspectable as
  hidden group membership.
- [ ] D8. Bundle boundary handles are explicitly presentation-only and never
  claim an authoritative port.
- [ ] D9. Bundle inspector shows member count and every original edge's exact
  source node/port kind/value and target node/port kind/value.
- [ ] D10. Selecting a bundle and selecting an exact member edge obey the
  locked separate presentation/authoritative selection semantics.
- [ ] D11. Expanding restores every original node, edge, port handle, and valid
  authoritative selection without loss or duplication.
- [ ] D12. Repeated collapse/expand cycles are deterministic and idempotent.
- [ ] D13. Bundle/member bounds fail atomically to raw topology with a visible
  diagnostic and no unbounded work.

## E. Navigation, raw mode, and selection

- [ ] E1. Breadcrumbs show the exact root-to-current group/isolation path.
- [ ] E2. Isolate, parent, and all-topology navigation works for nested groups.
- [ ] E3. React Flow minimap is present, labeled, and synchronized with the
  visible topology.
- [ ] E4. Grouped and **Raw topology** modes are explicit and keyboard
  operable.
- [ ] E5. Raw mode node/edge/port identities and cardinalities exactly equal
  the authoritative graph document.
- [ ] E6. Group/member/hidden-edge/bundle counts are visible and truthful.
- [ ] E7. A hidden selected authoritative node/edge remains selected in state;
  the collapsed ancestor visibly indicates contained selection.
- [ ] E8. Expansion/raw mode restores the selected authoritative object
  visibly and in the shared inspector.
- [ ] E9. Collapse, expand, isolation, breadcrumb navigation, raw/grouped mode,
  minimap, bundle, member-edge, and selection operations are pointer and
  keyboard operable with visible focus and semantic labels.
- [ ] E10. PATCH refresh preserves valid hierarchy, collapse, isolation, and
  selections by stable identity and clears invalidated state truthfully.
- [ ] E11. Navigation/collapse state remains local presentation state and is
  never included in PATCH or execution requests.

## F. Generic and FHSS fixture evidence

- [ ] F1. An independently authored non-FHSS split/merge fixture contains
  valid flat and nested groups, internal and crossing edges, and independently
  specified group/bundle oracles.
- [ ] F2. Across generic fixtures, all four layout modes are exercised.
- [ ] F3. Generic raw mode exactly equals direct source JSON.
- [ ] F4. Generic collapse/expand produces exact independently expected
  bundle/internal/member cardinalities and port tuples.
- [ ] F5. Invalid, overlapping, cyclic, unknown-member, and over-bound generic
  fixtures show diagnostics while preserving raw topology.
- [ ] F6. FHSS group metadata is authored in
  `libdsp/config/fhss_phase2_binary_iq_receiver.json` and its generator remains
  reproducible.
- [ ] F7. FHSS raw topology remains exactly 75 nodes and 137 edges.
- [ ] F8. FHSS detector group contains exactly the 64 independently identified
  detector nodes.
- [ ] F9. Collapsed FHSS membership contains exactly 64
  channelizer-to-detector plus 64 detector-to-merge authoritative edges.
- [ ] F10. Expanded FHSS restores all 64 detectors and all 128 exact bank edge
  node/port mappings.
- [ ] F11. FHSS-specific names exist only in authored metadata, fixtures,
  tests, and operator evidence; generic production code contains no
  FHSS/detector/frequency/IQ/message/SAR rule.
- [ ] F12. The same group/bundle/navigation implementation is used for generic
  and FHSS fixtures.

## G. Frontend, packaging, and architecture

- [ ] G1. Existing pinned React Flow/ELK dependencies are reused without an
  unnecessary upgrade or new runtime dependency.
- [ ] G2. `index.html` remains the sole entry point and generated assets remain
  contained under the one generic resource root.
- [ ] G3. Clean-lockfile build reproduces checked-in JS/CSS inventory and
  hashes.
- [ ] G4. Source, rebuilt, and installed index/JS/CSS hashes match exactly.
- [ ] G5. A missing installed entry point returns `503 ui_unavailable`;
  missing installed child assets return `404`; no per-file source/install
  fallback returns.
- [ ] G6. Node.js remains build-time only; native C++ build and installed
  dashboard operation require no Node runtime/download.
- [ ] G7. Architecture scans reject legacy dashboard imports, parallel
  management components, new group/topology routes, domain rules, CDNs,
  executor access, and serialization of presentation state.
- [ ] G8. Existing HTTP worker, admission, reset, static-root, active/destructor
  shutdown, and header/body/timeout tests remain green.

## H. Tests and operator evidence

- [ ] H1. Adapter/schema tests cover every valid, absent, invalid, and numeric
  boundary case.
- [ ] H2. Hierarchy/bundle tests use independently authored membership,
  endpoint, identity, cardinality, and diagnostic oracles.
- [ ] H3. Component tests cover compounds, minimap, modes, counts, inspectors,
  selection, refresh, keyboard/pointer controls, bounds, and read-only
  behavior.
- [ ] H4. Execution-isolation tests prove grouping metadata never changes
  construction/execution and PATCH preserves metadata.
- [ ] H5. Source-tree Firefox tests cover generic nested grouping and FHSS
  collapse/expand/raw behavior with screenshots and zero console errors.
- [ ] H6. Installed-tree Firefox tests cover the same critical generic and FHSS
  hierarchy paths and exact asset parity.
- [ ] H7. Browser tests repeat collapse/expand/refresh and prove stable
  identities, counts, selection, breadcrumbs, isolation, minimap, bundles,
  raw equality, and keyboard behavior without arbitrary sleeps.
- [ ] H8. `docs/graphx_dashboard_phase2_operator_test.md` starts from a fresh
  clone and documents native source/install workflows, generic and FHSS exact
  expectations, invalid/bound execution isolation, screenshots, console
  checks, read-only checks, and troubleshooting.
- [ ] H9. Operator instructions do not require Docker or the legacy FHSS
  container.

## I. Validation gates

- [ ] I1. Affected C++ builds in repository C++26 mode with warnings as errors.
- [ ] I2. Clean frontend install, syntax/typecheck, unit/component tests,
  production build, and reproduction checks pass.
- [ ] I3. Focused schema, hierarchy, layout, bundle, component, execution,
  HTTP, worker, architecture, and operator tests pass.
- [ ] I4. Source and installed real-browser generic/FHSS grouped/raw suites
  pass with direct visual inspection.
- [ ] I5. Repository-local ASan/UBSan HTTP lifecycle/static tests remain clean.
- [ ] I6. Full configured CTest passes with only documented hardware/data/local
  gates disabled or skipped.
- [ ] I7. `git diff --check` passes.
- [ ] I8. Independent verifier gives explicit PASS for every item above and
  reports no unresolved blocking or high-severity finding.
