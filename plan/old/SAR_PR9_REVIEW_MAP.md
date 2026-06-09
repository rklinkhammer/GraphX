# SAR PR9 Reviewer Evidence Map

## Scope To Evidence

### Real materialized extraction from accel-token host views

- Implementation: [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp)
- Implementation: [examples/SAR/src/SarMaterializedImageSinkNode.cpp](examples/SAR/src/SarMaterializedImageSinkNode.cpp)
- Shared runtime wiring: [examples/SAR/CMakeLists.txt](examples/SAR/CMakeLists.txt)
- Shared runtime wiring: [examples/SAR/plugins/CMakeLists.txt](examples/SAR/plugins/CMakeLists.txt)
- Shared runtime component: [examples/SAR/src/SarAccelTokenImagePayloadStore.cpp](examples/SAR/src/SarAccelTokenImagePayloadStore.cpp)

### Preserve GraphExecutor plus JSON runtime contract

- JSON integration tests: [examples/SAR/test/test_sar_json_pipeline.cpp](examples/SAR/test/test_sar_json_pipeline.cpp)
- Preset/runtime contract tests: [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp)
- Canonical entrypoint remains unchanged: [examples/SAR/src/main.cpp](examples/SAR/src/main.cpp)

### Preserve accel-token sidecar and merge invariants

- Sidecar and merge invariants: [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp)
- Fanout/terminal invariants: [examples/SAR/test/test_sar_pr2_fanout_json.cpp](examples/SAR/test/test_sar_pr2_fanout_json.cpp)

### PR9-specific sink contract hardening

- New sink contract suite: [examples/SAR/test/test_sar_materialized_image_sink_node.cpp](examples/SAR/test/test_sar_materialized_image_sink_node.cpp)
- Test target wiring: [examples/SAR/test/CMakeLists.txt](examples/SAR/test/CMakeLists.txt)

### Benchmark and trace attribution discipline unchanged

- Trace schema checks: [examples/SAR/test/test_sar_trace_schema.cpp](examples/SAR/test/test_sar_trace_schema.cpp)
- Benchmark implementation: [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp)

## Required Reviewer Checks

1. Confirm materialized sink captures payload only when shared payload exists for the token.
2. Confirm plugin and non-plugin SAR paths link the shared payload runtime component.
3. Confirm PR7 JSON pipeline parity tests remain green.
4. Confirm no legacy payload-edge contract was reintroduced under accel-token mode.
5. Confirm trace schema checks still enforce performance claim policy fields.

## Validation Snapshot

1. CTest target executed: sar_example_unit
2. Result: pass
3. Checklist status and summary: [plan/SAR_PR9_CHECKLIST](plan/SAR_PR9_CHECKLIST)
