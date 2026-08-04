# GraphX Generic Dashboard Phase 4 Orchestration

Implement Phase 4 of:

- `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`

Use these as normative supporting descriptions:

- `docs/graphx_dashboard.md`
- `plan/Phase2B_Generic_Graph_Management.md`
- `plan/Phase2B_Corrected_Implementation_Specification.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE0_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE1_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE2_ACCEPTANCE_CHECKLIST.md`
- `plan/GRAPHX_GENERIC_DASHBOARD_PHASE3_ACCEPTANCE_CHECKLIST.md`
- `docs/graphx_dashboard_phase1_operator_test.md`
- `docs/graphx_dashboard_phase2_operator_test.md`
- `docs/graphx_dashboard_phase3_operator_test.md`
- `containers/sanitizers/README.md`

Phase 4 adds generic typed-command interaction, browser export, and truthful
runtime metric overlays to the one existing GraphX dashboard. It does not add
another runtime-management path, dashboard, executor, coordinator, event
stream, domain service, or API namespace.

## Phase 3 prerequisite status

Phase 3 implementation and every automated qualification gate passed at
commit `e38de59b`. The independent Phase 3 verifier reported 81/83 criteria:

- G8 remained FAIL because the focused human WCAG worksheet in
  `docs/graphx_dashboard_phase3_operator_test.md` was blank.
- I10 consequently remained FAIL because not every Phase 3 criterion passed.

The orchestrator may audit Phase 4 and create its acceptance checklist before
this prerequisite is closed, but it must not assign Phase 4 implementation
until a human operator has completed the Phase 3 worksheet with explicit
PASS/FAIL/N/A, evidence, and observations and an independent verifier has
closed G8 and I10. Do not reinterpret automated browser evidence as human
evidence, fill the worksheet speculatively, or weaken the prerequisite. If it
is still incomplete, pause after the audit/checklist and request the missing
human evidence.

## Verified Phase 0-3 baseline

Treat the following as preserved contracts, subject to the Phase 3 human gate
above:

- GraphX has one graph-management implementation and one generic dashboard.
- `GraphCoordinator` is the sole graph-document and revision authority.
- `GraphHttpServer` is the HTTP adapter. It has no `GraphExecutor` pointer and
  makes no direct executor lifecycle calls.
- `graphx-dashboard` owns one mandatory, configured, lazy `GraphExecutor`.
- The lifecycle is
  `ConfigureGraph -> Init -> Start -> Run -> Stop -> Join`.
- `GraphExecutorBuilder` creates a configured executor shell without loading
  providers, instantiating nodes, constructing a `GraphManager`, or starting
  threads before `Init`.
- The exact `CommandCapability` and `MetricsCapability` objects exist before
  `Init`; policies bind to those objects rather than replacing them.
- Existing typed command discovery, submission, operation lookup, execution
  state, operation retention, revision, generation, dirty-state, asynchronous
  run, cooperative stop, and exactly-once teardown behavior are authoritative.
- Existing lifecycle resources are under `/api/v1/execution/*`. HTTP submits
  typed requests through `CommandCapability`.
- `GET /api/v1/graph` is the sole authoritative topology input. One fetched
  client-side graph document feeds the canvas, semantic topology, hierarchy,
  search, and inspectors.
- Node edits use `PATCH /api/v1/nodes/{id}` and truthfully update coordinator
  revision and dirty state without mutating an initialized graph.
- `libgraph/resources/web/index.html` is the sole dashboard entry point. The
  pinned React Flow/ELK/React TypeScript frontend lives under `libgraph/web`;
  generated assets are reproducible, self-hosted, and installed under one
  generic resource root.
- Raw topology, exact numeric/named ports, deterministic full-tuple edge
  identity, optional generic presentation groups, bundles, minimap, collapse,
  isolation, search, semantic topology, shared selection, focus restoration,
  local preferences, narrow reflow, and reduced motion are implemented.
- Minimal 2/1, nested 4/3, complex 9/9, SAR 21/23, and FHSS 75/137 graphs use
  the same generic implementation. Generic production code has no FHSS or SAR
  rule.
