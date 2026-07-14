# GraphX Test Simplification Plan

## Executive Summary

GraphX has accumulated a useful but temporary layer of phase-named tests during graph_rev2, fhss_upgrade, and the GPU clean-restart work. Those tests were appropriate while the implementation was moving, but the long-term suite should emphasize stable behavior, JSON topology ownership, backend truthfulness, CPU/reference parity, e2e smoke coverage, discovery registration, and benchmark/evidence lanes that are not part of the default fast unit path.

The main rule for closure is simple: phase labels may remain only while the migration is still active. Before the cleanup is considered done, every phase-specific suite must be classified as one of:

- KEEP_AS_CONTRACT
- RENAME_TO_BEHAVIOR_TEST
- MERGE_INTO_FINAL_SUITE
- DELETE_AS_DUPLICATE
- MOVE_TO_BENCHMARK_OR_MANUAL_VERIFICATION

The inventory below shows that most phase-named tests are already behaving like final contract tests. The cleanup work is therefore mostly naming, splitting, merging, and moving slow or hardware-specific coverage out of the default unit lane. One important exception is the FHSS phase-7 evidence runner, which should be kept as evidence/benchmark infrastructure rather than promoted into the fast unit path.

Search note: active test code does not currently use graph_rev2 as a suite name. That term appears in design and history notes, not in live test sources, so there is no graph_rev2 test suite to rename or delete.

## Inventory And Classification Table

Phase-specific or phase-labeled suites found in the workspace.

