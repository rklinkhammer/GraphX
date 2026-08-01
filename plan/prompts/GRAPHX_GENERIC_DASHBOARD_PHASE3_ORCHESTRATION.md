# GraphX Generic Dashboard Phase 3 Orchestration

Implement Phase 3 of:

- `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`

Use these as normative supporting descriptions:

- `docs/graphx_dashboard.md`
- `plan/Phase2B_Generic_Graph_Management.md`
- `plan/Phase2B_Corrected_Implementation_Specification.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE0_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE1_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE2_ACCEPTANCE_CHECKLIST.md`
- `docs/graphx_dashboard_phase1_operator_test.md`
- `docs/graphx_dashboard_phase2_operator_test.md`
- `containers/sanitizers/README.md`

Use WCAG 2.2 Level AA as the focused accessibility design and verification
target for the operator paths changed in this phase. This is not a claim of
formal WCAG certification or complete conformance for the entire product.
Automated checks supplement, but do not replace, the required human keyboard,
screen-reader-oriented semantics, focus, zoom, and reflow review.

Phases 0, 1, and 2 are complete and verified. Treat their architecture,
lifecycle, topology identity, hierarchy, packaging, HTTP, and execution
isolation contracts as the baseline. Do not redesign or bypass them while
adding the semantic topology alternative and operator-usability behavior.

## Verified Phase 0-2 baseline

The Phase 3 implementation starts from these verified facts:

- GraphX has one graph-management implementation and one generic dashboard.
- `GraphCoordinator` remains the sole graph-document and revision authority.
- `GraphHttpServer` remains the HTTP adapter. It has no `GraphExecutor`
  pointer and makes no direct executor lifecycle calls.
- `graphx-dashboard` owns one mandatory, configured, lazy `GraphExecutor`.
- The lifecycle remains
  `ConfigureGraph -> Init -> Start -> Run -> Stop -> Join`.
- `GET /api/v1/graph` is the sole authoritative topology input. One fetched
  client-side graph document feeds the canvas, existing table/search,
  hierarchy, and inspectors.
- Node edits continue through `PATCH /api/v1/nodes/{id}` and truthfully update
  revision/dirty state. Presentation interactions do not issue PATCH or
  execution requests.
- `libgraph/resources/web/index.html` is the sole dashboard entry point.
- The pinned React Flow/ELK/React TypeScript frontend is maintained under
  `libgraph/web`; generated assets are reproducible, self-hosted, and served
  from one sticky generic resource root.
- Raw topology preserves every authoritative node, edge, numeric or named
  port, and deterministic full-tuple edge identity.
- Optional `presentation.groups` metadata is interpreted only by the generic
  frontend presentation adapter. It is ignored by graph construction and
  execution.
- Generic nested groups, deterministic group layouts, collapse/expand,
  breadcrumbs, isolation, minimap, raw/grouped modes, authoritative hidden
  membership, and presentation-only bundles are implemented.
- Collapsing and expanding preserve exact authoritative selection and restore
  exact nodes, edges, and ports.
- The independently authored generic split/merge fixture and the FHSS fixture
  use the same generic implementation. FHSS raw topology remains 75 nodes and
  137 edges; its authored detector group contains 64 nodes and 128 exact bank
  edges. Generic production dashboard code contains no FHSS rule.
- Invalid or over-bound presentation metadata fails atomically to an exact
  semantic/raw fallback without changing execution.
- `SimpleHttpServer` retains bounded joinable workers, rejects overload,
  survives reset writes without SIGPIPE, selects one static root, and shuts
  down safely.
- `graph-cli` remains deprecated but built, installed, warned, and tested.
- The corrected Phase 2 tree passed:
  - 95/95 frontend tests, TypeScript typecheck, and asset reproduction;
  - the Docker ASan/LeakSanitizer/UBSan focused lane, 9/9 tests, including all
    1,080 `libgraph` tests, DSP examples, Phase 2A configuration, and Phase 2
    characterization;
  - 41/41 focused native ownership/lifecycle tests;
  - all 37 enabled native configured CTests, with only documented local/data
    gates disabled; and
  - `git diff --check`.

