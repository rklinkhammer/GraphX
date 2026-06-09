# Prompt: Implement METAL-Equivalent Nodes For Definitive SAR Topology

Use this prompt to implement METAL-equivalent execution for the nodes used by the definitive SAR topology while preserving GraphX runtime contracts and proving performance gains.

## Objective

Implement METAL-equivalent node support for the pipeline in examples/SAR/config/sar_stripmap_definitive.json, using ResolverConfig/runtime substitution where appropriate.

Design rule:

1. Keep SAR-specific adapters and SAR-only behavior in examples/SAR.
2. Move common reusable METAL-capable node functionality into libgpu.
3. Do not create SAR-local node classes or plugins that duplicate existing libgpu Metal nodes. SAR adapters may delegate to libgpu nodes, but concrete generic Metal nodes remain owned by libgpu.

The implementation must include performance testing that demonstrates measurable improvement versus the current baseline.

## Pipeline Scope (from definitive topology)

Target node intents:

1. SyntheticApertureIqSourceNode
2. RangeWindowNode
3. RangeCompressionNode
4. AzimuthTileSplitNode
5. H2DAsyncNode
6. SarBackprojectionTransformNode
7. D2HAsyncNode
8. ImageTileMergeNode
9. SarDiagnosticsSinkNode

## Placement Rules

Put in examples/SAR (SAR-specific):

1. SyntheticApertureIqSourceNode SAR semantics and source behavior.
2. SAR-specific range/backprojection adapters when they encode SAR-only metadata or sidecar semantics and delegate to libgpu Metal primitives.
3. SAR-only merge/diagnostics behavior and message contracts.

Put in libgpu (common/reusable):

1. Generic METAL async transfer nodes and shared transfer abstractions.
2. Reusable METAL kernel-dispatch nodes/descriptors that are not SAR-specific.
3. Common buffer lease/view/ticket handling, queue selection, and synchronization primitives.
4. Reusable acceleration helpers that could be used by non-SAR pipelines.

Do not migrate SAR-specific policy into libgpu unless at least one non-SAR consumer can use it.

## Mandatory Architecture Constraints

1. Keep GraphExecutorBuilder + JSON as canonical execution path.
2. Keep single definitive topology file as canonical artifact:
   - examples/SAR/config/sar_stripmap_definitive.json
3. Keep accel-token edge contract strict:
   - edge_contract must remain accel-token
4. Keep resolver metadata fields explicit:
   - execution_backend
   - backend_fallback_policy
   - resolver_diagnostics
   - edge_contract
5. Keep portable node intent types in JSON; backend-specific substitution happens in resolver/provider path.
6. Preserve sidecar identity fields across transfer/kernel boundaries.
7. No direct/non-graph runtime path for production behavior (direct allowed only for baseline/parity/perf attribution).
8. Resolver substitutions must not target SAR-local `*Metal` duplicates when an equivalent libgpu node exists.
9. SAR-specific resolver substitutions must be supplied dynamically through SAR-owned `resolver_mappings` or future plugin metadata, not hard-coded into `libgraph`.

## Required Implementation Outputs

1. Node mapping design note
   - For each definitive topology node, classify:
     - portable intent only
     - resolver-substituted to METAL equivalent
     - SAR-specific adapter that delegates to reusable libgpu METAL primitive
   - Include rationale and file locations.

2. Code changes
   - Implement missing METAL-equivalent behaviors required for definitive topology acceleration.
   - Refactor common logic into libgpu where reusable.
   - Keep SAR-specific layers in examples/SAR.

3. Tests
   - Add/extend unit and integration tests to prove:
     - resolver substitution behavior,
     - strict contract behavior when METAL strict mode cannot resolve,
     - allow_fallback behavior,
     - completion signaling and sidecar preservation,
     - no payload-contract regressions under accel-token mode.

4. Performance validation
   - Update or add benchmark coverage that compares:
     - resolver_auto path from definitive topology,
     - resolver_metal path from same topology.
   - Report per-run metrics and summary statistics.

5. Documentation updates
   - Update examples/SAR/README.md with:
     - which stages have METAL equivalents,
     - where implementations live (examples/SAR vs libgpu),
     - benchmark invocation and interpretation.

## Performance Evidence Requirements

Performance tests must demonstrate improvement and attribution clarity.

At minimum report:

1. Wall-clock run time (per-run ms, avg, min, max).
2. Graph run time and baseline execute time where available.
3. Transfer bytes and kernel dispatch counts.
4. Overhead attribution fields:
   - overhead_ms.graph_run_minus_baseline_median
   - overhead_attribution.cost_buckets.graph_overhead_ms
5. Performance claim policy fields remain valid.

Acceptance threshold:

1. Demonstrate reproducible improvement of resolver_metal versus resolver_auto for at least one meaningful benchmark profile, or
2. If improvement is not achieved, provide bottleneck attribution explaining why and what follow-up is needed.

## Validation Checklist

1. Definitive topology executes successfully through sar_example.
2. Completion signaling is true for expected complete runs.
3. Resolver strict METAL behavior is validated (expected fail without provider where applicable).
4. Resolver allow_fallback behavior is validated and succeeds from same topology.
5. SAR unit lane remains green.
6. Trace schema and attribution-policy tests remain green.
7. No regression in accel-token sidecar contract tests.

## Non-Goals

1. No framework-wide scheduler redesign.
2. No replacement of GraphExecutor + JSON contract.
3. No migration of SAR-specific semantics into libgpu without reuse justification.

## Reviewer Evidence Expectations

Provide:

1. File list of moved/refactored common METAL logic now in libgpu.
2. File list of SAR-specific adapters retained in examples/SAR.
3. Test output snippets showing resolver substitution behavior and SAR lane pass.
4. Benchmark output showing performance delta and attribution evidence.
5. Brief note on residual risks and next optimization candidates.

## Execution Notes For Coding Agent

1. Inspect current code before implementing; do not assume legacy topology split files are canonical.
2. Treat examples/SAR/config/sar_stripmap_definitive.json as source of truth.
3. Preserve backward compatibility for maintained SAR presets unless explicitly removed in this PR.
4. Prefer smallest safe refactor that achieves reusable METAL common-path extraction.