| File path | Current suite / CTest | Owning module | Behavior currently protected | Classification | Recommended final name or destination | Duplicate coverage | Hardware-specific | Slow | Standard unit? |
|---|---|---|---|---|---|---|---|---|---|
| [libgraph/test/integration/test_graph_1.cpp](libgraph/test/integration/test_graph_1.cpp) | `TestGraph1`; `libgraph_integration` | `libgraph` | Executor lifecycle, data flow, completion, concurrency, builder API, timing behavior | MERGE_INTO_FINAL_SUITE; RENAME_TO_BEHAVIOR_TEST | Split into `GraphExecutorLifecycleTest`, `GraphExecutorDataFlowTest`, `GraphExecutorCompletionTest`, `GraphExecutorConcurrencyTest`, `GraphExecutorBuilderContractTest`, and a separate timing/benchmark lane | Yes, the phase labels duplicate the same executor behaviors across Phases 1-5 | No | Some timing cases are slow | No, keep in integration/behavior lanes |
| [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) | `AccelGraphPhase2TopologyContractTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | JSON topology ownership, loader validation, checked-in topology structure, and the no-direct-Configure guardrail | KEEP_AS_CONTRACT | `TopologyJsonOwnershipTest` or `AccelGraphTopologyJsonOwnershipTest` | No | No | No | Yes |
| [libaccelgraph/test/unit/test_accelgraph_phase3_metal.cpp](libaccelgraph/test/unit/test_accelgraph_phase3_metal.cpp) | `AccelGraphPhase3ATest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CPU and Metal transfer topology execution with exact skip diagnostics | RENAME_TO_BEHAVIOR_TEST | `AccelGraphTransferTopologyTest` or `GraphExecutorBackendFallbackTest` | Partial overlap with other backend-matrix phase suites | Metal | No | Conditional; keep on macOS Metal lane |
| [libaccelgraph/test/unit/test_accelgraph_phase4_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase4_cuda.cpp) | `AccelGraphPhase4CudaTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CUDA provider identity and shell diagnostic honesty | KEEP_AS_CONTRACT | `CudaGraphExecutorContractTest` | Partial overlap with Phase 5 and Phase 6b CUDA matrix coverage | CUDA | No | Conditional; keep on CUDA lane |
| [libaccelgraph/test/unit/test_accelgraph_phase5_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase5_cuda.cpp) | `AccelGraphPhase5CudaGraphExecutorTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CUDA transfer topology execution or exact skip diagnostics | MERGE_INTO_FINAL_SUITE | `CudaGraphExecutorContractTest` or `GraphExecutorBackendFallbackTest` | Yes, overlaps with Phase 4 CUDA contract checks | CUDA | No | Conditional; keep on CUDA lane |
| [libaccelgraph/test/unit/test_accelgraph_phase6_spectrum.cpp](libaccelgraph/test/unit/test_accelgraph_phase6_spectrum.cpp) | `AccelGraphPhase6SpectrumTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | Spectrum CPU/Metal correctness, parity, fallback policy, plugin surface hygiene | MERGE_INTO_FINAL_SUITE | `SpectrumTopologyTest` or `AccelGraphSpectrumTopologyTest` | Yes, duplicates Phase 6b backend-matrix coverage | Metal | No to moderate | Conditional; keep on Metal-capable lane |
| [libaccelgraph/test/unit/test_accelgraph_phase6b_spectrum_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase6b_spectrum_cuda.cpp) | `AccelGraphPhase6BCudaSpectrumTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` when CUDA is enabled | `libaccelgraph` | CPU/CUDA spectrum parity, strict fallback, backend honesty | MERGE_INTO_FINAL_SUITE | `SpectrumTopologyTest` or `CudaGraphExecutorContractTest` | Yes, duplicates Phase 6 spectrum backend behavior | CUDA | No to moderate | Conditional; keep on CUDA lane |
| [libaccelgraph/test/unit/test_accelgraph_smoke.cpp](libaccelgraph/test/unit/test_accelgraph_smoke.cpp) | `AccelGraphSmokeTest`; `libaccelgraph_smoke` | `libaccelgraph` | Basic scaffold and smoke registration state | RENAME_TO_BEHAVIOR_TEST | `AccelGraphSmokeTest` or `AccelGraphScaffoldTest` | No | No | No | Yes |
| [libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp) | `AccelGraphFhssAccelContractTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | FHSS token alias bridge, config parsing, descriptor fields | KEEP_AS_CONTRACT | `AccelGraphFhssContractTest` | No | No | No | Yes |
| [libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp) | `AccelGraphFhssDownconverterTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CPU parity vs DSP downconverter, JSON-topology execution, Metal/CUDA strict-fallback matrix | RENAME_TO_BEHAVIOR_TEST | `AccelGraphFhssDownconverterTopologyTest` with a parity subcase if the file stays mixed | Partial overlap with the other FHSS stage matrix files | No | No to moderate | Yes, but only because it already has topology coverage |
| [libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp) | `AccelGraphFhssChannelizerTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CPU parity vs DSP channelizer, JSON-topology execution, Metal/CUDA strict-fallback matrix | RENAME_TO_BEHAVIOR_TEST | `AccelGraphFhssChannelizerTopologyTest` with parity subcases if needed | Partial overlap with the other FHSS stage matrix files | No | No to moderate | Yes, but only because it already has topology coverage |
| [libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp) | `AccelGraphFhssDetectorTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CPU parity vs DSP detector, JSON-topology execution, Metal/CUDA strict-fallback matrix | RENAME_TO_BEHAVIOR_TEST | `AccelGraphFhssDetectorTopologyTest` with parity subcases if needed | Partial overlap with the other FHSS stage matrix files | No | No to moderate | Yes, but only because it already has topology coverage |
| [libaccelgraph/test/unit/test_accelgraph_fhss_branch_metric.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_branch_metric.cpp) | `AccelGraphFhssBranchMetricTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | CPU parity vs DSP CPSM branch metric, JSON-topology execution, Metal/CUDA strict-fallback matrix | RENAME_TO_BEHAVIOR_TEST | `AccelGraphFhssBranchMetricTopologyTest` with parity subcases if needed | Partial overlap with the other FHSS stage matrix files | No | No to moderate | Yes, but only because it already has topology coverage |
| [libaccelgraph/test/unit/test_accelgraph_fhss_e2e_hybrid.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_e2e_hybrid.cpp) | `AccelGraphFhssE2EHybridTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery`, and `libaccelgraph_fhss_phase6_hybrid` | `libaccelgraph` | End-to-end FHSS hybrid pipeline with CPU decode tail and backend fallback honesty | KEEP_AS_CONTRACT | `AccelGraphFhssHybridPipelineTest` | Some overlap with per-stage FHSS tests, but it protects a distinct full-pipeline contract | Metal and CUDA rows are host-specific | Moderate | Yes, but not as a phase suite |
| [libaccelgraph/test/unit/test_accelgraph_fhss_phase7_evidence.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_phase7_evidence.cpp) | `AccelGraphFhssPhase7EvidenceTest`; `libaccelgraph_smoke`, `libaccelgraph_smoke_discovery` | `libaccelgraph` | Evidence JSON generation, stage diagnostics, fallback honesty, discovered topology list | MOVE_TO_BENCHMARK_OR_MANUAL_VERIFICATION | `AccelGraphFhssEvidenceTest` in a benchmark/evidence lane | Yes, evidence already overlaps stage tests and is intentionally slower | Backend rows may be hardware-specific | Yes | No |
| [libaccelgraph/test/bench/accelgraph_phase7_benchmark.cpp](libaccelgraph/test/bench/accelgraph_phase7_benchmark.cpp) | `accelgraph_phase7_benchmark_smoke`; `accelgraph_phase7_benchmark` | `libaccelgraph` | Benchmark metrics, imported-result validation, benchmark artifact generation | MOVE_TO_BENCHMARK_OR_MANUAL_VERIFICATION | `AccelGraphSpectrumBenchmarkTest` or `AccelGraphBenchmarkEvidenceTest` | Yes, benchmark and evidence overlap with phase-specific smoke tests | Hardware-specific rows in configs | Yes | No |
| [libgraph/test/unit/test_graph_executor_execute_timing.cpp](libgraph/test/unit/test_graph_executor_execute_timing.cpp) | `GraphExecutorExecuteTimingTest`; `libgraph_unit` | `libgraph` | GraphExecutor timing contract, lifecycle phase accounting, deterministic timing fields | RENAME_TO_BEHAVIOR_TEST | `GraphExecutorTimingContractTest` or `GraphExecutorLifecycleTimingTest` | No | No | Yes | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step1.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step1.cpp) | `DashboardServerStep1Test; DashboardServerStep1FailureInjectionTest`; `dsp_example_unit` | `examples/DSP` | Dashboard server bootstrap, health/readiness, asset serving, schema checks, clean shutdown, startup failure modes | RENAME_TO_BEHAVIOR_TEST; MERGE_INTO_FINAL_SUITE | `FhssDashboardServerContractTest`; `FhssDashboardServerFailureTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step2.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step2.cpp) | `DashboardServerStep2Test; GraphConfigurationServiceStep2Test`; `dsp_example_unit` | `examples/DSP` | Patch validation, stale revision handling, export/replay, and failure injection | RENAME_TO_BEHAVIOR_TEST; MERGE_INTO_FINAL_SUITE | `FhssDashboardConfigurationTest`; `FhssDashboardConfigurationServiceTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step2_browser.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step2_browser.cpp) | `DashboardBrowserConcurrencyTest`; `dsp_example_unit` | `examples/DSP` | Browser-session optimistic concurrency for the dashboard config endpoint | RENAME_TO_BEHAVIOR_TEST | `FhssDashboardConfigurationConcurrencyTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step3.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step3.cpp) | `DashboardServerStep3Test`; `dsp_example_unit` | `examples/DSP` | Rebuild acceptance, activation, cleanup, and failure injection | RENAME_TO_BEHAVIOR_TEST; MERGE_INTO_FINAL_SUITE | `FhssDashboardRebuildControlTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step5.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step5.cpp) | `DashboardServerStep5Test; DashboardStep5SourceTest`; `dsp_example_unit` | `examples/DSP` | Step/continue/reset sequencing, source gating, and failure injection | RENAME_TO_BEHAVIOR_TEST; MERGE_INTO_FINAL_SUITE | `FhssDashboardMessageControlTest`; `FhssDashboardMessageSourceTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step6.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step6.cpp) | `DashboardServerStep6Test`; `dsp_example_unit` | `examples/DSP` | Event replay monotonicity, resume semantics, backpressure, reconnect failure injection | RENAME_TO_BEHAVIOR_TEST | `FhssDashboardEventReplayTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step7.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step7.cpp) | `DashboardServerStep7Test`; `dsp_example_unit` | `examples/DSP` | Schedule rendering, heatmap snapshots, and refresh bounds | RENAME_TO_BEHAVIOR_TEST | `FhssDashboardVisualizationTest` | No | No | No to moderate | Yes |
| [examples/DSP/test/test_dsp_fhss_dashboard_step8.cpp](examples/DSP/test/test_dsp_fhss_dashboard_step8.cpp) | `DashboardServerStep8Test`; `dsp_example_unit` | `examples/DSP` | Decoder diagnostics, artifact export, containment, and write-failure handling | RENAME_TO_BEHAVIOR_TEST | `FhssDashboardArtifactExportTest` | No | No | No to moderate | Yes |
| [libgraph/test/integration/test_csv_pipeline_3.cpp](libgraph/test/integration/test_csv_pipeline_3.cpp) | file/comment phase label; `libgraph_integration` | `libgraph` | CSV parser/config and full CSV pipeline integration behavior | RENAME_TO_BEHAVIOR_TEST | `CsvPipelineIntegrationTest` or `GraphCsvPipelineIntegrationTest`; remove phase-label wording from comments/file name when convenient | No | No | No to moderate | Integration lane, not default unit |