Preserve those behaviors and their regression coverage.

## GraphX maturity and validation policy

GraphX is an engineering and research platform. Prioritize architecture,
correctness, determinism, maintainability, performance, and truthful operator
behavior. Apply validation proportionally to Phase 3 risks: semantic fidelity,
keyboard operation, focus, non-color state, reduced motion, local-state
isolation, reflow, packaging, and regression safety.

Do not expand this phase into speculative authentication, authorization, TLS,
network isolation, browser sandboxing, CSP, hostile-Internet hardening, or
general server security work. Security hardening is a later maturity phase
unless the normative plan specifically requires it.

There is no hardware-in-the-loop validation. All example and FHSS data is
synthetic or checked-in recorded test data. Do not claim live RF, HWIL, OTA,
conducted RF, or hardware qualification.

## Agent model assignments

- **Orchestrator:** `gpt-5.6-sol`, `max` reasoning. This role owns phase scope,
  the semantic-view contract, focus and keyboard behavior, local-preference
  isolation, WCAG applicability, acceptance decisions, and agent routing.
- **Implementer:** `gpt-5.6-sol`, `xhigh` reasoning. This role audits and
  implements the semantic topology view, synchronized interaction, accessible
  status/focus/motion behavior, local preferences, responsive reflow, tests,
  generated assets, and operator documentation.
- **Verifier:** `gpt-5.6-sol`, `ultra` reasoning with independent context. This
  role performs adversarial semantic-fidelity, keyboard, focus, non-color,
  motion, reflow, persistence-isolation, browser, packaging, sanitizer, and
  regression review. It must not edit implementation files.

Do not substitute a faster general-purpose model for the verifier. Accessibility
and semantic fidelity require independent DOM, keyboard, visual, source-data,
and request-level evidence rather than screenshots or automated rules alone.

## Orchestrator

1. Audit the current Phase 3-related behavior before assigning work. In
   particular inspect the existing node/edge table, search, inspectors,
   selection model, hierarchy projection, keyboard controls, focus styles,
   reduced-motion CSS, refresh behavior, responsive styles, and any local
   storage use. Do not recreate completed work.
2. Convert every Phase 3 requirement and every preserved Phase 0-2 invariant
   into a file-level acceptance checklist at:
   `plan/GRAPHX_GENERIC_DASHBOARD_PHASE3_ACCEPTANCE_CHECKLIST.md`.
3. Before implementation, lock and record in that checklist:
   - the semantic topology information architecture;
   - exact node, edge, port, group, warning, and selection representations;
   - keyboard interaction and focus-restoration rules;
   - screen-reader names, descriptions, status announcements, and non-color
     equivalents;
   - reduced-motion behavior;
   - the local presentation-preference schema, bounds, graph scoping,
     migration/fallback behavior, and storage failure behavior;
   - narrow-viewport and zoom/reflow expectations; and
   - focused WCAG 2.2 AA success criteria applicable to the changed paths.
4. Preserve all unrelated worktree changes.
5. Assign all implementation to one implementer agent.
6. After implementation and focused tests, assign independent verification to
   one verifier agent with fresh context.
7. Route every verifier finding, including medium findings that contradict a
   Phase 3 acceptance criterion, back to the implementer.
8. Repeat implementation and independent verification until every Phase 3
   criterion is PASS and no blocking or high-severity finding remains.
9. Do not commit, push, open a PR, proceed to Phase 4, remove `graph-cli`, add
   metrics subscription or runtime overlays, add a command console, add a new
   API, or introduce another dashboard architecture.

## Required Phase 3 implementation

### Semantic topology alternative

Convert the existing node table into a genuine semantic topology alternative
that can be used without the canvas. Reuse the same authoritative graph model,
hierarchy adapter, projections, warnings, and selection state; do not create a
second client-side topology authority.

The semantic surface must provide:

- a domain-neutral hierarchy/tree or tree-table for authored groups and
  authoritative nodes;
- an authoritative edge table containing every edge exactly once;
- exact source node, source port kind/value, target node, and target port
  kind/value for every edge;
