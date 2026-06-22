# SAR Performance PR Roadmap

Date: 2026-06-09
Role: PLANNER
Scope: Convert performance-audit findings into reviewable, measurement-first PRs

## 1. Findings Classification

### Instrumentation gap

- No per-stage runtime spans on the canonical SAR runtime path.
- No public native Metal telemetry snapshot for transfer/kernel duration summaries.
- No public memory metrics for live bytes, peak bytes, allocation churn, or reuse.
- No per-edge or per-queue latency distribution metrics beyond graph-wide aggregates.
- No direct diagnostics overhead measurement for sink update, graph-metric merge, or trace serialization.
- No resolver/plugin-load timing breakdown beyond coarse graph build timing.

### Benchmark gap

- Benchmark attribution is policy-safe but still relies on coarse aggregate/proxy fields rather than direct stage spans.
- `e2e_latency_ms` and some queue/fan-in values are still proxies rather than wall-clock measurements.
- Native parity evidence is explicit after PR8, but benchmark output still lacks memory and queue distributions needed for causal runtime analysis.

### Confirmed bottleneck

- None.

### Suspected bottleneck

- Graph scheduling/orchestration overhead is a likely contributor because the benchmark already computes `graph_run_minus_baseline_median`, but it is not decomposed per stage.
- Queue/fan-in waiting on merge is a likely contributor because `fanin_wait_ms`, backpressure, and peak queue depth already surface pressure signals.
- Diagnostics/trace cost may be non-trivial because the benchmark emits large structured traces, but there is no direct self-cost timing.

### Premature optimization

- Any SAR math changes.
- Native Metal kernel tuning.
- Queue policy tuning.
- Memory reuse/pool strategy changes.
- Resolver-path changes for performance.
- DeviceReduce or other algorithmic substitutions unless instrumentation first proves the bottleneck with current canonical-path data.

## 2. PR-Sized Roadmap

The roadmap is measurement-first and instrumentation-only until evidence quality is high enough to support optimization work.

Order:
1. Canonical SAR stage timing spans.
2. Public native Metal telemetry snapshots.
3. Native Metal memory metrics surface.
4. Per-edge queue and latency instrumentation.
5. Diagnostics and trace self-cost instrumentation.
6. Executor construction and resolver timing breakdown.

## 3. PR Details

### PR-A1

- Title: Canonical SAR Stage Timing Spans
- Scope:
  - Add explicit stage timing measurements for the definitive SAR path.
  - Export them through diagnostics and benchmark trace.
  - Do not change SAR math or transport contracts.