### Notes On Non-Phase But Important Final Suites

The workspace also contains behavior-oriented suites that should remain, but they are not phase-specific and do not need renaming for the phase cleanup itself:

- [libdsp/test/CMakeLists.txt](libdsp/test/CMakeLists.txt) already splits DSP/FHSS ownership cleanly into `libdsp_unit` and `libdsp_unit_discovery`.
- [libgpu/test/CMakeLists.txt](libgpu/test/CMakeLists.txt) already separates stub, backend, Metal runtime, integration, and perf lanes.
- [examples/SAR/test/CMakeLists.txt](examples/SAR/test/CMakeLists.txt) already separates CRSD IO, nodes, runtime integration, local-only/manual, discovery, and local validation lanes.

## Phase 1 Freeze Decisions

The classification table above is the accepted freeze map for this phase. No test names, CTest names, or build behavior should change in Phase 1.

### Do Not Rename Yet

- Keep all existing phase and step names intact until Phase 2 rename PRs start.
- Do not change any CMake or CTest registration in this pass.
- Do not split or merge any source files in this pass.

### Discovery Updates Required When Renamed

- `libaccelgraph_smoke_discovery` must be updated when the FHSS contract, FHSS topology, or backend-matrix suite names change.
- `libgraph_unit_discovery` must be updated once `test_graph_1.cpp` is split or renamed into behavior-focused suites.
- `dsp_example_unit` does not currently have a discovery lane, so the DSP dashboard renames can land without a discovery update, but any future discovery test should use the final behavior names.

