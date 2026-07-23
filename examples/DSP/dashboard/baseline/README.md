# Phase 0 Dashboard Baseline

This directory characterizes the one FHSS dashboard across its in-place
frontend replacement. It is not a second implementation and defines no new
API. In Phase 1, `dashboard/dist` is the sole source frontend inventory.

`phase0-baseline.json` binds stable repository-controlled inputs: the
authoritative OpenAPI and JSON Schemas, recursive frontend inventory, selected
toolchain and policy files, installed layout, and the synthetic-only/no-HWIL
qualification boundary. Volatile responses, event sequences, generated IDs,
timestamps, executable binaries, operator reports, and qualification outputs
are deliberately represented by concise semantic expectations rather than
hashes or redundant fixtures.

`topology-identity.json` records all 75 stable configuration node IDs and all
137 exact port-aware edge IDs. Runtime, metric, and diagnostic correlations are
deferred until Phase 3; Phase 0 enables no live overlays.

Regenerate and verify the canonical baseline from the repository root:

```sh
python3 examples/DSP/dashboard/operator/phase0_baseline.py generate
python3 examples/DSP/dashboard/operator/phase0_baseline.py verify
```

For a clean installed tree, add
`--installed-root /absolute/path/to/install`. Installed verification recomputes
the frontend inventory, requires one `index.html`, compares installed API
contracts, and reconstructs topology from the installed receiver graph.

All evidence is synthetic-only. No HWIL, conducted RF, OTA, live-RF, or
production-RF qualification is available or implied.