- `SimpleHttpServer` retains its bounded 8-active/16-pending worker behavior,
  sticky static root, contained asset lookup, SIGPIPE-safe writes, and
  joinable shutdown.
- `graph-cli` is deprecated but remains a migration adapter until the Phase 5
  removal gate. Phase 4 does not remove it or add new CLI-only functionality.
- The final Phase 3 automated tree passed 157/157 frontend tests, 37/37
  enabled native CTests, 9/9 focused Docker sanitizer tests, source/installed
  Firefox matrices, asset reproduction/parity, and `git diff --check`.

Preserve all of these behaviors and their regression coverage. Phase 4 must
extend the current implementation in place.

## GraphX maturity and validation policy

GraphX is an engineering and research platform. Prioritize architecture,
correctness, determinism, maintainability, truthful operator behavior, and
performance. Apply validation proportionally to command serialization,
subscriber lifetime, metric identity, generation/reset behavior, bounded
storage and polling, export consistency, accessibility, and shutdown races.

Do not expand Phase 4 into authentication, authorization, TLS, CSP, hostile-
Internet hardening, browser sandboxing, account storage, or general security
work. The command palette is a structured GraphX control surface, not a shell.

There is no hardware-in-the-loop validation. All generic, SAR, and FHSS
operator data and metric events are synthetic or checked-in recorded data. Do
not claim HWIL, live RF, OTA, conducted RF, hardware, or production deployment
qualification.

## Agent model assignments

- **Orchestrator:** `gpt-5.6-sol`, `max` reasoning. This role owns prerequisite
  enforcement, the runtime identity decision, command/metric/export schemas,
  lifetime and bounds contracts, phase scope, and acceptance decisions.
- **Implementer:** `gpt-5.6-sol`, `xhigh` reasoning. This role implements the
  generic C++ capability/server changes, one frontend command and metrics
  surface, browser export, tests, generated assets, and operator evidence.
- **Verifier:** `gpt-5.6-sol`, `ultra` reasoning with independent context. This
  role performs adversarial command-state, identity, subscriber-race, stale-
  generation, aggregation, browser, accessibility, packaging, sanitizer, and
  regression review. It must not edit implementation files.

Do not substitute a faster general-purpose model for the verifier. Runtime
telemetry that looks plausible while being mapped to the wrong node,
generation, edge, or counter epoch is worse than displaying it as unavailable.

## Orchestrator

1. Inspect the current repository and preserve unrelated changes. Record the
   starting commit, branch, worktree state, configured tests, disabled gates,
   installed host tool versions, and prior Phase 3 evidence.
2. Check the Phase 3 G8/I10 prerequisite exactly as described above. The
   audit and checklist may proceed if it is open; implementation may not.
3. Convert every Phase 4 requirement and every preserved Phase 0-3 invariant
   into a file-level acceptance checklist at:
   `plan/GRAPHX_GENERIC_DASHBOARD_PHASE4_ACCEPTANCE_CHECKLIST.md`.
4. Audit the current command and metric implementation before locking the
   checklist, including:
   - `CommandCapability`, `MetricsCapability`, `CommandPolicy`,
     `MetricsPolicy`, `GraphExecutor`, `GraphExecutorBuilder`,
     `GraphCoordinator`, `GraphHttpServer`, `GraphCli`, and
     `graphx-dashboard`;
   - the typed command descriptors, arguments, HTTP status mapping,
     asynchronous operation retention, and frontend execution buttons;
   - `MetricsEvent`, `NodeMetricsSchema`, every metrics publisher, subscriber
     registration/removal, callback locking, policy initialization, and
     generation reset behavior; and
   - the exact relationship, if any, between `MetricsEvent::source`,
     `NodeMetricsSchema::node_name`, authoritative graph node IDs, exact edge
     identities, and graph generation.
5. Complete the metric-identity prerequisite described below before assigning
   overlay implementation.
6. Lock and record exact numeric bounds and schemas before implementation:
   command history, operation polling, metric schemas/fields/current values,
   response bytes, string/value sizes, snapshot cadence, browser polling,
   stale timeout, animated objects, aggregation work, callback work, and
   shutdown waits. Derive values from existing GraphX bounds and measured
   representative graphs; do not leave them as “reasonable” or “bounded.”
