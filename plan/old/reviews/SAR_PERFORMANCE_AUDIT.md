# SAR Performance Audit

Date: 2026-06-09
Role: PERFORMANCE_AUDITOR
Scope: Current repository state after PR1-PR8

## Summary

GraphX does not yet expose enough instrumentation to fully understand runtime behavior end to end.

It is strong on:
- coarse aggregate counters,
- benchmark attribution-policy metadata,
- correctness-oriented runtime evidence,
- native-backend selection evidence.

It is weak on:
- per-stage timing,
- real exported GPU duration summaries,
- memory visibility,
- diagnostics self-cost,
- queue and latency distributions.

The current surface is sufficient for regression triage and policy enforcement, but not for deep causal runtime analysis.

## 1. Missing measurements

### Ranked by importance

1. Per-stage runtime spans on the canonical SAR path.
2. Public transfer and kernel duration summaries for native Metal runtime.
3. Memory live/peak/allocation metrics.
4. Per-edge queue metrics and latency distributions.
5. Diagnostics self-cost measurement.
6. Resolver/plugin load timing breakdown.
7. Explicit synchronization latency separate from sequence-based proxy.

### Detail

#### High importance

- Per-stage runtime spans are missing for the definitive SAR path.
  - Current surfaces do not cleanly separate graph overhead, DSP overhead, transfer overhead, kernel overhead, and diagnostics overhead with node-level timing on the canonical runtime path.

- Real exported GPU duration metrics are missing.
  - Native Metal telemetry stores sample counts and internal last-duration fields, but the public runtime surface does not expose usable summaries for benchmark or audit consumers.

- Memory high-water and live-usage measurements are missing.
  - The native runtime keeps allocation maps internally but does not expose live bytes, peak bytes, allocation churn, reuse, or fragmentation metrics.

#### Medium importance

- Per-edge and per-queue latency distributions are missing.
  - Aggregate queue metrics exist, but no queue histograms, per-edge occupancy, or queue wait by stage are surfaced.

- Diagnostics self-cost is missing.
  - Diagnostics are emitted and traced, but there is no direct timing or byte accounting for sink updates, metric aggregation, or trace serialization.

#### Low importance

- Resolver and plugin-load timing is folded into graph build time rather than broken out into discovery, load, resolution, and executor construction phases.

## 2. Existing measurements

- Graph-wide lifecycle, throughput, queue, rejection, and aggregate thread counters exist in `libgraph/include/graph/GraphMetrics.hpp`.
- GraphManager aggregates edge and thread metrics into a graph-level snapshot in `libgraph/include/graph/GraphManager.hpp`.
- SAR runtime diagnostics expose:
  - pulses,
  - tiles,
  - bytes,
  - kernel dispatches,
  - transfer and kernel timing fields,
  - fan-in wait,
  - out-of-order count,
  - queue backpressure,
  - peak queue depth.
- SAR benchmark emits:
  - graph build/run/lifecycle stats,
  - baseline stats,
  - resolved-node metadata,
  - token lifecycle metadata,
  - native execution evidence,
  - attribution policy,
  - PR8 native parity lock evidence.
- Native Metal telemetry currently exposes:
  - transfer sample count,
  - kernel sample count,
  - error count.

## 3. Graph-level metrics

### Present

- `init_time_ns`
- `start_time_ns`
- `execution_time_ns`
- `total_items_processed`
- `total_items_rejected`
- `total_messages_processed`
- `graph_total_enqueued`
- `graph_total_dequeued`
- `total_queue_time_ns`
- `total_process_time_ns`
- `total_thread_time_ns`
- `backpressure_events`
- `peak_active_threads`
- `peak_queue_depth`
- node/edge init and start failure counters

### Limitation

These are aggregate graph-level snapshots. They do not provide per-node or per-subpipeline timing breakdowns.

### Overhead separation status

- Graph overhead: partially visible.
- DSP overhead: not directly isolated.
- Transfer overhead: partially mixed into graph and SAR counters.
- Kernel overhead: not isolated at graph metric level.
- Diagnostics overhead: not directly measured.

## 4. GPU transfer metrics

### Present

- `bytes_h2d`
- `bytes_d2h`
- transfer ticket metadata:
  - transfer id
  - queue id
  - completion signal id
- token lifecycle host/device view handles in benchmark trace
- native telemetry transfer sample counts

### Limitation

Transfer timing is not exposed as a robust runtime series.

The public telemetry API exposes counts only, while timing storage is internal and retains only the last sample. There is no exported:
- min,
- max,
- total,
- median,
- per-direction timing summary.

## 5. Kernel metrics

### Present

- kernel dispatch count in SAR diagnostics and benchmark trace
- kernel identity, queue id, arg count, and native execution evidence in benchmark trace
- native telemetry kernel sample count and error count

### Limitation

