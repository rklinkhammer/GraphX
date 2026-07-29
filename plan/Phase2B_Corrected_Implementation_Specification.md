# Phase 2B corrected implementation specification

**Status:** Normative
**Architecture:** One generic GraphX dashboard
**Last reconciled:** 2026-07-29

This document is consistent with
[`Phase2B_Generic_Graph_Management.md`](Phase2B_Generic_Graph_Management.md).
Where an older Phase 2B prompt, check-in, or report conflicts with either
document, these two specifications prevail.

## Scope

Phase 2B provides a generic viewer and limited parameter editor for any GraphX
JSON graph. It is not an FHSS dashboard and must not depend on DSP or FHSS
types.

The implementation may:

- inspect a graph and its existing nodes;
- filter nodes by ID or type;
- replace an existing node's `node_config` in memory;
- explicitly save CLI session changes;
- control a supplied `GraphExecutor`;
- report unsupported executor operations truthfully.

The implementation must not:

- add or remove nodes;
- change node identity or type;
- infer or synthesize domain-specific topology;
- introduce `ReceiverGraphCoordinator`, `ReceiverGraphHttpServer`, or an
  FHSS-specific dashboard layer;
- silently report successful execution when no executor is available;
- persist HTTP edits implicitly.

## Components

### GraphCoordinator

`GraphCoordinator` is the thread-safe, non-owning in-memory boundary around a
graph JSON document. Reads return copies. `UpdateNodeConfig` is the only graph
mutation and affects only an existing node's `node_config`.

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
- `POST /api/v1/execution/{init,start,run,stop,join}`
- `POST /api/v1/execution/{pause,resume,step}`, returning `501` until the
  executor implements those operations.

Unknown routes return `404`; unsupported methods on known routes return `405`.
Malformed JSON and malformed framing return `400`; oversized input returns
`413`; invalid lifecycle transitions return `409`; execution endpoints return
`501` when the server has no executor.

`graphx-dashboard` is the generic launcher. It requires `--graph PATH`, accepts
`--port`, and defaults to inspection/editing with execution disabled.
`--enable-execution` opts into building an executor with the selected plugin
directory. It contains no domain-specific logic.

### GraphCli

`graph-cli` supports both an interactive stateful session and one-shot
inspection:

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

`pause`, `resume`, and `step` fail explicitly as unsupported. Execution
commands fail unless an executor was successfully initialized. Unsaved edits
must be saved before initialization because `GraphExecutorBuilder` consumes the
graph file.

## Acceptance criteria

- The build uses the repository's C++26 configuration and affected targets
  compile with project warnings plus `-Werror`.
- The generic dashboard can display any conforming graph without an executor.
- HTTP and CLI edits cannot add, remove, rename, or retype nodes.
- The checked-in UI can inspect, filter, view, and update existing
  `node_config` objects.
- REST routing and status codes are covered by real loopback socket tests.
- CLI file, query, edit, dirty-state, and truthful failure paths are tested.
- The generic CLI and dashboard launchers are built, installed, and have CTest
  smoke tests.
- Existing GraphX regression tests pass.
- `git diff --check` passes.

Security hardening beyond bounded local HTTP parsing and loopback binding is
not a Phase 2B gate under the GraphX engineering/research maturity model.