7. Lock the exact command-palette interaction model, export envelope, metric
   snapshot route/response, availability semantics, generation/reset rules,
   aggregation rules, pause behavior, keyboard/focus behavior, and accessible
   status wording in the checklist.
8. Assign implementation to one implementer only after both prerequisite
   gates pass.
9. After implementation and focused tests, assign independent review to one
   verifier with fresh context.
10. Route every verifier finding back to the implementer. Repeat focused and
    independent verification after each remediation; do not count pre-fix
    results as final-tree evidence.
11. Do not commit, push, open a PR, proceed to Phase 5, remove `graph-cli`, add
    a second dashboard/server/coordinator/runtime, add a domain API, or add
    speculative security work.

## Mandatory metric-identity prerequisite

Before displaying a runtime value on any authoritative node, edge, bundle, or
group, prove a stable generic mapping from the publisher to the exact graph
identity and graph generation.

Specifically:

- Do not assume a node class/type, display label, prefix, or non-unique name
  equals the authoritative graph node ID.
- Do not correlate `MetricsEvent::source` or
  `NodeMetricsSchema::node_name` by string coincidence unless their producer
  contract already guarantees the exact authoritative instance ID and direct
  tests prove duplicate-type and duplicate-label cases.
- A node metric must carry or be mapped through an immutable runtime binding
  to the exact authoritative node ID and active graph generation.
- An edge metric must carry the exact authoritative full-tuple edge identity,
  including source/target node and numeric/named port kind/value. Do not infer
  edge activity from adjacent node totals.
- A metric schema and value must use the same stable metric identity, declared
  type, unit, semantics, availability rule, and generation.
- If the current generic facilities cannot provide this mapping, stop before
  implementing overlays. Produce a concise generic identity-contract decision
  in the Phase 4 checklist/report, identify the smallest runtime contract
  change required, and request authorization. Do not hide the gap with an
  FHSS-specific mapping or frontend heuristic.

The orchestrator may authorize a minimal generic extension to the existing
metrics value objects and policy binding only when it is clearly part of the
Phase 4 identity contract. It must not create a second metrics service,
publisher, runtime session, or topology authority.

## Required Phase 4 implementation

### One typed command surface

- Retain `CommandCapability` as the sole lifecycle command authority.
- Retain the existing lifecycle state machine and exact configured/active
  revision, graph-generation, dirty-state, asynchronous operation, cooperative
  stop, and exactly-once teardown behavior.
- Keep `GraphHttpServer` free of direct `GraphExecutor` lifecycle calls.
- Route deprecated CLI lifecycle requests through the same typed capability.
  Terminal parsing is an adapter only; terminal strings are not capability
  requests.
- Migrate the existing web execution buttons and the new command palette to
  one shared typed frontend client using command discovery and
  `POST /api/v1/execution/commands/{name}`. Preserve documented legacy REST
  compatibility without making it the new UI path.
- Render discovered command argument schemas as bounded structured controls.
  Reject unsupported field types visibly instead of emitting guessed JSON.
- Do not expose a free-form shell, terminal, executable path, environment
  variable, filesystem path, arbitrary URL, or unrestricted JSON command box.
- Retain bounded in-page command history containing command identity,
  operation ID, acceptance/completion status, executor state, revisions,
  generation, and concise diagnostic. Keep it in page memory unless the
  checklist explicitly justifies a bounded local presentation record; never
  put it in graph JSON, cookies, coordinator state, or a new server store.
- Follow `202 Location` asynchronously through the existing operation route.
  Poll at the locked bounded cadence, stop on terminal status, cancel polling
  on unmount/reload, and report unknown/expired operations truthfully.
- Serialize or reject concurrent and duplicate commands according to the one
  documented state machine. UI button disabling is advisory; correctness must
  remain server-side.
- Preserve inspection and export while the executor is only `CONFIGURED`, and
  preserve cooperative Stop while Run is blocking.

### Browser graph export

- Add an explicit keyboard-operable browser download action to the existing
  page. Do not add a server-side save path or filesystem write.
