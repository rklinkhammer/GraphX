# GraphX Generic Dashboard Phase 0 Orchestration

Implement Phase 0 of:

- `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`

Use these as normative supporting descriptions:

- `docs/graphx_dashboard.md`
- `plan/Phase2B_Generic_Graph_Management.md`
- `plan/Phase2B_Corrected_Implementation_Specification.md`

## Agent model assignments

- **Orchestrator:** `gpt-5.6-sol`, `max` reasoning. This role owns lifecycle,
  concurrency, configuration-authority, and phase-boundary decisions.
- **Implementer:** `gpt-5.6-sol`, `xhigh` reasoning. This role performs the
  C++26 runtime refactor, API migration, and focused/full validation.
- **Verifier:** `gpt-5.6-sol`, `ultra` reasoning with independent context. This
  role performs adversarial state-machine, race, ownership, API, and regression
  verification and does not edit implementation files.

Do not substitute a faster general-purpose model for the verifier. This phase
changes lifecycle ownership and concurrent stop/teardown behavior.

## Orchestrator

1. Inspect the repository and convert every Phase 0 requirement into a
   file-level acceptance checklist.
2. Reconcile the three normative documents before assigning implementation.
3. Preserve unrelated changes.
4. Assign all implementation to one implementer agent.
5. After implementation and focused tests, assign independent review to one
   verifier agent with fresh context.
6. Route every verifier finding to the implementer.
7. Repeat implementation and verification until every acceptance criterion is
   PASS and there are no unresolved blocking or high-severity findings.
8. Do not commit, push, open a PR, proceed to Phase 1, or remove `graph-cli`.

## Required Phase 0 implementation

- Add immutable `GraphConfigurationSnapshot` with graph document, monotonic
  revision, and deterministic content identity.
- Make `GraphCoordinator` own the graph document and be the sole mutation and
  snapshot authority. Successful content-changing mutations advance revision;
  failed and no-op mutations do not.
- Add `GraphExecutor::ConfigureGraph(snapshot)` and a distinct `CONFIGURED`
  lifecycle state before `INITIALIZED`. Implement the exact transition table,
  synchronous/asynchronous behavior, natural-run teardown, stop cancellation,
  join behavior, dirty-state rejection, rollback, and error recovery specified
  by the normative plan; do not invent different lifecycle semantics.
- Refactor `GraphExecutorBuilder` so `Build()` returns a configured executor
  shell without loading providers, instantiating nodes, constructing a
  `GraphManager`, or starting threads.
- Preserve non-dashboard `Build()` plus `Execute()` behavior through an initial
  configured snapshot and update every affected call site.
- Register typed `CommandCapability` and `MetricsCapability` before returning
  the executor. Policies consume the same instances during `Init`.
- Define typed command discovery, request, result, operation identity,
  asynchronous completion, bounded retention, and HTTP status contracts under
  `/api/v1/execution/*`.
- Implement `GET /api/v1/execution/commands`,
  `POST /api/v1/execution/commands/{name}`, and
  `GET /api/v1/execution/operations/{operation_id}`. Use `200` for synchronous
  success, `202` plus `Location` for accepted asynchronous work, `409` for
  invalid transitions, and `404` for unknown commands or operation IDs.
- Route `GraphHttpServer` lifecycle operations through `CommandCapability`;
  remove all direct server calls to executor lifecycle methods.
- Run blocking execution on a joinable worker without holding the transition
  lock. `stop` must use `RequestStop()` as a fast path, and teardown must occur
  exactly once on the execution thread.
- Make `graphx-dashboard` always create the configured executor and remove
  `--enable-execution`. Loading the page must not initialize or execute nodes.
- Construct one owning `shared_ptr<GraphCoordinator>`, pass its single initial
  `Snapshot()` to `GraphExecutorBuilder::WithGraphSnapshot`, and pass that same
  coordinator plus shared command/metrics capabilities to `GraphHttpServer`.
  The server receives no raw executor pointer and creates no second graph
  document. Retain `WithJsonConfig` only as a non-dashboard compatibility
  adapter that produces one initial snapshot.
- Add `configure` to the HTTP lifecycle surface using one atomic coordinator
  snapshot.
- Report coordinator revision, configured revision, active revision, graph
  generation, and `configuration_dirty`. A PATCH must never mutate an
  instantiated graph implicitly. Successful configure advances generation and
  clears stale metrics and operation state; `start` and `run` reject dirty
  configuration until reconfiguration succeeds.
- Preserve the existing `GraphCoordinator`, `GraphHttpServer`,
  `graphx-dashboard`, `/api/v1`, and `index.html`; create no parallel runtime
  session, server, coordinator, dashboard executable, or FHSS management path.
- Keep `graph-cli` deprecated and compatibility-tested. Do not remove it in
  this phase.

## Implementer report

Report:

- files changed and why;
- transition table and ownership/lifetime decisions;
- commands run and exact results;
- focused and full regression results;
- migration effects on non-dashboard executor users; and
- known limitations reserved for later graphical phases.

Build affected C++ in repository C++26 mode with warnings treated as errors.
Use repository-local build/test paths and do not use `/private/tmp`.

## Independent verifier

The verifier must not edit implementation files. Give explicit PASS or FAIL
for every Phase 0 criterion and cite file and line evidence. Check especially:

- builder performs no prohibited eager work;
- executor and both capabilities exist before `Init`;
- capability object identity survives `Init`;
- configure failure is atomic;
- coordinator snapshots cannot mix document and revision;
- PATCH-before-configure is the exact graph later initialized;
- PATCH-after-init reports dirty state without live mutation;
- every invalid lifecycle transition fails truthfully;
- concurrent and duplicate commands are serialized;
- blocking run does not prevent stop;
- executor teardown occurs exactly once;
- command workers and pending operations are joined during destruction;
- HTTP uses no direct executor lifecycle calls;
- launcher has no nullable/no-executor mode or `--enable-execution`;
- existing non-dashboard executors retain supported behavior;
- no legacy FHSS dashboard dependency or parallel management component appears;
- focused tests, full regression tests, and `git diff --check` pass.

## Stop condition

Stop only when the verifier reports no unresolved blocking/high findings,
every Phase 0 criterion passes, affected and full regression suites pass, and
`git diff --check` passes. Provide a Phase 0 completion report and wait for
authorization before Phase 1.
