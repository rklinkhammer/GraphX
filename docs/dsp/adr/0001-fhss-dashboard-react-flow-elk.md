# ADR 0001: Modernize the FHSS Dashboard with React Flow and ELK.js

- Status: Accepted for implementation after Phase 0
- Date: 2026-07-22
- Scope: FHSS dashboard presentation only

## Context

The current `examples/DSP/dashboard/index.html` is the qualified dashboard and
an effective behavioral prototype, but its JSON-oriented topology presentation
cannot provide rich port-aware GraphX nodes, grouped FHSS stages, or bounded
edge activity. Earlier planning selected Cytoscape.js as an initial default.

## Decision

React Flow with ELK.js, TypeScript, and Vite supersedes the historical
Cytoscape.js frontend choice. React Flow supplies typed custom node content,
explicit port handles, accessibility primitives, and bounded custom edges.
ELK.js supplies deterministic port-aware layered layout. The FHSS presentation
will add structural detector-bank grouping without changing graph execution.

`V2` names the modernization initiative only. It is not an API version or a
second implementation. GraphX serves exactly one dashboard at `/`, installs
one current asset set, and retains `/api/v1/fhss` as its sole application API
namespace. There will be no `/api/v2`, `/legacy`, `/v2`, runtime UI selector,
duplicate route tree, compatibility adapter, or dormant packaged dashboard.

Phase 0 records policy and characterization only. The existing `index.html`
remains the sole UI until a later phase replaces it in place. Rollback means
rebuilding or reinstalling the last qualified source or release artifact.

Generic dashboard extraction remains deferred until a second domain provides
independent requirements. The initial GraphX topology concepts remain part of
the FHSS dashboard application.

## Consequences

- The C++ server, OpenAPI 3.1 document, `/api/v1/fhss` semantics, runtime owner,
  event ordering/recovery, and synthetic-only evidence boundaries remain.
- Node.js and the frontend toolchain are build-time dependencies only after
  frontend compilation is introduced; dashboard-disabled builds stay free of
  them.
- Frontend assets are self-hosted, recursively inventoried, hash-bound,
  bounded, policy-checked, and compared between source and clean install.
- Configuration topology identity is characterized before rendering. Runtime
  metric correlation remains later Phase 3 work.
- Security hardening and human WCAG campaigns follow the GraphX maturity model
  and are not manufactured as Phase 0 baseline evidence.

## Rejected alternatives

- **Cytoscape.js as the primary renderer:** less suitable for metrics-rich HTML
  nodes and explicit operator-facing port controls.
- **Parallel prototype and modernized UIs:** violates the single-implementation
  rule and doubles qualification and security surface.
- **A new API version:** the presentation change does not require or authorize
  an API semantic break.
- **A generic frontend framework now:** requirements from one domain are
  insufficient to establish a reusable public abstraction.
