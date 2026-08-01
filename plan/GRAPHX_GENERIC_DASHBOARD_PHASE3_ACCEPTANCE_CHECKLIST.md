# GraphX Generic Dashboard Phase 3 Acceptance Checklist

This checklist operationalizes Phase 3 of
`plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md` and
`plan/prompts/GRAPHX_GENERIC_DASHBOARD_PHASE3_ORCHESTRATION.md`.

## Locked Phase 3 contracts

### Semantic information architecture

The dashboard keeps its two top-level views. The existing **Nodes &
parameters** view becomes **Semantic topology** and remains sourced from the
same `DisplayGraph`, `DisplayHierarchy`, and shared selection state as the
canvas. It contains, in order:

1. one labelled semantic-topology region and heading;
2. one generic search control plus the existing node-type filter and truthful
   visible/total result counts;
3. a native nested hierarchy/list for authored groups and authoritative nodes,
   with an **Ungrouped nodes** branch when required;
4. an authoritative edge table containing every edge exactly once with edge
   identity, source node, exact source port kind/value, target node, exact
   target port kind/value, and selection action; and
5. the existing shared inspector/editor beside the active view.

The nested hierarchy uses native list, heading, disclosure, and button
semantics, not an incomplete ARIA `tree`/`treegrid`. Standard browser keyboard
semantics therefore apply: Tab/Shift+Tab traverse interactive elements,
Enter/Space activate buttons and disclosure, and Home/End remain native where
supported. No positive `tabindex` is allowed. Semantic disclosure is separate
from canvas collapse so a canvas-collapsed member remains reachable. Explicit
semantic group actions may still invoke the existing canvas collapse/isolate
operations.

Authoritative node identities occur once as primary node records in the
semantic hierarchy. Authoritative edge identities occur once as primary edge
rows. Group summaries may name/count members, and bundle inspection may link
to exact member edges, but must not create a second authoritative record.
Absent or invalid hierarchy produces a deterministic flat authoritative node
list and edge table with the existing diagnostic.

Ordering uses the Phase 2 code-unit identity comparator. Groups follow the
validated parent forest and stable sibling ordering; direct nodes follow their
group in stable identity order; ungrouped nodes are stable-identity ordered;
edges are stable-identity ordered. Search never changes identity or source
order and clearing it restores the complete inventory.

### Selection, focus, and announcements

- Canvas, semantic hierarchy, edge table, search results, bundle members, and
  inspector share the existing authoritative/presentation selection model.
- Selected semantic records expose text plus programmatic selected state.
- When a selected object survives refresh, its identity and inspector survive.
  If the focused semantic control survives, focus returns to that exact stable
  identity after refresh.
- If refresh removes the selected object, selection clears truthfully, a
  polite status announces the removal, and focus moves to the semantic heading
  when the semantic view is active or the active view heading otherwise.
- Opening the parameter editor records the invoking control. The modal receives
  initial focus, contains Tab/Shift+Tab, closes on Escape without saving, and
  restores focus to the invoking control (or the semantic heading if removed).
- Fetch, PATCH, preference fallback/reset, selection invalidation, and command
  outcomes use concise status/alert semantics without duplicate live regions.
- Switching view, mode, disclosure, collapse, isolation, or selection does not
  unexpectedly move focus unless the focused object is removed from the DOM.

### Keyboard and navigation

- A visible-on-focus skip link moves directly to the dashboard view controls
  or active primary region.
- Tab order is: skip link, execution controls, view controls, active-view
  controls/content, inspector, footer/modal as applicable.
- Enter/Space activate selection, group, breadcrumb, mode, minimap, bundle,
  edit, reset-preference, and lifecycle controls.
- Existing minimap arrow/plus/minus operation remains intact.
- Escape closes the parameter modal and any explicitly documented transient
  control; it does not discard edits merely because focus is elsewhere or
  issue execution/network operations.
- No positive `tabindex`, keyboard trap, hidden focus, or focus-dependent
  context change is introduced.

### Local preference schema

Use one browser-local record under the key:

`graphx.dashboard.presentation`

The locked JSON shape is:

```json
{
  "schema": 1,
  "graph_signature": "sha256:<lowercase-hex>",
  "mode": "grouped",
  "collapsed_group_ids": ["group-id"],
  "semantic_expanded_group_ids": ["group-id"],
  "viewport": {"x": 0, "y": 0, "zoom": 1}
}
```

- Compute `graph_signature` with built-in Web Crypto SHA-256 over a canonical,
  length-prefixed, code-unit-sorted sequence of authoritative node IDs,
  authoritative full-tuple edge IDs, and valid authored group IDs. Mutable
  `node_config` values are excluded.