- Export one atomic `GraphCoordinator::Snapshot()`, not a graph document and
  revision obtained in separate races.
- Lock a versioned export envelope containing the exact authoritative graph
  document, coordinator revision, and deterministic content identity. Do not
  insert runtime metrics, command history, execution state, selection,
  presentation preferences, or browser-only state into the graph document.
- Preserve every node, edge, exact port, `node_config`, and authored generic
  presentation field byte-semantically after JSON parse. Document that the
  export wrapper is an artifact format, not a second live graph authority.
- Use a deterministic bounded filename and Blob/object-URL lifecycle. Revoke
  the object URL after download and report failures through the existing
  accessible status mechanism.
- Obtain the export data through the existing authoritative graph resource or
  the smallest backward-compatible metadata addition to it. Do not add an
  export service or another graph endpoint.

### `GraphHttpServer` as the metrics subscriber

- Make `GraphHttpServer` implement `app::metrics::IMetricsSubscriber` as the
  plan requires. A pimpl may own storage, but it must not become a separate
  dashboard publisher or subscriber authority.
- Register exactly one server subscriber with the mandatory pre-`Init`
  `MetricsCapability` while all callback-visible storage is alive.
- Unregister deterministically before callback-visible server storage,
  request workers, capability handles, or the executor can be destroyed.
- Define and test registration behavior for Start failure, repeated Start,
  repeated Stop, never-started destruction, executor teardown, and server
  destruction.
- Make registration/removal and an in-flight publication race-safe. Do not
  rely on timing, detached workers, raw-pointer lifetime hope, or process exit.
- The callback may only validate bounded scalar metadata and copy/update
  bounded server-owned snapshot state. It must not perform socket I/O, HTTP
  response construction, JSON serialization, logging proportional to event
  rate, layout, allocation proportional to untrusted history, or calls back
  into `MetricsCapability`.
- HTTP response generation must copy a consistent snapshot under a short lock
  and serialize it after releasing the callback lock.
- A successful Configure/generation change must atomically invalidate prior
  schemas, values, rates, sequence state, and browser correlation. Late events
  from an older generation must be rejected.

### One bounded generic metric snapshot resource

- Add exactly one read-only generic metric snapshot resource under
  `/api/v1`; the orchestrator must lock its exact path and schema before code.
  Prefer the plan-anticipated `GET /api/v1/metrics` unless repository evidence
  requires a different single generic path.
- Do not add WebSocket, SSE, long-poll, per-message event, history, domain, or
  FHSS routes.
- The response must identify its schema version, graph generation, active
  revision, snapshot sequence/time, schemas, current values, and explicit
  availability/staleness. It must preserve exact node/edge/metric identities,
  declared type, unit, semantics, and sample time.
- Validate and bound schemas and events before publishing them to HTTP state.
  Unsupported types, invalid values, non-finite numbers, oversize strings,
  unknown identities, schema mismatches, old generations, and over-bound input
  fail safely and observably without blocking execution.
- Missing, not-yet-initialized, stopped, stale, reset, unknown, or
  uncorrelated values are `unavailable` with a reason. Never serialize or
  display absence as numeric zero.
- Do not compute rates unless the schema declares monotonic counter semantics
  and two ordered samples from the same identity, unit, generation, and
  counter epoch exist. A decrease/reset invalidates the rate until another
  valid pair exists.
- Metrics are observation only. They cannot change scheduling, queueing,
  backpressure, graph configuration, command acceptance, or execution state.

### Generic runtime presentation

- Extend the existing canvas, semantic topology, group/bundle presentation,
  and shared inspector; do not add another frontend graph model or fetch.
- Display only runtime information supported by the locked source contracts.
  Candidate information includes node state, queue depth, backpressure, and
  exact edge activity; unsupported information remains unavailable.
- Associate node metrics with the same authoritative node identity used by
  selection and inspection. Associate edge metrics only with exact
  authoritative full-tuple edge identity.
- Collapsed groups aggregate only the explicitly enumerated authoritative
  member nodes/edges. Apply sum/min/max/average/rate only when the metric
  schema declares compatible aggregation semantics and units. Otherwise show
  member availability/counts without inventing a value.
