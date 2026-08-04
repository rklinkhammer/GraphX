# GraphX Generic Dashboard Phase 4 Acceptance Checklist

Status: **BLOCKED BEFORE IMPLEMENTATION**  
Audit baseline: `e38de59bff78908e8084dea13e12160305a645b7` on `main`  
Prepared: 2026-08-04

This checklist translates Phase 4 into file-level, independently verifiable
acceptance criteria. It preserves the single `GraphCoordinator`, configured
`GraphExecutor`, `GraphHttpServer`, generic dashboard, graph model, and
`/api/v1` namespace established in Phases 0-3.

No Phase 4 implementer may be assigned until both prerequisites in P1-P2 are
closed and the generic metric identity contract in P3 is explicitly
authorized. An unchecked item is not a waiver.

## 1. Audit record

- Starting branch/commit: `main` at `e38de59bff78908e8084dea13e12160305a645b7`.
- Preserved pre-existing changes at audit start:
  `docs/graphx_dashboard_phase3_operator_test.md`,
  `libgraph/resources/web/assets/graphx-dashboard.js`,
  `libgraph/web/src/App.test.tsx`, `libgraph/web/src/App.tsx`, and
  `libgraph/web/test/browser-phase3.mjs`; the Phase 4 orchestration prompt was
  untracked. These changes must not be discarded or overwritten.
- Host tools: Node.js 26.5.0, npm 11.17.0, CMake 4.4.0, Ninja 1.13.2,
  Apple Clang 21.0.0, jq 1.7.1, Firefox 153.0.1.
- Phase 3 automated final-tree evidence: frontend 162/162; source/installed
  Firefox matrix 11/11 with 33 screenshots; Docker sanitizer 9/9; enabled
  native CTest 37/37; six documented disabled tests; asset parity and
  `git diff --check` passed.
- Phase 3 independent result: 81/83. G8 and consequently I10 remain open
  because the human evidence and observation cells were blank.
- The human procedure is now the plain-language eight-check worksheet in
  `docs/graphx_dashboard_phase3_operator_test.md`, Section 9. Its results are
  intentionally not pre-filled.

## 2. Mandatory prerequisites

- [ ] **P1 Human Phase 3 evidence.** A human completes checks A-H in
  `docs/graphx_dashboard_phase3_operator_test.md` with an explicit
  `PASS`/`FAIL`/`N/A`, a real evidence path, and an observation for every row.
  Any `FAIL` blocks Phase 4; `N/A` includes a concrete applicability reason.
- [ ] **P2 Independent Phase 3 closure.** A verifier confirms the evidence is
  human-produced, files exist and support the observations, and marks Phase 3
  G8 and I10 PASS. Automated browser output alone is insufficient.
- [x] **P3 Generic metric identity authorization.** On 2026-08-04, after the
  orchestrator reported that this authorization was required, the user
  explicitly instructed the orchestrator to execute the Phase 4 prompt again.
  This authorizes the
  minimal generic runtime contract in Section 3. Until then, no metric route,
  subscriber, snapshot, aggregation, or overlay implementation may begin.

## 3. Metric identity prerequisite decision

### 3.1 Current evidence and decision

The existing contract is insufficient for truthful overlays:

- `libgraph/include/metrics/MetricsEvent.hpp` carries a timestamp, diagnostic
  `source`, `event_type`, and string map. It carries no authoritative node ID,
  edge tuple, graph generation, metric identity, declared type/unit/semantics,
  or counter epoch.
- `libgraph/include/metrics/NodeMetricsSchema.hpp` carries `node_name`,
  `node_type`, JSON schema, event types, and display hints, but no canonical
  graph identity or generation.
- `libgraph/src/policies/MetricsPolicy.cpp` binds callbacks by runtime
  descriptor name and forwards publisher events unchanged. It does not stamp
  `GraphManager::GetCanonicalNodeIds()` or the executor generation.