- Store at most one graph record, at most 256 collapsed IDs, at most 256
  semantic disclosure IDs, and at most 64 KiB of serialized JSON.
- Accept only `schema === 1`, the exact signature, known mode, known unique
  group IDs, finite viewport values, `x/y` in `[-10000000, 10000000]`, and
  `zoom` in `[0.1, 4]`.
- Invalid, stale, over-bound, unsupported, unavailable-Web-Crypto,
  unavailable-storage, parse, write, or quota failure uses deterministic graph
  defaults and one concise non-fatal status. It never blocks graph inspection.
- Persist only after graph/signature validation. Coalesce viewport writes and
  persist the final React Flow viewport after an operator move/zoom.
- **Reset view preferences** removes the record and immediately restores the
  graph's deterministic grouped/raw default, authored collapsed defaults,
  semantic disclosure defaults, and deterministic fit/reset viewport.
- No preference enters graph JSON, PATCH bodies, lifecycle requests, export,
  cookies, HTTP state, or a new endpoint. View tab, inspector selection,
  isolated group, search text, editor contents, and execution state are not
  persisted.
- Honor only system `prefers-reduced-motion`; do not add an explicit motion
  override in Phase 3.

### Reflow and focused accessibility scope

- All reflowable management and semantic content works at 320 CSS pixels and
  200% browser zoom without page-level horizontal scrolling or hidden
  information. The two-dimensional canvas may use contained two-axis pan/zoom.
- Stable identities and ports wrap or use a labelled, keyboard-operable
  contained overflow region while preserving the complete value.
- Text-spacing overrides of line-height 1.5, paragraph spacing 2x, letter
  spacing 0.12em, and word spacing 0.16em do not clip or hide controls/content.
- Target controls introduced or materially changed in Phase 3 are at least 24
  by 24 CSS pixels or satisfy the WCAG 2.2 spacing/inline exception.
- Focus indicators have a non-color outline and are not obscured by sticky or
  overlapping content.
- State is exposed in text/programmatic attributes and never solely by color,
  animation, position, or canvas geometry.
- `prefers-reduced-motion: reduce` suppresses dashboard-controlled animation
  and smooth scrolling without removing information.
- Evidence maps the changed operator paths to WCAG 2.2 AA 1.3.1, 1.3.2,
  1.4.1, 1.4.3, 1.4.10, 1.4.11, 1.4.12, 2.1.1, 2.1.2, 2.4.3, 2.4.7,
  2.4.11, 2.5.3, 2.5.8, 3.2.1, 3.2.2, applicable 3.3.1-3.3.3, 4.1.2, and
  4.1.3. This is scoped evidence, not formal certification.

## A. Phase 0-2 architectural preservation

- [ ] A1. `GraphCoordinator` remains the sole graph-document/revision
  authority and `GET /api/v1/graph` remains the sole topology source.
- [ ] A2. One mandatory lazy executor and
  `ConfigureGraph -> Init -> Start -> Run -> Stop -> Join` remain intact.
- [ ] A3. `GraphHttpServer` has no executor pointer/direct lifecycle calls and
  no new API, preference route, hierarchy route, or topology mutation exists.
- [ ] A4. One fetched graph/model feeds canvas, semantic view, hierarchy,
  search, and inspectors.
- [ ] A5. Exact node, edge, numeric/named-port, full-tuple identity, raw mode,
  and malformed-input behavior remain unchanged.
- [ ] A6. Phase 2 groups, layouts, bundles, minimap, collapse, isolation,
  breadcrumbs, bounds, execution isolation, and selection remain functional.
- [ ] A7. Node PATCH and revision/dirty behavior remain authoritative;
  presentation operations never PATCH or execute.
- [ ] A8. HTTP 8-active/16-pending bounds, SIGPIPE-safe writes, sticky root,
  containment, and shutdown regressions remain green.
- [ ] A9. One `index.html`, one generic asset root, pinned self-hosted
  dependencies, and source/install parity remain intact.
- [ ] A10. `graph-cli` remains deprecated, built, installed, warned, and
  tested; no Phase 4 metrics/console work or domain-specific dashboard rule is
  introduced.

## B. Semantic topology fidelity

- [ ] B1. The existing Nodes view is renamed/reworked as **Semantic topology**
  without adding another top-level graph authority or fetch.
- [ ] B2. The labelled semantic region contains the locked search, hierarchy,
  authoritative edge table, visible/total counts, and shared inspector.
- [ ] B3. Every authoritative node appears exactly once as a primary semantic
  node record with exact ID, type, input/output ports, group path, and edit/
  selection controls.
- [ ] B4. Every authoritative edge appears exactly once with exact stable ID,
  endpoint node IDs, and source/target port kind and value.