- node type and direct/transitive group context where applicable;
- group direct-member, descendant, hidden-node, hidden-edge, and bundle counts;
- structural warnings and presentation-validation diagnostics associated with
  the affected semantic object when possible;
- deterministic ordering by stable authoritative identity, with an explicitly
  documented secondary ordering where hierarchy requires it; and
- a truthful semantic fallback when canvas layout is unavailable, invalid, or
  over-bound.

Every authoritative node and edge must remain discoverable and inspectable in
the semantic surface regardless of canvas collapse, isolation, layout failure,
or raw/grouped mode. Canvas collapse may change semantic group disclosure, but
must not make authoritative members unreachable.

Use native HTML semantics wherever practical. If an ARIA tree/treegrid pattern
is used, implement its keyboard and relationship contract completely for the
supported operations; do not add ARIA roles that contradict the native
elements or suggest unsupported editing.

### One synchronized selection and inspection model

- Canvas, semantic node/group rows, edge rows, bundle/member inspection,
  search results, and inspectors use the existing shared selection model.
- Selecting an authoritative object in any surface updates every other visible
  surface and the shared inspector without changing authoritative identity.
- A presentation bundle remains separate from the last authoritative
  selection until the operator chooses an exact member edge.
- A hidden authoritative selection remains selected and is represented by its
  containing group while collapsed. Its semantic row remains reachable.
- Raw/grouped mode, collapse/expand, isolate/breadcrumb navigation, search,
  and successful PATCH refresh preserve selection by stable identity.
- If refresh removes the selected object, clear selection truthfully, announce
  the change, and move focus to the nearest deterministic surviving semantic
  object or the semantic-region heading. Never leave focus on a detached DOM
  element.
- Selection, focus, disclosure, and inspection must not issue topology PATCH,
  execution, or new group-management requests.

### Keyboard and focus contract

Provide a coherent, documented keyboard model across the dashboard:

- `Tab` and `Shift+Tab` move among major regions and controls in a stable
  logical order without a keyboard trap.
- Arrow keys navigate within a semantic tree/treegrid when that pattern is
  used; `Home`/`End` move to the first/last applicable item.
- `Enter` or `Space` selects and activates the focused semantic operation.
- Group disclosure, collapse/expand, isolate, breadcrumbs, raw/grouped mode,
  minimap, bundle/member inspection, search, node editing, and existing
  lifecycle controls remain keyboard operable.
- `Escape` closes or clears only the currently documented transient state; it
  must not unexpectedly discard node edits or issue execution commands.
- Use stable identity to retain focus after refresh, mode changes, disclosure,
  collapse/expand, and local preference restoration.
- Provide visible `:focus-visible` treatment with sufficient non-color shape
  or outline indication. Do not use positive `tabindex` values.
- Add a skip/navigation mechanism if the final region order would otherwise
  require traversing a large canvas or semantic table to reach core controls.

Record the exact keyboard table in both tests and operator documentation.

### Screen-reader and non-color behavior

- Give every major region a unique accessible name.
- Give node, edge, group, bundle, warning, mode, count, refresh, and lifecycle
  controls concise names and, where necessary, descriptions that include exact
  identity and state.
- Expose expanded/collapsed, selected, hidden-selection, invalid, dirty,
  running, completed, and failed states through text or programmatic state;
  color, animation, position, and canvas geometry must never be the only
  representation.
- Use appropriately scoped status/live regions for asynchronous fetch, PATCH,
  selection invalidation, preference fallback, and execution results. Avoid
  duplicate or excessively chatty announcements.
- Preserve existing exact identifiers and port information in accessible
  names or associated descriptions; do not replace them with visual-only
  labels.
- Decorative canvas details must not duplicate the entire topology in the
  accessibility tree when the semantic alternative already provides it.

### Reduced motion and visual clarity

- Honor `prefers-reduced-motion: reduce` for layout transitions, animated
  selection, scrolling, status changes, and any React Flow motion controlled
  by the dashboard.
- Provide a local motion preference only if it adds a clear operator benefit;
  if provided, use `system`, `reduced`, and `full` behavior and persist it only
  as local presentation state.
- Motion is never required to understand state. Provide text and static visual
  equivalents.