- A presentation bundle remains a bundle of exact authoritative edges. It may
  aggregate only compatible enumerated member-edge metrics and must retain
  exact-member inspection.
- Preserve raw/grouped, collapse/expand, isolation, search, selection, focus,
  and semantic accessibility during metric updates.
- Never invoke ELK or any layout computation for a metric-only update. Add a
  direct layout-spy/count oracle.
- Poll the bounded snapshot resource at the locked cadence. Avoid overlapping
  requests; abort/ignore stale responses and stop polling on unmount.
- Add a keyboard-operable local **Pause runtime updates** control. Pause stops
  new metric polling/animation but leaves the last snapshot visibly labelled
  paused with its capture time; it does not pause GraphExecutor execution.
- Bound rendered detail, retained samples, update work, and animated edges.
  Prefer current-value snapshots rather than browser histories.
- Honor `prefers-reduced-motion`. Motion, pulse, color, thickness, opacity, or
  geometry must never be the only metric/state representation. Provide exact
  textual values, units, availability, timestamps, and status equivalents in
  semantic/inspector surfaces.
- Use concise scoped live announcements for command acceptance/completion,
  export completion/failure, metric connection loss/recovery, pause/resume,
  and stale-generation invalidation. Do not announce every metric sample.

### Phase boundary and exclusions

Phase 4 does not:

- add FHSS observations, expected truth, spectrum, jobs, investigations,
  message displays, detector-specific rules, or RF controls;
- add SAR-specific runtime semantics;
- add a second dashboard, executable, coordinator, runtime owner, metrics
  service, HTTP server, frontend root, or API namespace;
- depend on `libgraph/src/dashboard`, `libgraph/include/graph/dashboard`,
  `GraphRuntimeSession`, the legacy embedded dashboard publisher, or
  `examples/DSP/dashboard`;
- mutate topology from the canvas or metrics;
- remove `graph-cli` before the Phase 5 parity/removal gate;
- add a generic operator Docker image; or
- proceed into Phase 5 packaging/performance work.

## Accessibility and responsive behavior

Preserve the Phase 3 keyboard, focus, semantic, non-color, reduced-motion,
320-CSS-pixel, 200%-zoom, and text-spacing contracts for all new controls.

At minimum:

- command discovery/forms/history, operation status, export, metric pause,
  runtime values, unavailable reasons, and overlay legends have unique names,
  meaningful order, visible labels, truthful name/role/value/state, and
  visible unobscured focus;
- command forms identify invalid fields and provide useful corrections before
  submission where possible;
- command completion and failures use status/alert semantics without duplicate
  announcements;
- tables or metric grids use native semantics and contained labelled overflow;
- exact identities and values remain accessible without the canvas;
- focus remains deterministic when command history is truncated, operations
  complete, metric data changes/disappears, pause toggles, graph generation
  changes, or export finishes;
- no positive `tabindex`, keyboard trap, focus-only context change, or
  auto-focus churn is introduced; and
- target sizes, contrast, non-text contrast, label-in-name, reflow, text
  spacing, and reduced motion receive direct changed-path evidence.

This is focused WCAG 2.2 AA evidence, not a claim of formal certification.
Automated checks supplement but do not replace the required human keyboard,
screen-reader-oriented semantics, focus, zoom, reflow, and status review.

## Required tests and independent oracles

Tests must not use the production command client, metric adapter, aggregator,
or renderer as their only oracle. Independently author command transitions,
operation results, graph exports, node/edge identities, metric schemas,
samples, counter epochs, group membership, and expected aggregate/
availability results.

Add or extend:

### C++ capability and server tests

- pre-`Init` command and metrics capability identity and lifetime;
- exact CLI/HTTP typed-command equivalence for every lifecycle operation;
- allowed/invalid transitions, dirty revisions, async Run, cooperative Stop,
  Join, duplicate/concurrent submission, operation expiry, and exactly-once
  teardown;
- proof that `GraphHttpServer` has no executor pointer/direct lifecycle call;
- metric publisher-to-authoritative-ID and graph-generation mapping,
  including duplicate node types/names and exact numeric/named edge ports;
