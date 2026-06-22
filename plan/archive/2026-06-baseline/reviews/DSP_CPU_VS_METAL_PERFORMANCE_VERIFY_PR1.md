# DSP CPU vs Metal Performance PR1 Verifier Report

## PR

PR1 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: Consolidated GraphExecutor Execute Timing.

## Verdict

PASS.

## Required Checks

### `ExecutionResult` exposes consolidated `Execute()` timing fields

PASS.

`libgraph/include/graph/ExecutionResult.hpp` exposes:

- `init_elapsed_time_ms`
- `start_elapsed_time_ms`
- `run_elapsed_time_ms`
- `stop_elapsed_time_ms`
- `join_elapsed_time_ms`

`elapsed_time_ms` remains present and is documented as operation timing or total `Execute()` wall-clock timing.

### `GraphExecutor::ExecuteExpected()` sets total `elapsed_time_ms`

PASS.

`libgraph/src/graph/GraphExecutor.cpp` starts a wall-clock timer at the beginning of `ExecuteExpected()` and sets `result.elapsed_time_ms` from the total duration after `JoinExpected()` completes.

### `ExecuteExpected()` copies phase timings for init/start/run/stop/join

PASS.

`ExecuteExpected()` copies:

- `init_result->elapsed_time_ms` to `result.init_elapsed_time_ms`
- `start_result->elapsed_time_ms` to `result.start_elapsed_time_ms`
- `run_result->elapsed_time_ms` to `result.run_elapsed_time_ms`
- `stop_result->elapsed_time_ms` to `result.stop_elapsed_time_ms`
- `join_result->elapsed_time_ms` to `result.join_elapsed_time_ms`

### Existing manual lifecycle methods still expose previous timing behavior

PASS.

`InitExpected()`, `StartExpected()`, `RunExpected()`, `StopExpected()`, and `JoinExpected()` remain independently timed. The new focused test verifies that standalone `Start()`, `Stop()`, and `Join()` results do not populate composite `Execute()` phase fields.

### Focused timing contract tests exist and pass

PASS.

Focused tests exist in `libgraph/test/unit/test_graph_executor_execute_timing.cpp`:

- `GraphExecutorExecuteTimingTest.ExecuteReturnsTotalAndLifecyclePhaseTimings`
- `GraphExecutorExecuteTimingTest.StandaloneLifecycleResultsRemainStandalone`

Validation command:

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_libgraph_unit
```

Result: passed, no work to do.

Validation command:

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=GraphExecutorExecuteTimingTest.*:DspSpectrumGraphRuntimeTest.JsonTopologyRunsThroughExecutorAndDetectsSinePeak' \
  --gtest_brief=1
```

Result: passed, 3 tests.

### No benchmark executable, DSP docs, performance claims, or unrelated runtime redesign was added

PASS.

The PR changes are limited to:

- `libgraph/include/graph/ExecutionResult.hpp`
- `libgraph/src/graph/GraphExecutor.cpp`
- `libgraph/test/unit/test_graph_executor_execute_timing.cpp`
- this verifier report
- the implementer report

Searches found existing benchmark/performance-related SAR and threadpool material, but no new benchmark executable, DSP documentation, or performance claim introduced by this PR.

## Notes

- The implementation preserves the existing `ExecuteExpected()` failure style: lifecycle failures still return `std::unexpected`.
- The new consolidated timing fields are available on successful `Execute()` results for later CPU-vs-Metal comparison work.