- Preserve meaningful focus, selection, warning, dirty, and lifecycle state in
  forced/no-color or grayscale-oriented manual review.

### Local presentation preferences

Persist only local presentation preferences needed by this phase:

- layout choice where operator-selectable;
- canvas zoom and viewport;
- raw/grouped mode;
- group disclosure/collapse state; and
- optional motion preference if implemented.

The orchestrator must define one bounded, documented local preference schema.
Requirements:

- preferences live only in browser-local storage and are never inserted into
  graph JSON, PATCH bodies, command requests, exports, or server state;
- scope graph-specific identities to a deterministic signature derived from
  authoritative structural identities, not mutable node configuration values;
- validate decoded types, finite numeric values, known modes, known group IDs,
  and reasonable size/count bounds before applying state;
- unknown, malformed, stale, unsupported, unavailable, or quota-failed local
  storage falls back visibly but non-fatally to deterministic defaults;
- refresh retains valid preferences and drops only invalidated graph-specific
  identities;
- provide an explicit **Reset view preferences** operation; and
- do not add a server preference API, cookie, account model, or background
  synchronization.

Preference validation is correctness and maintainability work, not a request
for unrelated browser-security hardening.

### Narrow viewport, zoom, and reflow

- The management header, semantic topology, search, inspectors, forms,
  diagnostics, and controls must reflow without loss of information or
  operation at a 320 CSS-pixel content width and at 200% browser zoom.
- The two-dimensional topology canvas may retain intrinsic two-axis pan/zoom,
  but it must not prevent access to the reflowing semantic alternative.
- Do not hide authoritative nodes, edges, exact ports, warnings, selection,
  revision/dirty state, or execution controls solely because the viewport is
  narrow.
- Avoid page-level horizontal scrolling for reflowable content. Long stable
  identities and ports may wrap or use a contained, keyboard-operable region
  with the full value still available.
- Inspector/editor actions must remain adjacent to their labels and retain
  meaningful focus order after reflow.
- Test representative minimal, generic nested, SAR, and FHSS graphs at desktop
  and narrow widths. Large-graph semantic access must remain bounded and
  responsive under the Phase 2 limits.

### Focused WCAG 2.2 AA scope

The acceptance checklist must map direct evidence for the success criteria
materially affected by Phase 3, including at least:

- 1.3.1 Info and Relationships;
- 1.3.2 Meaningful Sequence;
- 1.4.1 Use of Color;
- 1.4.3 Contrast (Minimum);
- 1.4.10 Reflow;
- 1.4.11 Non-text Contrast;
- 1.4.12 Text Spacing;
- 2.1.1 Keyboard and 2.1.2 No Keyboard Trap;
- 2.4.3 Focus Order;
- 2.4.7 Focus Visible and 2.4.11 Focus Not Obscured (Minimum);
- 2.5.3 Label in Name;
- 2.5.8 Target Size (Minimum);
- 3.2.1 On Focus and 3.2.2 On Input;
- 3.3.1 Error Identification, 3.3.2 Labels or Instructions, and 3.3.3 Error
  Suggestion where node editing applies; and
- 4.1.2 Name, Role, Value and 4.1.3 Status Messages.

Do not claim that automated tooling proves WCAG conformance. Record automated
rule results, direct DOM/keyboard evidence, and the human review separately.

## Required tests and independent oracles

Tests must not use the production semantic adapter or renderer as their only
oracle. Independently author expected node IDs, group paths, edge endpoint/port
tuples, deterministic order, warnings, selection transitions, and focus
targets for small generic fixtures. Use direct source JSON for SAR and FHSS
cardinality/identity comparison.

Add or extend:

- semantic-model tests proving every authoritative node and edge appears once
  with exact identities, ports, group paths, counts, warnings, and ordering;
- component tests for synchronized selection across canvas, semantic view,
  search, bundles/member edges, and inspectors;
- keyboard tests for region traversal, semantic navigation, group operations,
  modes, search, inspector/edit flows, preference reset, and absence of traps;
- focus tests for fetch, PATCH refresh, object removal, collapse/expand,
  isolation, mode changes, errors, and preference restoration;