### Jetson CUDA Verification Required

- `AccelGraphPhase4CudaTest` / `CudaGraphExecutorContractTest`
- `AccelGraphPhase5CudaGraphExecutorTest`
- `AccelGraphPhase6BCudaSpectrumTest`
- `AccelGraphFhssBranchMetricTest`
- `AccelGraphFhssChannelizerTest`
- `AccelGraphFhssDetectorTest`
- `AccelGraphFhssDownconverterTest`
- `AccelGraphFhssE2EHybridTest`
- `AccelGraphFhssPhase7EvidenceTest` and `accelgraph_phase7_benchmark`

### Move Out Of Default Fast Unit Path

- `AccelGraphFhssPhase7EvidenceTest`
- `accelgraph_phase7_benchmark_smoke`
- `threadpool_extended`
- any future FHSS evidence replay or artifact export lane that writes benchmark-grade reports

### Open Questions

- Should `libgraph/test/integration/test_graph_1.cpp` become multiple executables in Phase 2, or stay as one integration target with behavior-based suite names?
- Should FHSS parity helpers be split into dedicated `*ParityTest` files during the rename PR, or kept inside the topology files with clear subcase naming?
- Should `AccelGraphPhase4CudaTest` remain a smoke-only provider contract, or be given a separate CUDA discovery expectation once the final name is chosen?
- There is no live `examples/DSP/test/test_dsp_fhss_dashboard_step4.cpp` file today; if one appears later, it should be classified with the other dashboard behavior suites, not as a new phase milestone.