- [ ] B5. Valid flat/nested groups use the exact Phase 2 forest, direct/
  transitive membership, counts, warnings, and stable ordering.
- [ ] B6. Ungrouped nodes are represented once in a deterministic branch.
- [ ] B7. Canvas collapse/isolation/layout failure does not make an
  authoritative semantic node or edge unreachable.
- [ ] B8. Absent/invalid/over-bound hierarchy and malformed canvas input retain
  a truthful bounded semantic/raw fallback and associated diagnostics.
- [ ] B9. Search/type filtering is generic, deterministic, reports visible/
  total counts, and restores exact inventory when cleared.
- [ ] B10. Minimal 2/1, generic nested 4/3, complex 9/9, SAR 21/23, and FHSS
  75/137 semantic inventories match independent source-data oracles.

## C. Shared selection and inspector behavior

- [ ] C1. Selecting a semantic node updates canvas/search/shared inspector and
  retains exact authoritative identity.
- [ ] C2. Selecting a semantic edge updates canvas/shared inspector and shows
  exact endpoint ports.
- [ ] C3. Canvas and search selection update the corresponding semantic record
  without creating duplicate state.
- [ ] C4. Group and bundle selection remain presentation-only until an exact
  authoritative member is chosen.
- [ ] C5. Hidden authoritative selection remains selected, is indicated by its
  containing group, and remains reachable semantically.
- [ ] C6. Raw/grouped mode, semantic disclosure, collapse/expand, isolation,
  search, and view switching preserve valid authoritative selection.
- [ ] C7. Successful PATCH refresh preserves surviving selection and inspector
  identity; removed selection clears and announces truthfully.
- [ ] C8. Semantic/group/bundle actions issue no unexpected PATCH, execution,
  group, or topology request.

## D. Keyboard and focus

- [ ] D1. Skip navigation, region labels, and logical Tab/Shift+Tab order work
  without a keyboard trap.
- [ ] D2. Semantic disclosure, node/edge selection, edit, group actions,
  search, reset preferences, and inspector controls operate with native
  keyboard semantics.
- [ ] D3. Existing canvas, minimap, breadcrumbs, mode, collapse, isolate,
  bundle/member, and lifecycle keyboard behavior remains green.
- [ ] D4. No positive `tabindex`, hidden focus, or focus-only context change is
  introduced.
- [ ] D5. Surviving focused semantic identity is restored after graph refresh;
  removed identity focuses the locked active-view heading fallback.
- [ ] D6. Mode/view/disclosure/collapse/isolate changes retain focus unless
  their focused control is removed, then use a deterministic fallback.
- [ ] D7. Parameter editor receives initial focus, traps Tab/Shift+Tab, closes
  safely with Escape, and restores the invoking focus.
- [ ] D8. Focus indicators are visible, non-color, unobscured, and remain so at
  narrow width and 200% zoom.
- [ ] D9. The exact keyboard table is covered by component/browser tests and
  documented for operators.

## E. Screen-reader, status, and non-color semantics

- [ ] E1. Major regions have unique accessible names and a meaningful order.
- [ ] E2. Node, edge, group, bundle, warning, mode, count, refresh, edit,
  preference, and lifecycle controls expose truthful name/role/value/state.
- [ ] E3. Exact stable IDs and port values remain available through visible
  text or associated accessible descriptions.
- [ ] E4. Selected, collapsed/expanded, contains-selection, invalid, dirty,
  execution, success, and failure states are not color/geometry-only.
- [ ] E5. Fetch, PATCH, preference fallback/reset, selection removal, and
  command outcomes use concise correctly scoped status/alert announcements.
- [ ] E6. Canvas accessibility does not duplicate the full semantic topology
  or create contradictory editing semantics.
- [ ] E7. Duplicate landmark names, duplicate DOM IDs, broken label/name
  relationships, and unsupported ARIA patterns are rejected by tests/review.
- [ ] E8. Reduced-motion mode removes controlled animation/smooth scrolling
  without removing state or operation.

## F. Local presentation preferences

- [ ] F1. The exact locked key/schema and canonical Web-Crypto graph signature
  are implemented without adding a package.
- [ ] F2. Signature includes sorted authoritative node/full-edge/group IDs and
  excludes mutable configuration.
- [ ] F3. Valid mode, collapse, semantic disclosure, and viewport restore only
  for an exact graph signature.
- [ ] F4. Type, uniqueness, group membership, numeric range, count, schema,
  signature, and 64-KiB bounds are checked before state application.
- [ ] F5. Malformed, stale, over-bound, unsupported, unavailable, and quota/
  write failures fall back non-fatally with one concise status.
- [ ] F6. Viewport writes are coalesced and final move/zoom state restores
  without causing graph layout or request churn.
