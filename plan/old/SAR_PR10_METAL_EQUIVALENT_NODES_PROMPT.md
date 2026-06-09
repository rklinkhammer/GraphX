# Prompt: PR10C Sidecar-Preserving Metal Coverage Expansion

Use this prompt to implement the next PR10 slice with two priorities:

1. Preserve SAR sidecar identity across transfer, kernel, and merge boundaries.
2. Increase Metal-equivalent stage coverage without duplicating libgpu-owned Metal nodes.

## Goal

Implement PR10C for the definitive SAR topology at examples/SAR/config/sar_stripmap_definitive.json with resolver-driven backend substitution, keeping portable intent types in JSON and proving both contract correctness and performance attribution quality.

## Current Gap Focus

Address the open work in current planning/checklist state:

1. Sidecar-preservation evidence is incomplete across full stage boundaries.
2. PR10B evidence-only stage is closed; PR10C carries algorithm-stage Metal coverage expansion, starting with the RangeWindow adapter path.
3. Transfer-stage substitutions exist, but broader stage coverage and parity evidence still need closure.

## Mandatory Constraints

1. Keep GraphExecutorBuilder plus JSON as the canonical runtime path.
2. Keep examples/SAR/config/sar_stripmap_definitive.json as the canonical topology artifact.
3. Keep edge_contract set to accel-token.
4. Keep resolver fields explicit in topology/config processing: execution_backend, backend_fallback_policy, resolver_diagnostics, edge_contract.
5. Keep portable intent node types in JSON; concrete backend types are selected by resolver/provider.
6. Do not add SAR-local duplicates for Metal nodes already owned by libgpu.
7. Keep SAR-specific semantics in examples/SAR and reusable backend mechanics in libgpu.
8. Keep SAR-specific resolver substitutions SAR-owned and dynamic; do not hard-code SAR node mappings in libgraph defaults.

## No-Duplicate Guardrail

Before adding any new Metal class or plugin, search for existing equivalents in libgpu. If an equivalent exists, reuse it via resolver mapping and SAR adapter wiring.

If a SAR-local Metal class is still proposed, require all of:

1. Written non-duplication justification.
2. Tests proving generic libgpu nodes cannot provide the same semantics.
3. SAR-owned resolver mapping path for selection.

## Required Implementation Scope

### 1) Sidecar-Preserving Work

Implement or harden adapter seams so these sidecar fields are preserved end-to-end:

1. stream_id
2. sequence_id
3. batch_id
4. aperture_id
5. pulse_range_start and pulse_range_count
6. tile_id and tile_count
7. frame and EOS markers
8. backend_id and queue_id metadata when available

No legacy payload-contract edge format may reappear on accel-token graph edges.

### 2) Increase Metal Node Coverage

Primary target: RangeWindow stage.

1. Evaluate DeviceTransformNodeMetal descriptor path for deterministic Hann window behavior.
2. If feasible, implement a SAR sidecar-preserving RangeWindow adapter that delegates to DeviceTransformNodeMetal.
3. If blocked, document blocker evidence and implement next highest-value coverage candidate that satisfies ownership and sidecar constraints.

Secondary target (only after gate conditions): RangeCompression stage.

1. Proceed only with explicit CPU parity gate and descriptor expressiveness proof.
2. Use generic libgpu primitives (DeviceKernelNodeMetal or DeviceTransformNodeMetal) plus SAR-owned parameter sidecars.

## Tests Required

Add or extend tests to prove all of the following:

1. Resolver substitution behavior for definitive topology.
2. Strict Metal failure behavior when concrete provider entries are unavailable.
3. allow_fallback behavior from the same definitive topology.
4. End-to-end sidecar identity preservation across source, split, H2D, algorithm stage, D2H, merge.
5. No payload-contract regression under accel-token mode.
6. Completion signaling and deterministic diagnostics behavior for expected complete runs.

For any newly Metalized algorithm stage, include:

1. CPU reference parity result.
2. Explicit tolerance policy.
3. Deterministic fixture.
4. Numeric comparison metric set (for example l_inf, rms, relative_l2, or image metric deltas).

## Performance Evidence Requirements

Benchmark must compare definitive topology under at least:

1. auto or stub or non-Metal baseline lane.
2. metal lane.

Report at minimum:

1. Wall-clock per-run ms, avg, min, max.
2. Graph build time.
3. Graph run time.
4. Graph lifecycle total time.
5. Direct baseline execute time where available.
6. H2D bytes, D2H bytes, kernel dispatch counts.
7. Queue wait/backpressure or proxy synchronization timing when available.
8. Resolved concrete node selections used by the run.
9. Attribution fields:
   - overhead_ms.graph_run_minus_baseline_median
   - overhead_attribution.cost_buckets.graph_overhead_ms
   - performance_claim_policy.speedup_basis
   - performance_claim_policy.disallow_lifecycle_total_as_speedup_basis

Speedup claim policy:

1. Do not claim speedup from lifecycle total metrics.
2. If no speedup is observed, provide bottleneck attribution and a concrete follow-up plan.

## Deliverables

1. Node mapping note for definitive topology with classification per node:
   - portable intent only
   - resolver-substituted Metal equivalent
   - SAR adapter delegating to libgpu primitive
2. File-by-file change summary grouped by:
   - examples/SAR (SAR-specific)
   - libgpu (reusable/common)
3. Test evidence snippets for resolver behavior, sidecar preservation, parity gates, and SAR lane pass.
4. Benchmark evidence snippets with attribution interpretation.
5. Updated checklist state in plan/SAR_PR10_CHECKLIST.md with exact boxes closed.

## Acceptance Gate

Work is complete only when all are true:

1. Sidecar-preservation checks pass end-to-end for the intended path.
2. At least one additional algorithm-stage Metal coverage item is implemented, or formally blocked with reproducible evidence and approved fallback scope.
3. No duplicate libgpu-owned Metal nodes are introduced in examples/SAR.
4. Definitive topology remains portable and executable through GraphExecutorBuilder plus JSON.

## Non-Goals

1. No framework-wide scheduler redesign.
2. No replacement of GraphExecutor plus JSON runtime contract.
3. No migration of SAR-specific policy into libgpu without demonstrated multi-consumer reuse.

## Execution Notes For Coding Agent

1. Inspect current code first; do not assume older split topology files are canonical.
2. Treat examples/SAR/config/sar_stripmap_definitive.json as source of truth.
3. Preserve compatibility of maintained SAR presets unless explicitly changed.
4. Prefer smallest safe refactor that improves reusable Metal coverage while preserving sidecar contracts.