- schema/value validation, units, types, unsupported flags, finite values,
  bounds, unavailable reasons, stale generation, late publication, counter
  reset, and deterministic snapshot ordering;
- exactly-once subscriber registration/removal across Start/Stop/failure/
  destruction paths;
- unregister racing with in-flight publication, server shutdown under active
  metrics requests, and executor teardown under publication;
- callback instrumentation proving no socket I/O, JSON serialization, layout,
  capability re-entry, or unbounded history work;
- metric snapshot route method/path/status/schema/size/ordering and snapshot
  consistency under concurrent publication;
- atomic graph export metadata under concurrent successful/no-op/failed node
  edits; and
- inspection and metric-unavailable responses while the executor remains
  `CONFIGURED` and uninitialized.

### Frontend unit/component tests

- shared typed submission for buttons and command palette;
- command discovery, structured argument controls, invalid/unsupported schema,
  200/202/409/404/503 behavior, operation polling, expiry, cancellation, and
  bounded history eviction with deterministic focus;
- absence of shell/free-form terminal execution and forbidden request paths;
- browser export envelope, exact document/revision/content identity, filename,
  object-URL revocation, failure status, and absence of runtime/browser state;
- metric response parsing and bounds, exact identity correlation, unavailable
  reasons, generation replacement, out-of-order response rejection, stale
  timeout, counter reset, and unit/type mismatch;
- independently calculated compatible group/bundle aggregates and explicit
  unavailable behavior for incompatible or uncorrelated metrics;
- node/edge/group/bundle canvas, semantic, and inspector synchronization;
- polling cadence, no overlap, abort/unmount cleanup, pause/resume, bounded
  rendered history/detail, and no PATCH/command request from metric-only UI;
- a layout spy proving zero layout calls for metric-only updates;
- keyboard/focus/live-region/non-color/reduced-motion tests for every new
  control and asynchronous transition; and
- 320-pixel reflow, 200% page zoom, text spacing, contrast, and target-size
  evidence.

### Integration, browser, and regression tests

- source-tree and clean installed-tree Firefox tests for command discovery,
  button/console parity, asynchronous completion, cooperative stop, export,
  metrics availability/pause/staleness, exact identities, and zero unexpected
  console errors;
- independently authored synthetic metric fixtures for minimal, nested,
  complex, SAR, and FHSS graph structures without domain-specific production
  rules;
- direct request logs proving only the existing graph/node/execution resources
  plus the single authorized metric snapshot resource are used;
- screenshots at desktop, 320 CSS pixels with text-spacing overrides, and
  actual 200% Firefox page zoom in both motion modes;
- architecture scans rejecting a second authority/server/runtime/dashboard,
  legacy embedded dashboard imports, `/api/v2`, `/api/v1/fhss`, shell
  execution, server-side export writes, per-message streams, domain-specific
  generic code, runtime CDN, Phase 5 CLI removal, and unapproved routes;
- source/rebuilt/installed `index.html`/JS/CSS inventory and SHA-256 parity;
- C++26 warnings-as-errors build and focused command/metrics/HTTP tests;
- supported Docker ASan/LeakSanitizer/UBSan validation with subscriber/
  publication/shutdown race tests;
- full enabled configured native CTest; and
- repository-wide `git diff --check`.

Tests must be condition-based. Do not use fixed sleeps as synchronization,
increase timeouts to conceal races, or run destructive overlapping builds in
the same tree. Preserve the exact final-tree evidence after any remediation.

## Operator evidence

Create:

- `docs/graphx_dashboard_phase4_operator_test.md`

The operator document must start with a fresh clone/download into an
operator-owned workspace and a build from first principles. Document the
ordinary native, non-Docker workflow first:

- prerequisites for the host compiler, CMake, Ninja, Node.js/npm, jq, and
  Firefox or another supported browser;
- repository-local configure, C++26 build, frontend check, native tests,
  install, and source/installed dashboard launch commands;
- separate-terminal instructions for a long-running dashboard;
- exact expected initial `CONFIGURED` state with topology, semantic view, and
  export available before Init;
