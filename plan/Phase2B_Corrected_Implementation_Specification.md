# Phase 2B corrected implementation specification

**Status:** Normative
**Architecture:** One generic GraphX dashboard
**Last reconciled:** 2026-07-29

This document is consistent with
[`Phase2B_Generic_Graph_Management.md`](Phase2B_Generic_Graph_Management.md).
Where an older Phase 2B prompt, check-in, or report conflicts with either
document, these two specifications prevail.

The runtime lifecycle and launcher rules in
`GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md` supersede the original optional
executor handoff in this document. In particular, the dashboard always owns a
configured executor, obtains revisioned snapshots from `GraphCoordinator`,
uses `ConfigureGraph → Init → Start → Run → Stop → Join`, creates command and
metrics capabilities before `Init`, removes `--enable-execution`, and treats
`graph-cli` as deprecated pending verified web/REST parity.

## Scope

Phase 2B provides a generic viewer and limited parameter editor for any GraphX
JSON graph. It is not an FHSS dashboard and must not depend on DSP or FHSS
types.

The implementation may:

- inspect a graph and its existing nodes;
- filter nodes by ID or type;
- replace an existing node's `node_config` in memory;
- explicitly save CLI session changes;
- configure and control the dashboard's mandatory `GraphExecutor`;
- report unsupported executor operations truthfully.

The implementation must not:

- add or remove nodes;
- change node identity or type;
- infer or synthesize domain-specific topology;
- introduce `ReceiverGraphCoordinator`, `ReceiverGraphHttpServer`, or an
  FHSS-specific dashboard layer;
- initialize or execute the configured executor merely because the page opens;
- persist HTTP edits implicitly.

## Components

### GraphCoordinator

`GraphCoordinator` is the thread-safe owning in-memory boundary around a
graph JSON document. Reads return copies. `UpdateNodeConfig` is the only graph
mutation and affects only an existing node's `node_config`. It produces atomic
snapshots containing the document, monotonic revision, and deterministic
content identity.

### GraphHttpServer

`GraphHttpServer` serves the checked-in
`libgraph/resources/web/index.html`, not a second embedded page. It binds to
IPv4 loopback and exposes:

- `GET /api/v1/graph`
- `GET /api/v1/nodes`
- `GET /api/v1/nodes/{id}`
- `GET /api/v1/nodes/type/{type}`
- `PATCH /api/v1/nodes/{id}`
- `GET /api/v1/execution/state`
- `POST /api/v1/execution/{configure,init,start,run,stop,join}`
- `POST /api/v1/execution/{pause,resume,step}`, returning `501` until the
  executor implements those operations.

Unknown routes return `404`; unsupported methods on known routes return `405`.
Malformed JSON and malformed framing return `400`; oversized input returns
`413`; invalid lifecycle transitions return `409`; operations not implemented
by the executor return `501`.

`graphx-dashboard` is the generic launcher. It requires `--graph PATH`, accepts
`--port` and `--plugins`, and always creates one configured executor.
`--enable-execution` is removed. Opening the page performs no initialization
or execution. The builder creates the executor shell without loading plugins,
constructing nodes, or building a `GraphManager`; that work begins during
explicit `Init`.

### GraphCli

`graph-cli` supports both an interactive stateful session and one-shot
inspection:

> **Deprecated:** This section records the compatibility contract of the
> existing implementation. New work must target the `graphx-dashboard` web
> command surface and REST automation described in
> `plan/GRAPHX_GENERIC_GRAPHICAL_DASHBOARD_PLAN.md`. Removal waits for verified
> browser command, graph-export, and headless automation parity.

```text
load PATH
save PATH
show [--format table|json]
list-nodes [--type TYPE] [--format table|json]
get-node --id ID [--format table|json]
update-node --id ID --config JSON
node-count
node-ids
nodes-by-type TYPE
plugins DIRECTORY
init
start
run
stop
join
state
```

`pause`, `resume`, and `step` fail explicitly as unsupported. During migration,
CLI lifecycle operations use the same typed command capability as HTTP.
Dashboard initialization consumes an atomic coordinator snapshot and does not
require saving edits to disk.

## Acceptance criteria

- The build uses the repository's C++26 configuration and affected targets
  compile with project warnings plus `-Werror`.
- The generic dashboard can display any conforming graph with its executor
  configured but not initialized or running.
- HTTP and CLI edits cannot add, remove, rename, or retype nodes.
- The checked-in UI can inspect, filter, view, and update existing
  `node_config` objects.
- REST routing and status codes are covered by real loopback socket tests.
- CLI file, query, edit, dirty-state, and truthful failure paths are tested.
- The deprecated CLI remains built, installed, warned, and compatibility-tested
  only until the web command, export, and REST automation removal gates pass.
- Existing GraphX regression tests pass.
- `git diff --check` passes.

Security hardening beyond bounded local HTTP parsing and loopback binding is
not a Phase 2B gate under the GraphX engineering/research maturity model.