### No-Phase-Label Notice

The audit did not find an active `graph_rev2` test suite name in live test sources. That term remains documentation-only and should not drive source renames.

## Proposed Final Suite Structure

The final tree should be behavior-oriented, not phase-oriented.

### API And Unit Contract Tests

- `TopologyJsonOwnershipTest`
- `AccelGraphFhssContractTest`
- `CudaGraphExecutorContractTest`
- `AccelGraphSmokeTest`
- `GraphExecutorLifecycleTest`
- `GraphExecutorDataFlowTest`
- `GraphExecutorCompletionTest`
- `GraphExecutorConcurrencyTest`
- `GraphExecutorBuilderContractTest`

### JSON Topology And IConfigurable Ownership Tests

- `TopologyJsonOwnershipTest`
- `AccelGraphFhssDownconverterTopologyTest`
- `AccelGraphFhssChannelizerTopologyTest`
- `AccelGraphFhssDetectorTopologyTest`
- `AccelGraphFhssBranchMetricTopologyTest`
- `AccelGraphFhssHybridPipelineTest`

### Backend Strict/Fallback Matrix Tests

- `GraphExecutorBackendFallbackTest`
- `CudaGraphExecutorContractTest`
- `SpectrumTopologyTest`
- `AccelGraphFhssBackendMatrixTest`

### CPU / Reference Parity Tests

- `AccelGraphFhssDownconverterParityTest`
- `AccelGraphFhssChannelizerParityTest`
- `AccelGraphFhssDetectorParityTest`
- `AccelGraphFhssBranchMetricParityTest`
- `GraphExecutorLifecycleTest` where the behavior is a pure CPU oracle

### E2E Pipeline Smoke Tests

- `libgraph_integration` or a renamed `GraphExecutorIntegrationTest` lane for generic pipeline coverage
- `AccelGraphFhssHybridPipelineTest`
- `sar_runtime_integration` and `sar_example_ci_lane` for SAR smoke and correctness lanes

### Discovery And CTest Registration Tests

- `libgraph_unit_discovery`
- `libdsp_unit_discovery`
- `libaccelgraph_smoke_discovery`
- the SAR discovery tests in [examples/SAR/test/CMakeLists.txt](examples/SAR/test/CMakeLists.txt)

### Benchmark And Evidence Tests

- `AccelGraphFhssEvidenceTest`
- `AccelGraphBenchmarkEvidenceTest`
- `threadpool_extended`
- the SAR benchmark and trace-schema lanes when they are intentionally slow

### Hardware-Specific Verification Lanes

- `libgpu_metal_runtime`
- Metal-only accelgraph topology and fallback suites
- CUDA-only accelgraph topology, fallback, and provider suites
- Jetson-only evidence/benchmark verification where native CUDA execution must be proven on hardware

## FHSS-Specific Recommendations

FHSS is the clearest example of how to simplify the suite without weakening coverage.

### Contract And Topology Separation

- Keep [libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp) as the permanent contract test for token aliases, config parsing, and descriptor metadata.
- Keep [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) as the permanent JSON-topology ownership guardrail.
- Keep the direct `Configure(...)` calls in [libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp), [libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp), [libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp), and [libaccelgraph/test/unit/test_accelgraph_fhss_branch_metric.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_branch_metric.cpp) only while those calls remain narrow parity/unit checks, not topology tests. They should be documented as such, not treated as topology bypasses.
- If a file is renamed to `*TopologyTest`, direct `Configure(...)` calls should move to a separate `*ParityTest` or `*UnitTest` suite, or the topology ownership guardrail should explicitly exclude only the parity helper section by naming convention. A `*TopologyTest` suite should mean JSON/`GraphExecutorBuilder`/`IConfigurable` construction.
- If any future FHSS topology test starts using direct `Configure(...)`, move it into the topology ownership guardrail file and fail the test instead of normalizing the bypass.

### Recommended FHSS Renames And Merges