- REST examples for command discovery, each lifecycle command, asynchronous
  operation lookup, invalid transitions, execution state, and the single
  metric snapshot resource;
- browser exercises proving execution buttons and structured command palette
  produce equivalent typed results and cannot invoke a shell;
- a finite synthetic graph run that demonstrates asynchronous Run,
  cooperative Stop, terminal operation status, and exactly one teardown;
- graph export/download comparison against one atomic authoritative snapshot,
  including revision and content identity;
- synthetic minimal/nested/complex/SAR/FHSS metric examples showing exact
  identity, units, availability, stale/reset behavior, pause/resume, semantic
  access, collapsed-group aggregation, and no layout movement;
- expected request inventory, response shapes, console output, status text,
  focused-element identity, screenshot names, and failure diagnostics;
- keyboard-only, screen-reader-oriented semantic, non-color, reduced-motion,
  320-pixel, actual 200%-zoom, and text-spacing procedures for changed paths;
- a focused human WCAG worksheet with Status, Evidence, and Observation
  columns; and
- troubleshooting for missing schemas, unavailable values, generation change,
  expired operations, dirty configuration, stopped execution, export failure,
  and browser polling pause.

Every operator expectation must be objective and reproducible. State clearly
that all metrics and FHSS/SAR data are synthetic or recorded and that there is
no HWIL validation. Docker is optional and supplemental; native build,
operation, and verification remain supported.

## Frontend, host-tool, and sanitizer policy

- Use the compatible Node.js/npm installed on the host and the pinned
  `libgraph/web` lockfile. Do not impose an obsolete exact Node version.
- Do not create Python or Node virtual environments.
- Do not use `/private/tmp` or related paths. Use repository-local build,
  install, evidence, and screenshot paths or Docker-managed named volumes.
- Do not add or upgrade a dependency unless existing tools cannot satisfy a
  Phase 4 requirement and the orchestrator records why. If a required host
  package is missing, stop and ask the user to install it on the host.
- Use no CDN or runtime-loaded dependency. Installed dashboard operation must
  require no Node.js runtime or network download.
- Preserve native non-Docker configure, build, test, install, and dashboard
  operation.
- Use the supported `containers/sanitizers` image with Docker-managed named
  volumes, documented default parallelism, LeakSanitizer and UBSan enabled,
  and no forced emulated architecture.
- Do not create another dashboard/operator Docker image in Phase 4.

## Implementer report

Report:

- audited pre-existing command, metric, export, and frontend behavior;
- prerequisite evidence and the exact metric identity/generation contract;
- files changed and why;
- command discovery/form/submission/history/operation behavior and bounds;
- CLI/HTTP/button/console typed-command equivalence;
- export envelope, atomicity, filename, browser lifecycle, and exclusion of
  runtime/browser state;
- subscriber ownership, registration/removal order, callback constraints,
  snapshot locking, and race behavior;
- metric route schema, exact identity, generation, units, type, availability,
  reset/rate semantics, ordering, and every numeric bound;
- overlay, semantic text, group/bundle aggregation, polling, pause,
  reduced-motion, focus, and no-relayout behavior;
- independently verified minimal/nested/complex/SAR/FHSS identities and
  metric expectations;
- source/rebuilt/installed asset inventories and hashes;
- exact commands and results for frontend, C++26, browser, accessibility,
  focused, sanitizer, and full regression tests;
- operator evidence and screenshot paths; and
- known limitations reserved for Phase 5 or later.

Build affected C++ in repository C++26 mode with warnings treated as errors.
Preserve unrelated changes. Do not commit, push, or open a PR.

## Independent verifier

The verifier must not edit implementation, tests, generated assets, fixtures,
documentation, packages, plans, or configuration. It must independently audit
both normative documents and give explicit PASS or FAIL for every Phase 4
acceptance-checklist item, with file/line and direct C++, DOM, request,
browser, screenshot, source-data, and race-test evidence.

Verify especially:

- Phase 3 G8/I10 were genuinely closed by recorded human evidence before
  implementation began;