- accessibility tests for names, roles, values, relationships, labels,
  descriptions, status announcements, non-color text, and duplicate landmark
  or ID defects;
- reduced-motion tests at both system preference values and any explicit local
  override;
- local-preference encode/decode, graph scoping, type/range/bound validation,
  stale identity, reset, unavailable storage, and quota failure tests;
- responsive component and real-browser tests at desktop, 320 CSS-pixel
  content width, 200% zoom, and text-spacing overrides;
- execution-isolation/request-spy tests proving semantic, keyboard, focus,
  layout, zoom, collapse, and preference interactions issue no PATCH or
  execution request;
- source-tree and installed-tree real-browser tests with screenshots and zero
  unexpected console errors; and
- architecture scans proving no second graph authority, new API, legacy FHSS
  dashboard import, domain-specific generic code, CDN/runtime dependency,
  preference server path, Phase 4 metric overlay, or command console exists.

Use a focused automated accessibility checker only if it is already available
or the orchestrator independently demonstrates that a pinned development-only
dependency is necessary. Do not add or upgrade a runtime dependency merely to
obtain a score. The verifier must still inspect representative DOM semantics
and execute the human keyboard/reflow procedure.

## Operator evidence

Create:

- `docs/graphx_dashboard_phase3_operator_test.md`

The operator document must begin with a fresh clone and a build from first
principles. Document the ordinary native, non-Docker workflow first:

- clone/download the repository into an operator-owned workspace;
- confirm compatible host compiler, CMake, Ninja, Node.js/npm, and browser
  tools;
- configure, build, test, and install in repository-local paths;
- launch both source-tree and installed `graphx-dashboard`;
- inspect minimal, generic nested split/merge, SAR, and FHSS graphs;
- verify exact node/edge/port access in the semantic view without using the
  canvas;
- exercise the documented keyboard table end to end without a mouse;
- verify synchronized selection, group operations, bundle/member inspection,
  search, node edit refresh, and focus preservation;
- verify non-color text/state and reduced-motion behavior;
- exercise local preference save, reload, graph change, malformed-state
  fallback where practical, and reset without any graph PATCH;
- verify 320 CSS-pixel reflow, 200% zoom, and text-spacing overrides;
- record expected results, screenshots, focused-element identity, relevant
  accessibility-tree observations, network requests, and browser-console
  errors; and
- include troubleshooting and a concise human WCAG review worksheet.

The procedure must state objective expectations rather than ask the operator
whether the page merely "looks good." It must explain that a human WCAG pass
is a scoped manual verification, not a formal accessibility certification.

Docker is optional for operators. The native dashboard and GraphX workflows
must remain fully supported without Docker. Do not reuse the legacy FHSS
dashboard container and do not create another dashboard image in this phase.

## Frontend, host-tool, and sanitizer policy

- Continue using the pinned `libgraph/web` lockfile and compatible installed
  host Node.js/npm. Do not impose an obsolete exact Node version.
- Do not create Python or Node virtual environments.
- Do not use `/private/tmp` or related paths for builds, tests, installs,
  screenshots, or operator evidence. Use repository-local paths or
  Docker-managed named volumes.
- Do not add or upgrade a dependency unless the existing toolchain cannot
  satisfy a Phase 3 requirement and the orchestrator records the necessity.
- If a required host package or browser is missing, stop and ask the user to
  install it on the host. Do not silently download an alternate host runtime.
- Use no CDN or runtime network-loaded code. Node.js remains build-time only;
  installed dashboard operation requires no Node runtime or download.
- Preserve native non-Docker configure, build, test, install, and dashboard
  operation.
- Use the supported `containers/sanitizers` environment for proportional
  Linux ASan/LeakSanitizer/UBSan regression. Keep leak detection and undefined
  behavior checks enabled. Use Docker-managed named volumes and the documented
  default parallelism; do not force an emulated CPU architecture.
- Sanitizer Docker evidence supplements rather than replaces the native full
  configured CTest and source/installed browser evidence.

## Implementer report

Report:

- audited pre-existing Phase 3 behavior and what was reused;
- files changed and why;
- semantic-view information architecture and ordering rules;
- shared selection and inspector synchronization behavior;
- exact keyboard and focus-restoration contracts;
- accessible names/relationships/status and non-color behavior;
- reduced-motion behavior;
- local preference schema, graph scoping, bounds, fallback, and request
  isolation;