- `AccelGraphFhssAccelContractTest` -> `AccelGraphFhssContractTest`
- `AccelGraphFhssDownconverterTest` -> `AccelGraphFhssDownconverterTopologyTest` plus an explicit parity subcase if needed
- `AccelGraphFhssChannelizerTest` -> `AccelGraphFhssChannelizerTopologyTest` plus an explicit parity subcase if needed
- `AccelGraphFhssDetectorTest` -> `AccelGraphFhssDetectorTopologyTest` plus an explicit parity subcase if needed
- `AccelGraphFhssBranchMetricTest` -> `AccelGraphFhssBranchMetricTopologyTest` plus an explicit parity subcase if needed
- `AccelGraphFhssE2EHybridTest` -> `AccelGraphFhssHybridPipelineTest`
- `AccelGraphFhssPhase7EvidenceTest` -> `AccelGraphFhssEvidenceTest` and move it out of the default unit lane

### Why The FHSS Evidence Test Moves

`AccelGraphFhssPhase7EvidenceTest` is not a normal unit test. It writes an evidence artifact, checks backend diagnostics, and exists to support benchmark/evidence verification. The right home is a benchmark or manual-verification lane, not the default fast unit suite.

## graph_rev2, libgraph, And Legacy Phase Tests

The main graph_rev2 lesson is that phase numbers are temporary implementation scaffolding. They should disappear when the behavior is stable.

### libgraph

- [libgraph/test/integration/test_graph_1.cpp](libgraph/test/integration/test_graph_1.cpp) should be split or renamed into behavior-focused suites.
- The phase 1-5 labels currently encode lifecycle, data flow, concurrency, and builder behavior; they should be converted into stable names instead of preserved as phases.
- Timing and performance assertions should move to a benchmark lane or a dedicated timing-contract test, not remain embedded in the same phase-heavy integration file.

### libaccelgraph

- [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) should remain as a permanent ownership guardrail.
- [libaccelgraph/test/unit/test_accelgraph_phase3_metal.cpp](libaccelgraph/test/unit/test_accelgraph_phase3_metal.cpp), [libaccelgraph/test/unit/test_accelgraph_phase4_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase4_cuda.cpp), [libaccelgraph/test/unit/test_accelgraph_phase5_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase5_cuda.cpp), [libaccelgraph/test/unit/test_accelgraph_phase6_spectrum.cpp](libaccelgraph/test/unit/test_accelgraph_phase6_spectrum.cpp), and [libaccelgraph/test/unit/test_accelgraph_phase6b_spectrum_cuda.cpp](libaccelgraph/test/unit/test_accelgraph_phase6b_spectrum_cuda.cpp) should be collapsed into a smaller behavior set: transfer/backend contracts, spectrum topology, and backend fallback matrix.
- [libaccelgraph/test/unit/test_accelgraph_smoke.cpp](libaccelgraph/test/unit/test_accelgraph_smoke.cpp) is already a final smoke suite in practice and only needs its phase0 wording removed.

### libdsp

- `libdsp` already looks mostly final.
- Its FHSS tests are behavior-oriented rather than phase-oriented, so the simplification work there is mostly to keep ownership clean and avoid introducing duplicate accelgraph copies.

### examples/DSP

- The FHSS dashboard tests are step-numbered in file and suite names, for example `DashboardServerStep1Test` through `DashboardServerStep8Test`, and are built into `dsp_example_unit`.
- These steps should be renamed into feature contracts rather than preserved as implementation chronology.
- Recommended grouping:
  - `FhssDashboardServerContractTest` and `FhssDashboardServerFailureTest` for startup, health/readiness, static assets, clean shutdown, and startup failure modes.
  - `FhssDashboardConfigurationTest` and `FhssDashboardConfigurationServiceTest` for graph/config schemas, patch validation, export/replay, and failure injection.
  - `FhssDashboardRebuildControlTest`, `FhssDashboardMessageControlTest`, and `FhssDashboardMessageSourceTest` for rebuild, activation, stepping, reset, source gating, and scenario cursor behavior.
  - `FhssDashboardEventReplayTest` for monotonic event sequences, replay/resume, retention gaps, and backpressure.
  - `FhssDashboardVisualizationTest` for schedule/heatmap snapshots and refresh bounds.
  - `FhssDashboardArtifactExportTest` for decoder diagnostics, artifact bundles, path containment, and write-failure handling.
