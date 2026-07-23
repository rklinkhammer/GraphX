# GraphX FHSS frontend

This directory builds the single dashboard served at `/`. Node.js, npm, Vite,
and TypeScript are build-time tools only; the installed product contains only
`dist/index.html` and hashed files below `dist/assets/`.

The implementation separates responsibilities as follows:

- `domain.ts` validates the existing `/api/v1/fhss` graph contract;
- `api.ts` owns HTTP access without changing public API shapes;
- `transportState.ts` and `useEventTransport.ts` own ordered WebSocket,
  polling fallback, gap detection, and coherent resynchronization behavior;
- `topology.ts` maps configuration identities and exact ports to a stable
  presentation model;
- `layout.ts` applies deterministic ELK layered layout with fixed port order;
- `GraphView.tsx`, `TopologyTable.tsx`, and `Inspector.tsx` provide graphical,
  semantic, and detail representations; and
- `Operations.tsx` is the FHSS-specific operator workbench.

Topology is read-only. Dragging is local presentation state only. Runtime,
metric, and diagnostic correlation overlays remain deferred until Phase 3.

Build and test from this directory:

```sh
npm ci --ignore-scripts --offline
npm run format:check
npm run typecheck
npm test
npm run build
```

All qualification is synthetic-only. No HWIL or RF qualification is implied.