- `libgraph/include/capabilities/MetricsCapability.hpp` retains schemas and raw
  subscribers; `ResetGeneration()` only clears schemas. It has no generation
  or current-value contract.
- The policy's `ActiveQueue<MetricsEvent>` is constructed with capacity zero,
  which means unbounded retention.
- Exact canonical node IDs do exist, in runtime-node order, through
  `GraphManagerCore::GetCanonicalNodeIds()`. The `GraphExecutor` and command
  capability already expose graph generation. No generic exact edge-metric
  publisher binding currently exists.

Therefore `source`, node name/type, display label, and string prefixes may not
be used to correlate metrics. Edge metrics must remain unavailable unless a
publisher is explicitly bound to the complete authoritative edge tuple.

### 3.2 Smallest authorized generic extension

Authorization of P3 permits only these changes to the existing metrics path:

1. Add a value-semantic target identity to schema/sample records:
   - node target: exact canonical `node_id`;
   - edge target: exact source node plus source numeric/named port and exact
     target node plus target numeric/named port;
   - every target also carries the active `graph_generation`.
2. During `MetricsPolicy::OnInit`, bind each node callback immutably by runtime
   index to `GraphManager::GetCanonicalNodeIds()[index]` and the current graph
   generation. Stamp/validate that binding at the policy boundary; publisher
   `source` remains diagnostic and is never an identity.
3. Require schema and sample to share a stable `metric_id`, target, generation,
   scalar type, unit, semantics, aggregation rule, availability rule, and,
   for counters, `counter_epoch`.
4. Provide an explicit exact-edge binding path using the full tuple. Until a
   publisher supplies it, expose the edge metric as `unavailable` with reason
   `unbound_edge_identity`; never infer activity from adjacent nodes.
5. Make `MetricsCapability::ResetGeneration(generation)` atomically clear old
   schemas/current values/rate state and publish the new generation while
   preserving the pre-`Init` capability object and live subscribers.
6. Bound the existing policy queue and all accepted schema/sample data using
   Section 4. Rejection increments a bounded diagnostic counter and never
   blocks graph execution.
7. Make subscriber registration idempotent and unregistration wait for any
   in-flight callback without holding a lock across arbitrary callback work.

This is an extension of the existing metrics value objects, policy, and
capability—not a second service, topology authority, session, publisher, API
namespace, or domain adapter. Legacy `libgraph/src/dashboard`,
`GraphRuntimeSession`, and `examples/DSP/dashboard` are not implementation
dependencies or identity authorities.

## 4. Locked numeric bounds

| Contract | Bound |
| --- | ---: |
| Server command-operation retention and browser command history | 128 records each |
| Operation polling cadence | 500 ms; one in flight; terminal/404/unmount aborts |
| Discovered command arguments | 32 fields per command |
| Command field name / string value | 128 / 1,024 UTF-8 bytes |
| Existing HTTP request body / header / path | 1,048,576 / 65,536 / 2,048 bytes |
| Metrics policy queue | 4,096 events, non-blocking rejection on full |
| Fields in one metric event | 64 |
| One encoded metric event | 16,384 bytes |
| Target node ID / port name / metric ID / unit / reason | 256 / 128 / 128 / 32 / 256 UTF-8 bytes |
| Registered target schemas / descriptors / current values | 2,048 / 4,096 / 4,096 |
| Metrics per target schema | 64 |
| Encoded schemas / encoded current values | 262,144 / 524,288 bytes |
| `GET /api/v1/metrics` encoded body | 1,048,576 bytes |
| Scalar string value | 1,024 UTF-8 bytes |
| Browser metrics polling cadence | 1,000 ms; one in flight |
| Browser stale threshold | 3,000 ms since last accepted snapshot |
| Browser retained metric snapshots | 1 current snapshot; no history |
| Animated exact edges | 256; excess remains text/static |
| Groups / nesting / direct membership | 256 / 12 / 2,048 |
| Total group memberships / edges per bundle | 10,000 / 10,000 |
| Layout items per call / cumulative layout work / visible detail | 20,000 / 100,000 / 25,000 |
| Layout calls for a metric-only update | 0 |
| Browser export encoded envelope | 16,777,216 bytes |
| HTTP request workers / pending requests | 8 / 16 |
| Socket send/receive timeout | existing 5 seconds; no detached timeout worker |