Kernel duration recording at the node layer is still placeholder-grade in several Metal nodes, which record zero-duration telemetry samples.

Missing:
- per-kernel duration distribution,
- compile/register time,
- occupancy/utilization,
- queue wait per kernel dispatch.

## 6. Memory metrics

### Present

- token and diagnostics surfaces carry byte counts and lease/view metadata
- native runtime internally tracks allocation records for device, shared, and host allocations

### Limitation

Those allocation maps are implementation detail only. There is no public metric surface for:
- live bytes,
- peak bytes,
- allocation count by pool,
- release count,
- reuse ratio,
- leak suspicion,
- fragmentation.

## 7. Queue metrics

### Present

- graph-level `backpressure_events`
- graph-level `peak_queue_depth`
- aggregate queue-time accounting
- SAR `fanin_wait_ms`
- SAR `out_of_order_completion_count`

### Limitation

Queue metrics are aggregate and shallow.

Missing:
- per-edge queue depth timeline,
- queue wait distribution,
- queue-specific service time by queue id,
- GPU queue occupancy measurement.

## 8. Diagnostics overhead

### Present

- diagnostics fields are merged in `SarDiagnosticsSinkNode`
- benchmark preserves diagnostics contract and attribution-policy metadata

### Limitation

Diagnostics overhead is not directly measured.

There is no explicit timing for:
- sink consumption,
- graph-metric merge,
- JSON trace assembly,
- trace file write cost.

Also, `e2e_latency_ms` is used as a proxy in some paths rather than a direct wall-clock measurement.

## 9. Benchmark gaps

The benchmark is good at:
- coarse attribution policy,
- correctness parity metadata,
- native-path evidence.

The benchmark still lacks:
- per-node spans for the canonical SAR path,
- real exported GPU timing summaries,
- memory high-water and allocation churn reporting,
- queue latency distributions,
- explicit diagnostics serialization/update cost,
- a clean distinction between DSP stage cost and graph orchestration cost beyond baseline subtraction,
- a persistent metrics snapshot API separate from trace generation.

## 10. Instrumentation to add

### Highest importance

- Add per-stage timing spans on the definitive SAR path for:
  - range stage,
  - split,
  - H2D,
  - backprojection,
  - D2H,
  - merge,
  - sink,
  - graph lifecycle phases.

- Add a public native telemetry snapshot surface that exports:
  - transfer totals,
  - kernel totals,
  - counts,
  - last value,
  - ideally min/max or bucketed summaries by operation kind and queue.

- Add memory pool metrics snapshots for:
  - live bytes,
  - peak bytes,
  - allocation count,
  - release count,
  - per-pool totals.

### Medium importance

- Add per-edge queue snapshots for:
  - queue depth high-water,
  - cumulative wait,
  - per-edge backpressure.

- Add explicit diagnostics overhead counters for:
  - sink update time,
  - graph-metric merge time,
  - benchmark trace serialization/write time.

- Add resolver/plugin-load timing breakdown inside executor construction and benchmark trace.

### Lower importance

- Add synchronization timing distinct from sequence-based proxy latency.
- Add raw telemetry export for error-code counts already tracked internally.

## Separation By Overhead Class

### Graph overhead

#### Existing
- graph build time
- graph lifecycle timing
- aggregate queue time
- aggregate thread time
- queue backpressure events
- peak queue depth

#### Missing
- per-node orchestration time
- executor construction breakdown
- scheduling overhead by stage

### DSP overhead

#### Existing
- benchmark baseline execute time
- range-stage selection metadata
- matched-filter reference/runtime comparison fields

#### Missing
- isolated timing for `RangeWindowNode`
- isolated timing for `RangeCompressionNode`
- direct stage spans on canonical graph path

### Transfer overhead

#### Existing
- bytes H2D/D2H
- SAR transfer timing fields
- transfer ticket metadata
- telemetry sample counts

#### Missing
- exported transfer duration summaries
- per-direction latency distributions
- queue wait before transfer submission/completion

### Kernel overhead

#### Existing
- kernel dispatch count
- kernel ticket metadata
- native kernel executed flag
- telemetry sample counts

#### Missing
- non-placeholder duration reporting
- per-kernel timing summaries
- kernel registration/compile timing
- utilization/occupancy-like measurements

### Diagnostics overhead

#### Existing
- diagnostics message contract
- sink consume path
- benchmark attribution policy fields

#### Missing
- sink update time
- graph metrics merge time
- trace serialization/write time
- diagnostics byte cost

## Conclusion

GraphX currently exposes enough instrumentation to answer:
- whether the system ran,
- whether native Metal was selected,
- whether canonical token transport remained intact,
- whether coarse aggregate throughput and queue pressure changed.

GraphX does not yet expose enough instrumentation to explain why runtime behavior changed at stage granularity.