- `GraphCoordinator`, `GraphHttpServer`, the mandatory configured executor,
  one graph model, one entry point, and the lifecycle authority remain singular;
- exact pre-`Init` `CommandCapability`/`MetricsCapability` object identity and
  the configured lifecycle remain intact;
- HTTP, deprecated CLI, buttons, and command palette adapt to the same typed
  command capability and produce equivalent results;
- the command palette is structured, bounded, accessible, and incapable of
  arbitrary shell/terminal/host execution;
- operation polling and history are bounded, cancel safely, and preserve exact
  operation/revision/generation state;
- browser export is one atomic authoritative snapshot with revision/content
  identity and contains no metrics, history, execution, selection, or local
  preferences;
- `GraphHttpServer` is the direct `IMetricsSubscriber`, registers exactly once
  while storage is alive, unregisters before destruction, and remains safe
  against in-flight publication and request handling;
- metric callbacks perform only bounded snapshot updates and no I/O,
  serialization, layout, capability re-entry, or unbounded retention;
- every displayed metric maps to exact authoritative node/edge identity and
  graph generation without name/type/prefix/domain heuristics;
- schemas, units, types, sample times, bounds, stale/unavailable/reset/rate
  semantics, deterministic ordering, and response sizes are enforced;
- missing and uncorrelated values render unavailable, never zero;
- old generations and counter resets cannot create false current values or
  rates;
- metrics never affect GraphX scheduling, queueing, topology, configuration,
  command acceptance, or execution;
- collapsed group/bundle aggregates use only enumerated authoritative members
  with compatible declared aggregation semantics;
- metric-only updates never rerun layout and rendered/polled/animated work is
  bounded;
- pause/reduced-motion/non-color/semantic/status behavior remains truthful and
  does not pause the executor;
- source and installed browser tests include direct visual/semantic review,
  screenshots, exact request allowlists, and no console errors;
- generic production code contains no FHSS, SAR, detector-count, frequency,
  IQ, message, truth, spectrum, job, or investigation rule;
- no second management component, legacy dashboard dependency, new namespace,
  per-message stream, server-side export write, CDN, or new image exists;
- `graph-cli` remains deprecated and present for Phase 5 removal after parity;
- C++26/`-Werror`, clean frontend install/check/reproduction, source/install
  browser tests, sanitizer races, all enabled native CTests, asset parity, and
  `git diff --check` pass; and
- only documented local/data/hardware gates are disabled or skipped.

Report findings by severity with exact file and line references. Route every
finding back through the orchestrator. Do not accept implementer test reports
as independent evidence and do not convert automated WCAG evidence into a
human pass.

## Stop condition

Stop only when:

- Phase 3 G8 and I10 are independently verified PASS;
- the metric identity prerequisite is proven, or an explicitly authorized
  generic identity-contract change is implemented and proven;
- the verifier reports no unresolved blocking or high-severity findings;
- every Phase 4 acceptance criterion is PASS;
- command discovery, buttons, palette, deprecated CLI, asynchronous
  operations, cooperative stop, and concurrency use one typed authority;
- export is atomic, browser-only, revisioned, and byte-semantically preserves
  the authoritative graph document;
- subscriber registration, publication, unregistration, request handling, and
  teardown race tests pass with no lifetime defect;
- every displayed metric is exact, bounded, generation-safe, unit/type-safe,
  and truthfully available/unavailable;
- metric updates do not affect execution or rerun layout, and aggregation,
  polling, history, pause, motion, accessibility, and reflow contracts pass;
- source-tree and installed-tree browser suites pass with screenshots and no
  unexpected requests or console errors;
- Phase 0-3 architecture, topology, hierarchy, semantic, focus, preference,
  HTTP, lifecycle, packaging, and deprecation regressions remain green;
- frontend, clean dependency install, C++26/`-Werror`, supported Docker
  sanitizer, full enabled CTest, asset reproduction/parity, architecture
  scans, and `git diff --check` pass; and
- the verifier supplies an explicit PASS/FAIL matrix with no unresolved Phase
  4 criterion.

Provide a Phase 4 completion report and wait for authorization. Do not proceed
to Phase 5 automatically.