Over-bound schema/sample input is rejected with a bounded counter and concise
reason. If a complete snapshot would exceed its byte budget, the resource
returns a bounded `503 snapshot_unavailable` response; it never truncates an
identity, schema, value, or JSON token and never substitutes zero.

## 5. Locked wire and interaction contracts

### 5.1 Commands

- Discovery remains `GET /api/v1/execution/commands`; submission is
  `POST /api/v1/execution/commands/{name}`; accepted asynchronous work follows
  the returned `Location` under `/api/v1/execution/operations/{id}`.
- Existing execution buttons and the new in-page command palette use one
  shared typed frontend client. The legacy `/api/v1/execution/{command}` path
  remains compatibility-only; new UI code never calls it.
- Controls are generated only for declared bounded boolean, signed/unsigned
  integer, finite number, string enum, and bounded string arguments.
  Unsupported schemas display `unsupported` and cannot submit.
- No shell, terminal, command line, executable/environment/filesystem field,
  arbitrary URL, or unrestricted JSON control exists.
- Browser history is FIFO, page-memory-only, capped at 128, and stores command,
  operation ID, status, state, revisions, generation, and concise diagnostic.
  Eviction never moves focus.

### 5.2 Browser export

The browser downloads one envelope obtained from one atomic
`GraphCoordinator::Snapshot()`:

```json
{
  "artifact": "graphx.graph-export",
  "version": 1,
  "coordinator_revision": 0,
  "content_identity": "<existing deterministic snapshot identity>",
  "graph": {}
}
```

`GET /api/v1/graph` remains the sole graph resource and is extended
backward-compatibly with snapshot revision/content identity; no export route is
added. The `graph` value round-trips every topology and authored presentation
field after JSON parse and excludes metrics, execution, history, selection,
and preferences. Filename is
`graphx-graph-r<revision>-<first-12-identity>.json`, sanitized to ASCII and
capped at 96 bytes. The browser revokes its Blob
URL after initiating download and reports success/failure in the existing live
status surface.

### 5.3 Metrics resource

Exactly one new resource is authorized: `GET /api/v1/metrics`. Its envelope is:

```json
{
  "success": true,
  "data": {
    "schema_version": 1,
    "graph_generation": 1,
    "active_revision": 0,
    "snapshot_sequence": 1,
    "snapshot_time": "RFC3339 UTC",
    "availability": {"state": "available|unavailable|stale", "reason": "..."},
    "schemas": [],
    "values": [],
    "diagnostics": {"rejected": 0, "dropped_queue_full": 0}
  }
}
```

Schemas and values are deterministically sorted by target kind, complete
target identity, then metric ID. Each schema declares scalar type, unit,
semantics (`gauge`, `monotonic_counter`, or `state`), aggregation (`sum`,
`min`, `max`, `average`, `rate`, or `none`), and availability rule. Each value
includes the same identity fields, sample time, availability/reason, and a
typed scalar; counters include epoch. JSON numbers must be finite. Missing,
unknown, stopped, reset, old-generation, mismatched, and uncorrelated values
are unavailable, never zero.

A rate exists only after two increasing, ordered samples with identical target,
metric, unit, generation, and epoch. A decrease, epoch change, generation
change, or non-positive time delta invalidates it until another valid pair.

### 5.4 Subscriber, polling, aggregation, and accessibility