- Files to touch:
  - `examples/SAR/src/AzimuthTileSplitNode.cpp`
  - `examples/SAR/src/H2DAsyncAccelNode.cpp`
  - `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
  - `examples/SAR/src/D2HAsyncAccelNode.cpp`
  - `examples/SAR/src/ImageTileMergeNode.cpp`
  - `examples/SAR/include/sar/SarMessages.hpp`
  - `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `examples/SAR/test/test_sar_json_runtime.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Schema assertions for new per-stage timing fields.
  - Runtime assertions that definitive topology emits non-negative per-stage timings.
- Metrics expected:
  - `range_stage_time_us`
  - `split_time_us`
  - `h2d_stage_time_us`
  - `backprojection_stage_time_us`
  - `d2h_stage_time_us`
  - `merge_stage_time_us`
  - `diagnostics_sink_time_us`
- Acceptance criteria:
  - Definitive topology emits stage timing fields in diagnostics and trace.
  - Benchmark trace schema covers those fields.
  - Full CTest lane remains green.

### PR-A2

- Title: Public Native Metal Telemetry Snapshots
- Scope:
  - Add a public telemetry snapshot API for Metal transfer/kernel metrics.
  - Wire it into benchmark trace export.
  - Do not change execution semantics.
- Files to touch:
  - `libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp`
  - `libgpu/include/gpu/metal/capabilities/NativeMetalCapabilities.hpp`
  - `libgpu/src/gpu/metal/native/NativeMetalCapabilities.cpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `libgpu/test/runtime/test_metal_runtime_stress.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Runtime tests for snapshot totals and last-value exposure.
  - Trace schema assertions for exported telemetry summary block.
- Metrics expected:
  - transfer sample count
  - kernel sample count
  - error count
  - transfer total duration
  - kernel total duration
  - last transfer duration
  - last kernel duration
  - optional per-direction counts
- Acceptance criteria:
  - Public snapshot API exists and is used by the SAR benchmark.
  - Benchmark trace exports telemetry summaries for native backend runs.
  - Full CTest lane remains green.

### PR-A3

- Title: Native Metal Memory Metrics Surface
- Scope:
  - Surface read-only memory pool metrics from the native Metal runtime.
  - Export them in benchmark traces.
- Files to touch:
  - `libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp`
  - `libgpu/include/gpu/metal/capabilities/NativeMetalCapabilities.hpp`
  - `libgpu/src/gpu/metal/native/NativeMetalCapabilities.cpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `libgpu/test/runtime/test_metal_runtime_stress.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Allocation/release lifecycle tests asserting live/peak counters move as expected.
  - Trace schema assertions for memory metrics block.
- Metrics expected:
  - live device bytes
  - live shared bytes
  - live host bytes
  - peak bytes by pool
  - allocation count
  - release count
- Acceptance criteria:
  - Memory metrics are publicly queryable.
  - SAR benchmark trace exports memory metrics for native backend runs.
  - Full CTest lane remains green.

### PR-A4

- Title: Per-Edge Queue and Latency Instrumentation
- Scope:
  - Add per-edge queue snapshot support.
  - Expose queue-wait summaries relevant to canonical SAR runtime behavior.
- Files to touch:
  - `libgraph/include/graph/GraphMetrics.hpp`
  - `libgraph/include/graph/GraphManager.hpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
  - `examples/SAR/test/test_sar_json_runtime.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Graph-level tests for per-edge queue snapshot aggregation.
  - Trace schema assertions for per-edge queue summary block.
- Metrics expected:
  - per-edge peak queue depth
  - per-edge backpressure events
  - per-edge total queue wait
  - queue wait summary buckets or min/max/avg
- Acceptance criteria:
  - Queue metrics are available beyond graph aggregate totals.
  - SAR trace exports queue summaries for canonical-path runs.
  - Full CTest lane remains green.

### PR-A5

- Title: Diagnostics and Trace Self-Cost Instrumentation
- Scope:
  - Measure diagnostics sink update time, graph-metric merge time, and benchmark trace serialization/write overhead explicitly.
- Files to touch:
  - `examples/SAR/src/SarDiagnosticsSinkNode.cpp`
  - `examples/SAR/include/sar/SarMessages.hpp`
  - `examples/SAR/src/sar_benchmark.cpp`
  - `examples/SAR/test/test_sar_diagnostics_contract.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Diagnostics contract assertions for new self-cost fields.
  - Trace schema assertions for diagnostics-overhead block.
- Metrics expected:
  - diagnostics sink update time
  - graph-metric merge time
  - trace JSON assembly time
  - trace file write time
- Acceptance criteria:
  - Diagnostics overhead is explicitly exported rather than implied.
  - Benchmark attribution policy remains intact after new fields are added.
  - Full CTest lane remains green.

### PR-A6

- Title: Executor Construction and Resolver Timing Breakdown
- Scope:
  - Break graph build into config parse, plugin discovery/load, resolver mapping/build, and executor construction timings.
- Files to touch:
  - `examples/SAR/src/sar_benchmark.cpp`
  - `libgraph/include/graph/GraphManager.hpp`
  - `libgraph/src/graph/GraphExecutorBuilder.cpp`
  - `examples/SAR/test/test_sar_json_runtime.cpp`
  - `examples/SAR/test/test_sar_trace_schema.cpp`
- Tests to add:
  - Trace schema assertions for build-phase breakdown.
  - Runtime assertions that definitive/native runs emit non-negative build-phase times.
- Metrics expected:
  - config parse time
  - plugin discovery/load time
  - resolver/build time
  - executor construction time
- Acceptance criteria:
  - Graph build timing is no longer a single opaque number in benchmark output.
  - Full CTest lane remains green.

## 4. Items Explicitly Deferred

- Any optimization PRs.
- Any SAR math or algorithm changes.
- Any kernel tuning, queue tuning, or memory reuse tuning.
- Any DeviceReduce promotion or removal decisions not already supported by measured evidence.
- Any architecture change away from `AccelControlToken<SarSidecar>`.
- Any broad native-Metal parity redesign beyond existing PR8 evidence and measurement export work.