- [ ] F7. Refresh drops only invalidated group IDs while retaining valid local
  state for the same structural signature.
- [ ] F8. Reset removes storage and immediately restores deterministic graph
  defaults, authored collapse, semantic disclosure, and viewport.
- [ ] F9. Preferences never enter graph JSON, PATCH, command, export, cookie,
  HTTP/server state, or a new route; non-locked transient state is not stored.
- [ ] F10. System reduced-motion is honored; no unnecessary explicit motion
  preference or server synchronization is introduced.

## G. Reflow and focused WCAG 2.2 AA evidence

- [ ] G1. Management, semantic view, search, inspector/editor, diagnostics,
  counts, revision/dirty state, and execution controls retain all information
  and operation at 320 CSS pixels.
- [ ] G2. The same reflowable content works at 200% browser zoom without
  page-level horizontal scrolling; only the contained 2-D canvas pans.
- [ ] G3. Locked text-spacing overrides do not clip, overlap, or hide content
  or controls.
- [ ] G4. Complete long IDs/ports remain available through wrapping or a
  labelled contained keyboard-operable overflow region.
- [ ] G5. Changed controls satisfy the 24-by-24 target or documented WCAG
  exception, and focus is not obscured.
- [ ] G6. Color/contrast/non-text-contrast evidence covers normal, selected,
  warning, dirty, status, and focus states; color is never the sole cue.
- [ ] G7. Minimal, generic nested, SAR, and FHSS graphs retain semantic access
  at desktop and narrow layouts within Phase 2 work/detail bounds.
- [ ] G8. The checklist/report maps direct automated, DOM, keyboard, visual,
  and human evidence to every locked applicable WCAG criterion without a
  certification claim.

## H. Tests and independent evidence

- [ ] H1. Semantic model tests use independently authored node/group/edge/
  port/order/count/warning oracles rather than production output alone.
- [ ] H2. Component tests cover semantic fidelity, search, counts, selection,
  inspector synchronization, refresh, invalid fallback, and request isolation.
- [ ] H3. Keyboard/focus/modal tests cover the entire locked interaction table
  and deterministic focus restoration without arbitrary sleeps.
- [ ] H4. Preference tests cover encode/decode/signature, every validation
  bound/failure, graph changes, refresh, viewport coalescing, and reset.
- [ ] H5. Accessibility tests cover landmarks, labels, names, roles, states,
  status, duplicate IDs, non-color text, reduced motion, and text spacing.
- [ ] H6. Source-tree real-browser tests cover generic/SAR/FHSS semantic,
  keyboard, focus, persistence, 320-pixel, 200%-zoom, and reduced-motion paths
  with screenshots and zero unexpected console errors.
- [ ] H7. Installed-tree browser tests cover the same critical semantic and
  accessibility paths with exact asset parity.
- [ ] H8. Browser request evidence proves local interactions do not PATCH,
  execute, or call a new endpoint.
- [ ] H9. `docs/graphx_dashboard_phase3_operator_test.md` begins from a fresh
  clone and provides objective native source/install, keyboard, focus,
  non-color, motion, preference, reflow, zoom, text-spacing, screenshot,
  network, console, accessibility-tree, and troubleshooting expectations.
- [ ] H10. Operator instructions preserve non-Docker operation, use no legacy
  FHSS container, describe synthetic/recorded-only data, and make no formal
  WCAG/HWIL/live-RF claim.

## I. Packaging, architecture, and validation gates

- [ ] I1. Existing pinned runtime dependencies are reused; any necessary
  development-only accessibility dependency has recorded justification.
- [ ] I2. One entry point/resource root and exact source/rebuilt/installed
  index/CSS/JS inventories and hashes remain equal.
- [ ] I3. Architecture scans reject parallel authorities, new APIs, legacy
  dashboard imports, domain rules, runtime CDN/Node, server preferences,
  Phase 4 metrics/console work, and serialized local presentation state.
- [ ] I4. Affected C++ builds in repository C++26 mode with warnings as errors.
- [ ] I5. Compatible host Node/npm clean install, syntax/typecheck, all
  frontend tests, production build, and reproducible generated assets pass.
- [ ] I6. Focused semantic/accessibility/browser/HTTP/execution-isolation tests
  pass in source and installed forms.
- [ ] I7. Supported Docker ASan/LeakSanitizer/UBSan focused lane passes using
  named volumes and documented native architecture/default parallelism.
- [ ] I8. Full enabled native configured CTest passes with only documented
  hardware/data/local gates disabled or skipped.
- [ ] I9. `git diff --check` passes and unrelated working-tree changes remain
  preserved.
- [ ] I10. Independent verifier gives explicit PASS for every item above with
  no unresolved blocking or high-severity finding.
