# GraphX Generic Dashboard Phase 0 Acceptance Checklist

This checklist is the orchestrator's file-level interpretation of Phase 0 in
`plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`. The implementer must report
evidence for every item; the independent verifier must mark every item PASS or
FAIL.

## A. Configuration authority

- [ ] Add a public immutable `GraphConfigurationSnapshot` value containing the
  complete graph document, monotonic revision, and deterministic content
  identity.
- [ ] Update `libgraph/include/graph/GraphCoordinator.hpp` and
  `libgraph/src/graph/GraphCoordinator.cpp` so the coordinator owns its JSON
  document; no outside mutable alias bypasses its mutex or revision.
- [ ] `Snapshot()` copies document, revision, and content identity atomically.
- [ ] A successful content-changing `UpdateNodeConfig` advances revision once.
  Failed and no-op updates do not advance it.
- [ ] Coordinator tests cover initial snapshot, stable identity, changed
  identity, no-op update, failed update, and concurrent snapshot/update
  consistency.

## B. Configured executor lifecycle

- [ ] Add `CONFIGURED` before `INITIALIZED` in the public execution state
  contract and update every state formatter/parser/test.
- [ ] Add `GraphExecutor::ConfigureGraph(snapshot)` with atomic rollback.
- [ ] Configure performs no provider loading, graph/node construction, thread
  creation, policy initialization, or node lifecycle work.
- [ ] Init rejects an unconfigured or dirty snapshot and constructs/initializes
  exactly the configured document.
- [ ] Successful Init records active revision; successful configure advances
  graph generation and clears prior generation telemetry and command results.
- [ ] Coordinator, configured, and active revisions plus graph generation and
  `configuration_dirty` are queryable.
- [ ] Reconfigure is accepted only while configured or after joined stopped
  teardown. It is rejected in initialized, running, and stopping states.
- [ ] Init/start/run reject dirty configuration. A running generation remains
  bound to its recorded active revision.
- [ ] Configure/Init failure rollback and terminal cleanup failure behavior
  match the normative transition table.
- [ ] Lifecycle unit tests cover every allowed and rejected command/state pair.

## C. Lazy builder and compatibility

- [ ] Add `GraphExecutorBuilder::WithGraphSnapshot(snapshot)`.
- [ ] `Build()` returns a configured shell without loading providers,
  instantiating nodes, constructing `GraphManager`, starting threads, or
  initializing policies.
- [ ] `WithJsonConfig` remains a non-dashboard compatibility adapter that
  creates exactly one initial snapshot.
- [ ] Non-dashboard `Build()` plus `Execute()` callers retain supported
  behavior after call-site migration.
- [ ] Builder tests prove command and metrics capabilities exist before Init
  and retain the same object identity after Init.
- [ ] Existing tests that required metrics absence before Init are replaced
  with the new identity-preservation contract.

## D. Typed command capability and concurrency

- [ ] Replace the runtime authority's raw command-string contract with typed
  command name/arguments, typed results, operation ID, status, executor state,
  configured/active revision, generation, and diagnostic message.
- [ ] Keep terminal parsing confined to deprecated CLI/processor adapters.
- [ ] Configure, Init, and Start complete synchronously.
- [ ] Run returns an accepted asynchronous operation and executes on one owned,
  joinable worker.
- [ ] Natural completion and requested stop converge on the worker's single
  Stop/Join teardown sequence.
- [ ] Stop calls `RequestStop()` without waiting behind Run. Stop during
  initialized state creates the sole teardown worker when no run worker exists.
- [ ] Join observes the active teardown, never starts another teardown, and is
  idempotent after joined STOPPED.
- [ ] Concurrent and duplicate commands follow the normative transition table;
  no command worker or pending operation survives capability destruction.
- [ ] Command/lifecycle tests cover natural completion, cancellation, duplicate
  stop/join, stop during Run, operation completion, bounded retention, and
  exactly one executor teardown.

## E. HTTP and launcher integration

- [ ] `tools/graph-dashboard.cpp` creates one owning
  `shared_ptr<GraphCoordinator>`.
- [ ] Its single initial `Snapshot()` is passed to
  `GraphExecutorBuilder::WithGraphSnapshot`.
- [ ] The same coordinator and executor command/metrics capability handles are
  passed to `GraphHttpServer`; the server receives no raw executor pointer and
  constructs no second graph document.
- [ ] `graphx-dashboard` always creates the configured executor.
  `--enable-execution` is removed, and loading the page performs no Init or
  execution.
- [ ] `POST /api/v1/execution/configure` submits exactly one atomic current
  coordinator snapshot.
- [ ] Implement `GET /api/v1/execution/commands`,
  `POST /api/v1/execution/commands/{name}`, and
  `GET /api/v1/execution/operations/{operation_id}` with the normative
  `200`/`202 + Location`/`409`/`404` mappings.
- [ ] Existing lifecycle routes adapt to the same typed command capability.
  `GraphHttpServer` contains no direct calls to executor lifecycle methods.
- [ ] PATCH-before-configure initializes that exact new revision.
  PATCH-after-Init marks dirty without mutating the active graph.
- [ ] Blocking Run never blocks the HTTP accept/handler path or cooperative
  Stop.
- [ ] Real loopback tests cover discovery, synchronous and asynchronous
  results, operation lookup, dirty state, invalid transitions, and shutdown.

## F. Scope, migration, and architecture

- [ ] Preserve one `GraphCoordinator`, `GraphHttpServer`,
  `graphx-dashboard`, `/api/v1` namespace, and checked-in `index.html`.
- [ ] Add no receiver/FHSS management routes, runtime session, runtime owner,
  server, coordinator, dashboard executable, or dependency on the legacy
  embedded dashboard.
- [ ] Keep `graph-cli` built, installed, deprecated, warned, and
  compatibility-tested. Do not remove it in Phase 0.
- [ ] Preserve unrelated worktree changes and do not commit, push, or open a
  pull request.
- [ ] Use repository-local build/test paths; do not use `/private/tmp`.

## G. Validation gates

- [ ] Affected C++ targets build in repository C++26 mode with project warnings
  and `-Werror`.
- [ ] Focused coordinator, builder, executor, capability, HTTP, launcher, and
  deprecated CLI tests pass.
- [ ] Real source-tree executable/loopback tests pass.
- [ ] Full configured regression suite passes.
- [ ] `git diff --check` passes.
- [ ] Independent verifier reports PASS for every section with no unresolved
  blocking or high-severity findings.
