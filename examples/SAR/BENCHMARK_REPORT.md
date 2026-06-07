# SAR Benchmark Report

## Scope

This report compares the deterministic GraphX SAR JSON pipeline against a deterministic non-graph baseline path that executes the same stage sequence:

1. SyntheticApertureIqSourceNode
2. RangeWindowNode
3. AzimuthTileSplitNode
4. H2DAsyncNode
5. SarBackprojectionTransformNode
6. D2HAsyncNode
7. ImageTileMergeNode
8. SarDiagnosticsSinkNode

The benchmark is implemented in examples/SAR/src/sar_benchmark.cpp and emits:

- warm-up + repeated run timing summaries
- median/min/max/stddev
- graph-vs-baseline timing comparison
- overhead attribution categories required by Phase 8

## Profiles

CI-safe small profile:

- pulses: 32
- samples_per_pulse: 256
- tile_count: 4
- warm-up runs: 1
- measured runs: 5

Larger local profile:

- pulses: 128
- samples_per_pulse: 1024
- tile_count: 8
- warm-up runs: 2
- measured runs: 10

## How To Run

Build:

```bash
cmake --build --preset build-debug --target sar_benchmark
```

Run CI-safe profile:

```bash
./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci
```

Run larger local profile:

```bash
./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=local
```

Optional JSON trace export:

```bash
./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci --trace-out=/tmp/sar_trace.json
```

The trace uses schema `graphx.sar.benchmark.trace.v1` and records profile metadata, graph build/run/lifecycle timing summaries, baseline timing, last lifecycle phase timings, diagnostics counters, queue counters, and overhead proxies.

## Correctness Guard

Each measured run verifies deterministic diagnostics parity between graph and baseline:

- pulses_processed
- tiles_processed
- bytes_h2d
- bytes_d2h
- kernel_dispatches
- duplicate_tile_count
- missing_tile_count

If parity fails, the benchmark exits non-zero.

The direct baseline includes the same deterministic range-window stage as the graph topology,
so reported overhead remains graph-specific rather than DSP-algorithm drift.

## Overhead Attribution Categories

The benchmark reports the following categories using deterministic, measurable proxies:

1. graph scheduling/run loop: median(graph_run_ms) - median(baseline_execute_ms)
2. message allocation/copy: bytes_h2d/bytes_d2h totals
3. queue wait/backpressure: fanin_wait_ms, backpressure_events, peak_queue_depth
4. provider/plugin lookup: graph build-time summary
5. diagnostics collection: sink contract emission path
6. backend synchronization: e2e_latency_ms proxy
7. lifecycle teardown: init/start/stop/join/total timing reported separately from graph run time

## CI Gate Guidance

- CI should gate on correctness and metrics presence.
- Performance thresholds should remain conservative and non-brittle.
- The CI profile above is designed to be stable and fast.