- The CTest name can remain `dsp_example_unit`; the simplification target is the step-numbered source/suite names.

## CTest And Discovery Recommendations

The key discovery rule is that the suite names and the discovery expectations must move together.

Current caveat: `AccelGraphPhase4CudaTest` is compiled into `test_libaccelgraph_smoke`, but it is not listed in `ACCELGRAPH_DISCOVERY_EXPECTED_SUITES` today. During cleanup, either add its final behavior name to discovery expectations or explicitly document why provider-shell diagnostics are smoke-only and not discovery-enforced.

### Standard CTest Should Include

- `libgraph_unit`
- `libgraph_unit_discovery`
- `libgraph_integration`
- `libdsp_unit`
- `libdsp_unit_discovery`
- `dsp_example_unit`
- `fhss_fixture_topology_generator`
- `libaccelgraph_smoke`
- `libaccelgraph_smoke_discovery`
- `libgpu_stub_unit`
- `libgpu_integration`

### Hardware-Specific CTest Should Stay Separate

- `libgpu_metal_runtime` on macOS Metal-capable hosts
- CUDA-specific accelgraph suites only on CUDA-capable hosts
- `libgpu_backend_unit` only where the backend build is actually enabled
- `libgpu_perf` only in a performance lane, not the default local unit lane

### Benchmark And Manual Lanes Should Stay Out Of Default Fast Unit

- `accelgraph_phase7_benchmark_smoke`
- `threadpool_extended`
- the SAR local-only and benchmark-style lanes

## Staged Implementation Roadmap

### Step 1: Audit And Classification Freeze

- Freeze the phase-to-final mapping and mark each suite with one of the five migration outcomes.
- Add temporary alias notes if a rename will happen in a later PR.
- Do not change behavior in this step.

### Step 2: Rename Suites Without Changing Behavior

- Rename phase-labeled suites to behavior names first.
- Update CTest and discovery expectations in the same change so the suite names remain discoverable.
- Keep old and new names temporarily only if CI needs a bridge period.

### Step 3: Merge Duplicate Backend Coverage

- Collapse the phase 3/4/5/6/6b backend matrix into fewer final suites.
- Keep one contract suite per behavior category instead of one suite per migration milestone.

### Step 4: Split Graph Executor Integration Tests

- Break [libgraph/test/integration/test_graph_1.cpp](libgraph/test/integration/test_graph_1.cpp) into behavior-focused suites.
- Move timing/performance cases to benchmark or timing-contract coverage.

### Step 5: Move Evidence And Benchmarks Out Of Default Unit Path

- Move the FHSS evidence runner to a benchmark/evidence lane.
- Keep benchmark files under `bench/` or a clearly labeled evidence lane.
- Keep evidence generation out of the default fast unit path.
- Implementation note: `AccelGraphFhssEvidenceTest` should be built and run via the dedicated `test_libaccelgraph_evidence` / `libaccelgraph_evidence` lane, not as part of `test_libaccelgraph_smoke`.

### Step 6: Update Discovery Expectations

- Update `libaccelgraph_smoke_discovery`, `libgraph_unit_discovery`, and `libdsp_unit_discovery` after the renames land.
- Keep discovery tests aligned with the actual suite names, not the old phase names.
- Decide whether the final replacement for `AccelGraphPhase4CudaTest` belongs in `libaccelgraph_smoke_discovery`. If it remains a permanent CUDA provider contract, it should be discovery-enforced under its final name.
- Implementation note: graph executor integration behavior is discovery-enforced through `libgraph_integration_discovery`; DSP dashboard behavior is discovery-enforced through `dsp_example_unit_discovery`; AccelGraph smoke discovery rejects `AccelGraphFhssEvidenceTest` so evidence remains in `libaccelgraph_evidence`.

### Step 7: Verify On macOS, Then On Jetson

- Run the macOS unit and discovery lanes first.
- Run the macOS Metal lane if Metal-specific suites are present.
- Run the Jetson CUDA lane only after the rename/merge work is stable.
- Implementation note: macOS local and macOS Metal lanes are green after the Step 6 discovery updates. The configured macOS local tree does not currently register `libgpu_integration`; include that target/test only in build trees that actually configure GPU integration sources.

