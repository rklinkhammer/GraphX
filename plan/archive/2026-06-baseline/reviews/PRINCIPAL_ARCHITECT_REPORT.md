# SAR Principal Architect Report

Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

Inputs:

- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`

Scope: architecture decision record only. No code implementation.

## 1. Final Recommendation

Adopt the simplifier target architecture with one tightening: GraphX SAR must have exactly one maintained runtime/GPU path, and that path must be tokenized end to end with `SarAccelControlToken`.

The current repository is close enough to preserve the accel-token foundation, but not close enough to call the architecture clean. The stripmap path is largely aligned. The CRSD/focused-image path is the architectural conflict: it introduces `SarPhaseHistoryControlMessage`, `FocusedImageResult`, mixed edge contracts, and an experimental Metal domain node beside the canonical SAR GPU lane.

The principal decision is:

- Keep `SarAccelControlToken` and `SarSidecar` as the runtime contract.
- Keep normalized SAR products, GOTCHA/CRSD conversion, SarPy validation, local real-data workflows, and comparison harnesses as product/tooling layers.
- Reject a second CRSD/focused-image runtime message system inside maintained GraphX execution.
- Reject production Metal claims for incomplete domain algorithms.
- Prefer deletion or quarantine over compatibility.

Correctness and truth-in-labeling come before performance and convenience.

## 2. Accepted Proposals

- Accept `SarAccelControlToken = AccelControlToken<SarSidecar>` as the only maintained SAR runtime/GPU edge type.
- Accept `SarSidecar` as the sole SAR identity, lineage, ordering, diagnostics, timing, backend, checksum, and artifact-reference carrier inside runtime execution.
- Accept opaque transport semantics for `host_ptr`, `device_ptr`, and `ready_event`.
- Accept `NormalizedSarProduct` as an I/O/product conversion model, not a graph runtime payload.
- Accept CRSD/GOTCHA readers, writers, manifests, reports, checksums, and sidecar JSON schemas as artifact/product contracts.
- Accept external baselines as evidence and comparison harnesses only.
- Accept generic libgpu transfer, sync, memory, and kernel primitives as the target GPU execution substrate.
- Accept SAR-owned descriptor construction around generic GPU primitives.
- Accept local-only real-data workflows when explicitly gated and excluded from required CI.
- Accept truth-in-labeling inventory for Metal nodes, including explicit `unsupported` and `experimental-incomplete` states.
- Accept moving historical/planning artifacts out of active product surfaces.

## 3. Rejected Proposals

- Reject dual SAR runtime paths.
- Reject maintained graph edges using `SarPhaseHistoryControlMessage`.
- Reject maintained internal graph edges using `FocusedImageResult`.
- Reject configs that claim `"edge_contract": "accel-token"` while executing non-token graph edges.
- Reject SAR-specific H2D/D2H transfer nodes as the final architecture.
- Reject monolithic SAR GPU wrappers that hide generic GPU transfer/kernel stages.
- Reject resolver mappings that imply backend substitution while resolving every backend to the same SAR concrete node.
- Reject backend control duplication across top-level resolver settings, node `backend`, node `backend_id`, and node-specific `execution_backend` fields.
- Reject `CrsdFocusedImageTransformMetalNode` as a production node until it implements complete native Metal focused-image formation end to end.
- Reject unsupported Metal plugins from active supported-node claims.
- Reject SarPy, gotcha-back, ISCE3, or any external package as a source of GraphX core runtime contracts.
- Reject compatibility shims for obsolete SAR message or node names.
- Reject naming or docs that imply intermediate GraphX artifacts are standards-compliant CRSD.

## 4. Required PR Sequence

1. Quarantine non-runtime and historical surfaces.
   Move planning-era artifacts, stale verifier/implementer reports, tracked caches, and historical-only tests/docs out of active product surfaces.

2. Establish active config taxonomy.
   Classify every `examples/SAR/config` file as canonical runtime, fixture, local-only, or delete. Remove or quarantine configs with mixed token/non-token contracts.

3. Tighten runtime contract guardrails.
   Add or update tests so maintained SAR runtime configs cannot contain non-`SarAccelControlToken` graph edges under `edge_contract: "accel-token"`.

4. Bound CRSD/focused-image lane.
   Decide whether CRSD focused-image processing is product/tooling or maintained runtime. Until tokenized, keep it out of the canonical runtime path and production claims.

5. Replace SAR-specific transfer endpoint.
   Introduce the sidecar-preserving generic GPU transfer boundary, then remove active dependence on SAR-specific H2D/D2H as the final path.

6. Replace monolithic SAR GPU kernel wrapper.
   Move toward SAR descriptor preparation plus topology-visible generic GPU kernel execution.

7. Simplify backend selection.
   Collapse duplicate backend fields and remove resolver mappings that create misleading backend diagnostics.

8. Enforce Metal truth-in-labeling.
   Gate or remove unsupported and experimental-incomplete Metal nodes from production configs; retain only honest inventory/test exposure.

9. Normalize artifact/tool boundaries.
   Keep CRSD/GOTCHA conversion, SarPy validation, image comparison, and local workflows as explicit artifact harnesses outside `libgraph` and `libgpu`.

10. Restore example observability.
    Ensure `examples/SAR/main.cpp` remains tested and reports meaningful runtime/performance diagnostics in the canonical path.

## 5. Non-Negotiable Invariants

- Correctness outranks determinism.
- Determinism outranks architecture.
- Architecture outranks observability.
- Observability outranks performance.
- Performance outranks convenience.
- There is exactly one maintained SAR runtime/GPU path.
- Every maintained SAR runtime/GPU edge uses `SarAccelControlToken`.
- `SarSidecar` is the only SAR identity carrier in runtime execution.
- Transport handles are never identity.
- Generic libgpu remains free of SAR, GOTCHA, CRSD, SarPy, and external baseline concepts.
- SAR-specific code may prepare descriptors, metadata, validation, diagnostics, and artifacts, but it may not create a parallel GPU runtime architecture.
- Resolver diagnostics must describe actual substitution, not aspirational backend intent.
- Metal-named domain nodes must either execute the advertised native algorithm completely or be explicitly non-production.
- Simulated capabilities and CPU fallback do not prove native Metal algorithm support.
- CRSD compliance is literal; misleading CRSD output is worse than failure.
- Local-only real-data workflows remain opt-in and CI-safe by default.
- Backward compatibility with obsolete SAR architecture is not required.
- Compatibility shims are forbidden.

## 6. External Baseline Boundaries

- External SAR packages are comparators, validators, or artifact generators only.
- External packages must not define GraphX graph contracts, node APIs, token structure, sidecar structure, resolver behavior, or libgpu capability design.
- SarPy may validate CRSD products and may remain a local/external writer harness only if labeled as such.
- SarPy-generated or surrogate images are evidence artifacts, not proof that GraphX owns a fully focused native SAR algorithm.
- gotcha-back and ISCE3 may support local comparison experiments, not runtime architecture.
- External baseline execution must remain outside `libgraph` and `libgpu`.
- CI-safe baseline tests must use tiny deterministic fixtures or checked-in derived artifacts with clear provenance.
- Real GOTCHA/OpenSAR/large-dataset workflows must remain local-only unless explicitly promoted with licensing, determinism, and runtime safeguards.
- No external package integration may force GraphX to imitate that package's internal data model.
