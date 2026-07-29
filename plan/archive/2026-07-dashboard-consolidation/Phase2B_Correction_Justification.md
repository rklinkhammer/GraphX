# Phase 2B architecture correction

> Archived historical dashboard-planning record. The resulting corrected specifications remain active.

**Status:** Final correction
**Last reconciled:** 2026-07-29

The earlier receiver-specific Phase 2B proposal was a category error. Graph
management is infrastructure; FHSS interpretation belongs to DSP nodes,
plugins, graph configuration, and domain documentation.

## Decision

There is one generic management architecture:

```text
operator
   |
   +-- graphx-dashboard / GraphHttpServer
   |        |
   |        +-- GraphCoordinator -- in-memory graph JSON
   |        +-- optional GraphExecutor
   |
   +-- graph-cli
            |
            +-- GraphCoordinator -- explicit load/save
            +-- optional GraphExecutor
```

`GraphCoordinator`, `GraphHttpServer`, and `GraphCli` operate on ordinary
GraphX JSON. They do not know what FHSS is, do not construct a receiver
topology, and do not repair or infer missing ports. The graph file remains the
topology source of truth.

The following concepts are rejected and must not be restored:

- `ReceiverGraphCoordinator`;
- `ReceiverGraphHttpServer`;
- a parallel FHSS-specific dashboard implementation;
- management-layer logic that creates an acquisition detector bank or connects
  domain-specific ports;
- structural node CRUD in Phase 2B.

The allowed write surface is deliberately narrow: replace `node_config` on an
existing node, in memory. Identity, type, nodes, and edges remain structurally
immutable. The CLI may persist an explicitly requested save; HTTP edits are
not implicitly written to disk.

## Why this is consistent

- Generic infrastructure can display FHSS, SAR, audio, or test graphs without
  domain dependencies.
- Topology completeness is validated where a graph is authored or instantiated,
  rather than hidden inside the dashboard.
- A single checked-in web resource prevents divergent server and prototype
  pages.
- Optional executor injection preserves read-only inspection and makes
  execution availability explicit.

The normative requirements are
[`Phase2B_Generic_Graph_Management.md`](Phase2B_Generic_Graph_Management.md)
and
[`Phase2B_Corrected_Implementation_Specification.md`](Phase2B_Corrected_Implementation_Specification.md).
Older orchestration prompts and check-in reports are historical records only.