- `GraphHttpServer` directly implements `IMetricsSubscriber`, registers once
  with the mandatory pre-`Init` `MetricsCapability` while callback storage is
  alive, and unregisters before workers, storage, capability handles, or
  executor teardown. Repeated/failed/never-started lifecycle paths are safe.
- Callback work validates/copies one bounded event only. It performs no socket
  I/O, response construction, JSON serialization, layout, capability re-entry,
  event-rate logging, or history append. HTTP copies under a short lock and
  serializes after unlocking.
- Browser polling is single-flight, abortable, generation-aware, and stops on
  unmount or **Pause runtime updates**. Pause does not pause the executor and
  retains one snapshot labelled with capture time. Out-of-order/old-generation
  responses are ignored.
- Groups and bundles aggregate only explicitly enumerated exact members with
  identical type, unit, semantics, generation, epoch (where applicable), and
  compatible declared aggregation. Otherwise display available/member counts
  and an unavailable reason.
- Canvas, semantic topology, and inspector share the existing graph model and
  selection. Exact textual identity/value/unit/time/reason remains available
  without color or motion. Reduced motion disables animation. Metric updates
  cause zero ELK/layout calls and no PATCH or command requests.
- Live announcements are limited to command acceptance/completion, export
  result, metrics loss/recovery, pause/resume, and generation invalidation;
  individual samples are not announced.

## 6. File-level implementation checklist

### A. Architecture and runtime identity

- [ ] **A1** `MetricsEvent.hpp` and `NodeMetricsSchema.hpp` implement the
  authorized exact target/generation/descriptor contract without domain rules.
- [ ] **A2** `MetricsPolicy.hpp/.cpp` binds runtime nodes by index to canonical
  IDs and generation, bounds the queue, and rejects unstamped/old/over-bound
  input without blocking execution.
- [ ] **A3** `MetricsCapability.hpp` preserves pre-`Init` object identity,
  atomically resets generation state, prevents duplicate registration, and
  makes in-flight unsubscribe/destruction safe.
- [ ] **A4** Duplicate names/types/labels cannot alias. Edge values require the
  complete numeric/named port tuple. Unbound edge data is unavailable.
- [ ] **A5** `GraphCoordinator`, configured mandatory `GraphExecutor`, and the
  `ConfigureGraph -> Init -> Start -> Run -> Stop -> Join` authority remain
  singular and unchanged except for the approved identity plumbing.

### B. HTTP adapter, export, and subscriber lifetime

- [ ] **B1** `GraphHttpServer.hpp/.cpp` directly implements
  `IMetricsSubscriber` and contains no executor pointer/direct lifecycle call.
- [ ] **B2** Registration/removal is exactly once and safe for failed/repeated
  Start/Stop, never-started destruction, concurrent publish/unregister,
  request shutdown, and executor destruction.
- [ ] **B3** `GET /api/v1/metrics` is the only new route and implements the
  locked method/status/schema/order/byte/availability contract.
- [ ] **B4** `GET /api/v1/graph` uses one coordinator snapshot and supplies the
  revision/content identity needed by the locked browser export envelope.
- [ ] **B5** Existing worker/request/socket bounds and static-root containment
  remain intact; no event stream, domain route, server-side export write, or
  second server is introduced.

### C. Typed frontend and runtime presentation

- [ ] **C1** New typed command client/palette and old execution buttons use
  discovery plus typed submission, structured bounded controls, 500 ms
  single-flight operation polling, truthful expiry, and 128-record history.
- [ ] **C2** No shell/free-form execution surface or forbidden request path is
  present; server state remains authoritative for concurrency/transitions.
- [ ] **C3** Browser export creates the exact locked envelope/filename, enforces
  its bound, revokes object URLs, and excludes runtime/browser state.
- [ ] **C4** Metric response parsing is bounded, exact-identity and
  generation-safe; polling/pause/stale behavior matches Section 5.