- narrow viewport, zoom, and text-spacing behavior;
- independently verified generic, SAR, and FHSS semantic identities/counts;
- generated asset inventory and source/rebuilt/installed hashes;
- commands run and exact frontend, C++26, browser, accessibility, sanitizer,
  focused, and full regression results;
- operator screenshot/evidence paths; and
- limitations reserved for Phase 4 or later work.

Build affected C++ in the repository's C++26 mode with warnings treated as
errors. Preserve unrelated changes and do not commit, push, or open a PR.

## Independent verifier

The verifier must not edit implementation, tests, generated assets, fixtures,
documentation, packages, plans, or configuration. It must give an explicit
PASS or FAIL for every Phase 3 acceptance-checklist item with file/line and
direct test, DOM, network, browser, screenshot, and source-data evidence.

Verify especially:

- `GraphCoordinator`, `GraphHttpServer`, one graph fetch/model, one entry
  point, existing `/api/v1` routes, and the mandatory lazy executor contracts
  remain intact;
- the semantic topology is an alternative to the canvas, not another graph
  authority, and contains every authoritative node and edge exactly once with
  exact numeric/named ports;
- generic nested, raw, collapsed, isolated, invalid, and over-bound states
  remain semantically inspectable even when canvas layout is unavailable;
- selection and inspection synchronize across canvas, semantic view, search,
  hierarchy, bundles/member edges, and refresh without identity loss;
- all documented operations are usable from the keyboard with logical order,
  no trap, visible unobscured focus, and deterministic focus restoration;
- screen-reader names, roles, relationships, values, descriptions, and status
  messages are truthful and not excessively duplicated;
- state is never represented only by color, animation, geometry, or motion;
- reduced-motion behavior is effective and does not remove information;
- local preferences are bounded presentation-only state, survive/reject state
  truthfully, reset explicitly, and never enter graph JSON, PATCH, command,
  export, cookie, or server state;
- 320 CSS-pixel reflow, 200% zoom, and text-spacing review preserve all
  reflowable information and operations, with only the two-dimensional canvas
  using its justified pan/zoom exception;
- focused WCAG 2.2 AA evidence is itemized and no automated-only or formal
  certification claim is made;
- generic production code contains no FHSS, detector-count, frequency, IQ,
  message, or SAR rule;
- source-tree and installed-tree browser tests perform direct visual and
  accessibility inspection with screenshots, not DOM assertions alone;
- no Phase 4 metrics subscriber/overlay, command console, new REST route,
  graph mutation, parallel management component, runtime CDN, or new dashboard
  image was introduced;
- tests use independent semantic identity/order/port/focus oracles rather than
  the production implementation as their sole oracle;
- C++26/`-Werror`, clean frontend install/typecheck/tests/build/reproduction,
  focused browser/accessibility tests, supported Docker sanitizer validation,
  full enabled native CTest, source/install asset parity, and
  `git diff --check` pass; and
- only documented hardware/data/local gates are disabled or skipped.

## Stop condition

Stop only when:

- the verifier reports no unresolved blocking or high-severity finding;
- every Phase 3 acceptance criterion is PASS;
- every authoritative node/edge/port is available through the independently
  verified semantic view for representative generic, SAR, and FHSS graphs;
- keyboard, focus, non-color, reduced-motion, local-preference isolation,
  narrow reflow, zoom, and text-spacing tests and human procedures pass;
- source-tree and installed-tree browser suites pass with screenshots and no
  unexpected console errors;
- Phase 0-2 lifecycle, ownership, topology, hierarchy, HTTP, execution
  isolation, packaging, and deprecation regressions remain green;
- frontend, C++26/`-Werror`, supported sanitizer, full enabled configured
  CTest, asset reproduction/parity, and `git diff --check` pass; and
- the verifier supplies an explicit PASS/FAIL matrix with no unresolved Phase
  3 criterion.

Provide a Phase 3 completion report and wait for authorization. Do not proceed
to Phase 4 automatically.
