# DSP CPU vs Metal Performance PR1 Implementer Report

## PR

PR1 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: Consolidated GraphExecutor Execute Timing.

## Files Changed

- `libgraph/include/graph/ExecutionResult.hpp`
  - Added consolidated `Execute()` phase timing fields:
    - `init_elapsed_time_ms`
    - `start_elapsed_time_ms`
    - `run_elapsed_time_ms`
    - `stop_elapsed_time_ms`
    - `join_elapsed_time_ms`
  - Clarified that `elapsed_time_ms` is the total `Execute()` wall-clock duration when returned from `Execute()`.
- `libgraph/src/graph/GraphExecutor.cpp`
  - Updated `GraphExecutor::ExecuteExpected()` to measure total `Execute()` wall-clock duration.
  - Copied elapsed timings from `InitExpected()`, `StartExpected()`, `RunExpected()`, `StopExpected()`, and `JoinExpected()` into the consolidated `ExecutionResult`.
  - Preserved the existing lifecycle call sequence and failure behavior.
- `libgraph/test/unit/test_graph_executor_execute_timing.cpp`
  - Added focused contract tests for consolidated `Execute()` timing.
  - Added a guard test that standalone lifecycle calls keep their prior standalone timing behavior and do not populate composite phase fields.

## Files Deleted

- None.

## Tests Added

- `GraphExecutorExecuteTimingTest.ExecuteReturnsTotalAndLifecyclePhaseTimings`
- `GraphExecutorExecuteTimingTest.StandaloneLifecycleResultsRemainStandalone`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=GraphExecutorExecuteTimingTest.*:DspSpectrumGraphRuntimeTest.JsonTopologyRunsThroughExecutorAndDetectsSinePeak' \
  --gtest_brief=1
```

Result: passed, 3 tests.

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit --gtest_brief=1
```

Result: passed, 999 tests passed, 7 skipped, 1 disabled.

## Scope Guardrails

- No benchmark executable was added.
- No DSP documentation was changed.
- No performance claims were added.
- No GraphExecutor lifecycle method semantics were changed outside the requested consolidated `Execute()` timing result.

## Remaining Follow-Up Work

- PR2 can now use `GraphExecutor::Execute()` returned `ExecutionResult` fields as the canonical timing source for CPU-vs-Metal DSP comparison.