### Step 8: Delete Duplicate Phase Wrappers

- Remove the old phase-named wrappers only after the final suites are green and discovery tests are updated.
- Preserve rollback by keeping one rename-only PR separate from any behavioral change.
- Implementation note (2026-07-13): removed duplicate phase-era accelgraph wrappers by consolidating transfer and spectrum backend-matrix coverage into `test_accelgraph_transfer_backend_matrix.cpp` and `test_accelgraph_spectrum_backend_matrix.cpp`, and renamed remaining contracts to `test_accelgraph_topology_json_ownership.cpp` and `test_accelgraph_cuda_graph_executor_contract.cpp`. Discovery suite names and benchmark/evidence lane ownership are unchanged.

## Verification Matrix

### macOS Local

Recommended lane:

```bash
cmake --build build-ninja/ninja-debug --target test_libgraph_unit test_libgraph_integration test_libdsp_unit test_dsp_example_unit test_libaccelgraph_smoke test_libgpu_stub_unit test_libgpu_integration
ctest --test-dir build-ninja/ninja-debug --output-on-failure -R '^(libgraph_unit|libgraph_unit_discovery|libgraph_integration|libdsp_unit|libdsp_unit_discovery|dsp_example_unit|fhss_fixture_topology_generator|libaccelgraph_smoke|libaccelgraph_smoke_discovery|libgpu_stub_unit|libgpu_integration)$'
```

`test_libgpu_integration` / `libgpu_integration` are optional for build trees that register GPU integration sources; omit them when the configured tree only has `test_libgpu_stub_unit`.

### macOS Metal

Recommended lane:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libaccelgraph_smoke test_libgpu_metal_runtime
ctest --test-dir build-ninja/ninja-debug-metal-native --output-on-failure -R '^(libgpu_metal_runtime|libaccelgraph_smoke|libaccelgraph_smoke_discovery)$'
```

If a focused FHSS Metal filter is needed during the migration, run the smoke binary with a Metal-oriented gtest filter in the Metal build tree.

### Jetson CUDA

Recommended lane:

```bash
cmake --build <jetson-build-dir> --target test_libdsp_unit test_libaccelgraph_smoke test_libgpu_backend_unit
ctest --test-dir <jetson-build-dir> --output-on-failure -R '^(libdsp_unit|libdsp_unit_discovery|libaccelgraph_smoke|libaccelgraph_smoke_discovery|libgpu_backend_unit)$'
<jetson-build-dir>/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Cuda*:*FHSS*' --gtest_brief=1
```

### Benchmark And Evidence

Recommended lane:

```bash
./build-ninja/ninja-debug/libaccelgraph/test/accelgraph_phase7_benchmark --frames=4 --warmup=1
./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_evidence --gtest_filter='AccelGraphFhssEvidenceTest.*' --gtest_brief=1
```

The evidence artifact should remain under `build/fhss_accelgraph_evidence.json`.

There are two evidence artifact families today and they should be kept distinct:

- The FHSS evidence unit helper writes `build/fhss_accelgraph_evidence.json`.
- The Phase 7 benchmark executable writes `verification/accelgraph/phase-7/macos-local-latest.json` by default, with Jetson imports and matrix reports also living under `verification/accelgraph/phase-7/`.

The long-term cleanup may rename the benchmark executable and configs away from `phase7`, but the durable verification artifacts should remain under `verification/accelgraph/`.

## Risks And Rollback Strategy

### Risks

- Renaming suites without updating discovery expectations at the same time will break CI.
- Merging phase suites too early can hide behavior-specific regressions if the replacement suite is too broad.
- Moving benchmark/evidence work into the fast path will slow the default unit lane and make the CI signal noisier.
- Hardware-specific lanes can be truthful only if the diagnostic text stays exact and the host-specific skips remain explicit.

### Rollback Strategy

- Use rename-only PRs first.
- Keep old and new suite names overlapping for one CI cycle if needed.
- Do not delete phase wrappers until the final names are green in both execution and discovery.
- If a merge is too broad, split it back into a smaller behavior suite and a separate backend-matrix suite.