- [ ] **C5** Node/edge/group/bundle canvas, semantic, and inspector views are
  synchronized. Unsupported or incompatible data is explicitly unavailable.
- [ ] **C6** Metric updates preserve search/isolation/collapse/selection/focus,
  retain one snapshot, animate at most 256 edges, and invoke layout zero times.
- [ ] **C7** New controls preserve keyboard, focus, name/role/value, status,
  non-color, reduced-motion, 320 CSS pixel, 200% zoom, text-spacing, contrast,
  and target-size behavior.
- [ ] **C8** Generated `libgraph/resources/web` assets exactly reproduce the
  pinned `libgraph/web` source and require no CDN or installed Node runtime.

### D. Independent tests and operator evidence

- [ ] **D1** C++ tests independently cover identity/generation, duplicate
  names/types, exact ports, schema/value/rate/bounds, registration races,
  snapshot consistency, export atomicity, configured-state inspection, and
  command equivalence/transitions/expiry/teardown.
- [ ] **D2** Frontend tests use independent fixtures/oracles for commands,
  exports, metrics, generation, rates, aggregation, polling, pause, focus,
  semantic state, bounds, and zero relayout—not production code as sole oracle.
- [ ] **D3** Source and clean-installed Firefox tests cover command parity,
  asynchronous Run/Stop, export, metrics, pause/stale/generation, exact
  identities, request allowlists, no console errors, desktop/narrow/200%-zoom,
  both motion modes, and text spacing.
- [ ] **D4** Synthetic minimal, nested, complex, SAR 21/23, and FHSS 75/137
  graphs exercise the same generic implementation; production code contains
  no SAR/FHSS/detector/frequency/IQ/message/truth/job/investigation rule.
- [ ] **D5** `docs/graphx_dashboard_phase4_operator_test.md` begins with a
  fresh clone and first-principles native build, covers source/installed use,
  objective REST/browser/accessibility checks, and states all data is
  synthetic or recorded with no HWIL.
- [ ] **D6** A human completes the Phase 4 changed-path worksheet with actual
  status, evidence, and observations; an independent verifier reviews it.

### E. Final qualification and exclusions

- [ ] **E1** Clean pinned frontend install, typecheck, unit/component tests,
  production build, asset reproduction, and source/install SHA-256 parity pass.
- [ ] **E2** A repository C++26 warnings-as-errors build and all focused
  command/metrics/HTTP tests pass.
- [ ] **E3** Supported Docker ASan/LeakSanitizer/UBSan subscriber/publication/
  shutdown tests pass using named volumes and documented parallelism.
- [ ] **E4** All enabled configured native CTests pass; every disabled/local/
  data/hardware gate is enumerated with its exact reason.
- [ ] **E5** Architecture scans reject a second authority/runtime/dashboard,
  legacy embedded dashboard imports, `/api/v2`, domain routes, streams, shell
  execution, CDN, server export, new Docker image, and premature CLI removal.
- [ ] **E6** Native non-Docker build/test/install/operation remains supported;
  no virtual environment, `/private/tmp`, dependency upgrade, or silent host
  package installation is introduced.
- [ ] **E7** `graph-cli` remains deprecated and present; no Phase 5 work is
  included.
- [ ] **E8** Final-tree browser/sanitizer/regression evidence is rerun after the
  last remediation, `git diff --check` passes, and unrelated changes remain.
- [ ] **E9** Independent verifier reports explicit PASS for P1-P3 and A1-E8,
  with no unresolved blocking/high finding, before Phase 4 is complete.

## 7. Orchestration state

- Orchestrator audit/checklist: complete.
- Phase 3 human prerequisite: blocked pending operator evidence.
- Generic metric identity contract: authorized by the user's renewed Phase 4
  execution instruction; implementation remains gated on P1-P2.
- Implementer: not assigned, as required by the orchestration prompt.
- Verifier: not assigned; independent verification follows implementation.
- Commit/push/PR: not performed.
