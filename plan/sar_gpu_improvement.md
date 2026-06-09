Use this exact prompt:

Prompt Title: PR10C Sidecar-Preserving Metal Coverage Expansion

Objective:
Implement the next SAR PR10 slice with two priorities:
1. Preserve SAR sidecar identity end to end across transfer, kernel, and merge boundaries.
2. Increase Metal-equivalent stage coverage without introducing duplicates of libgpu-owned Metal nodes.

Scope:
1. Keep GraphExecutorBuilder plus JSON as the canonical runtime path.
2. Keep the definitive SAR topology portable with resolver-driven backend substitution.
3. Keep accel-token as the edge contract.
4. Keep SAR-specific behavior in examples/SAR and reusable backend primitives in libgpu.
5. Keep SAR-specific resolver mappings dynamic and SAR-owned; do not hard-code SAR mappings in libgraph defaults.

No-duplicate rule:
1. Before adding any Metal node, verify whether libgpu already has an equivalent.
2. If equivalent exists, reuse it through resolver substitution and SAR adapters.
3. If a SAR-local Metal node is still proposed, require:
1. Written non-duplication justification.
2. Tests proving generic libgpu nodes cannot cover the semantics.
3. SAR-owned mapping path for selection.

Required implementation work:
1. Sidecar-preserving hardening:
1. Preserve stream_id, sequence_id, batch_id, aperture_id, pulse_range_start, pulse_range_count, tile_id, tile_count, frame marker, EOS marker, backend_id, and queue_id where available.
2. Ensure no legacy payload-contract format appears on accel-token graph edges.
2. Metal coverage expansion:
1. Preferred first target is RangeWindow via a SAR adapter over DeviceTransformNodeMetal.
2. If blocked, document reproducible blocker evidence and implement the next highest-value stage without violating ownership boundaries.
3. RangeCompression may proceed only after CPU parity gate and descriptor expressiveness gate are both satisfied.

Tests required:
1. Resolver substitution behavior for definitive topology.
2. Strict Metal expected-failure behavior when required providers are unavailable.
3. Allow-fallback success behavior from the same definitive topology.
4. End-to-end sidecar identity preservation across source, split, H2D, algorithm stage, D2H, and merge.
5. No payload-contract regression under accel-token mode.
6. Completion signaling and deterministic diagnostics checks for expected-complete runs.
7. For any newly Metalized stage:
1. CPU reference parity.
2. Explicit tolerances.
3. Deterministic fixture.
4. Numeric metrics such as l_inf, rms, relative_l2, or image metric deltas.

Performance evidence requirements:
1. Compare definitive topology in at least:
1. Auto or stub or non-Metal baseline.
2. Metal lane.
2. Report:
1. Per-run wall-clock times, avg, min, max.
2. Graph build time.
3. Graph run time.
4. Graph lifecycle total time.
5. Direct baseline execute time where available.
6. H2D bytes, D2H bytes, kernel dispatch counts.
7. Queue wait/backpressure or proxy synchronization timing where available.
8. Resolved concrete node selections.
9. Attribution fields:
1. overhead_ms.graph_run_minus_baseline_median
2. overhead_attribution.cost_buckets.graph_overhead_ms
3. performance_claim_policy.speedup_basis
4. performance_claim_policy.disallow_lifecycle_total_as_speedup_basis
3. Do not claim speedup from lifecycle total metrics.
4. If no speedup appears, provide bottleneck attribution and concrete follow-up actions.

Deliverables:
1. Node mapping note for each definitive node:
1. Portable intent only.
2. Resolver-substituted Metal equivalent.
3. SAR adapter delegating to libgpu primitive.
2. File-by-file change summary grouped into examples/SAR and libgpu.
3. Test evidence snippets for resolver behavior, sidecar preservation, parity gates, and SAR lane pass.
4. Benchmark evidence snippets with attribution interpretation.
5. Checklist update with exact boxes closed.

Acceptance gate:
1. Sidecar-preservation checks pass end to end.
2. At least one additional algorithm-stage Metal coverage item is implemented, or formally blocked with reproducible evidence and approved fallback scope.
3. No duplicate libgpu-owned Metal nodes are introduced.
4. Definitive topology remains portable and executable through GraphExecutorBuilder plus JSON.

If you want, I can also compress this into a shorter execution prompt version optimized for immediate agent run.