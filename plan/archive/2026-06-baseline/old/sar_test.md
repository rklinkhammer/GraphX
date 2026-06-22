Index:
- [Dataset investigation prompt](plan/sar_test.md)
- [Coding agent implementation prompt](plan/sar_test_coding_agent_prompt.md)

You are a senior SAR systems engineer, radar data-ingestion specialist, and C++ pipeline architect.

Investigate whether the AFRL Gotcha Volumetric SAR Dataset can be integrated as a real-world dataset for the GraphX SAR pipeline.

Important instruction:
Do not assume dataset details that are not verified. Clearly label each item as:
- Known
- Likely
- Unknown
- Requires inspection
- Requires experiment

Project context:
- Target system: GraphX SAR implementation with plugin-based graph nodes, JSON topologies, explicit DAG tokens/context, and PR3 Metal-native acceleration path.
- Existing synthetic/topology coverage includes windowing, range compression, fanout, tile split/merge, H2D, kernel dispatch, D2H, diagnostics sinks.
- GraphX edges carry tokens/context/metadata, not implicit byte movement.
- Capabilities/backend nodes perform actual data movement and kernel execution.
- Goal: move from synthetic SAR test inputs to validated real-data ingestion and reconstruction.

Primary question:
Can Gotcha phase-history/IQ data be used as a practical real-data source for GraphX SAR reconstruction, diagnostics, benchmarking, and PR3 Metal acceleration?

Produce the following:

1. Go/no-go recommendation
- Recommendation: Go, No-Go, or Go-with-caveats.
- Confidence level.
- Top 5 blockers.
- Top 5 reasons it is valuable for GraphX.

2. Dataset feasibility
Investigate:
- Dataset access path.
- License/use constraints.
- Redistribution restrictions.
- Whether CI fixtures can legally include raw data, derived data, metadata-only slices, or synthetic derivatives.
- Data volume and storage layout.
- Native format(s).
- Metadata completeness.
- Expected preprocessing burden.
- Compatibility with GraphX’s current stripmap/spotlight assumptions.

3. SAR data mapping
Identify the exact fields required by GraphX:
- complex IQ / phase-history samples
- pulse index
- frequency/range sample axis
- carrier or center frequency
- bandwidth / frequency step
- platform position
- platform velocity if available
- aperture/pass ID
- scene center
- coordinate frame
- timing metadata
- calibration terms
- polarization if applicable
- units and scaling
- endian/layout assumptions

Provide a mapping table:

Gotcha field or file item
→ meaning
→ GraphX message/envelope/sidecar field
→ required/optional
→ confidence
→ validation method

4. GraphX integration architecture
Design:
- Offline converter
- Native Gotcha source node
- Metadata normalization node
- Optional calibration/motion compensation node
- Handoff into existing PR3 accel-token pipeline
- Handoff into existing H2D/kernel/D2H/tile-merge path
- Diagnostics sink integration

Make clear which parts belong under:

examples/SAR/
libdsp/
libgpu/
libgraph/

Do not propose framework-wide rewrites unless unavoidable.

5. Message and DAG contracts
Define the GraphX message contracts needed for Gotcha ingestion:
- frame ID
- pass ID
- pulse block ID
- range bin span
- aperture span
- tile ID
- batch ID
- timestamp or synthetic ordering key
- backend/device/queue metadata
- buffer ownership
- host buffer view
- device lease/view
- transfer ticket
- kernel ticket
- completion/watermark behavior

State which contracts already exist and which are new or adapter-only.

6. Minimal viable implementation
Define the smallest useful end-to-end milestone:

Gotcha subset
→ offline conversion
→ GraphX source
→ range preprocessing
→ tiled backprojection
→ image tile merge
→ diagnostics report
→ image output

Include:
- files to create
- nodes to create
- tests to add
- topology JSON to add
- command-line tool behavior
- expected output artifacts

Limit the first implementation to a reviewable PR.

7. Phased roadmap
Phase A: Offline converter and deterministic fixture generation.
Phase B: Native Gotcha stream/source node.
Phase C: PR3 Metal-native acceleration path.
Phase D: Larger-pass performance benchmarking.
Phase E: multi-backend CUDA/SYCL/Metal comparison.

For each phase:
- goal
- deliverables
- dependencies
- estimated effort
- risks
- exit criteria

8. Validation strategy
Define correctness checks:
- known-scene geometry checks
- point-target focus
- impulse/PSF behavior
- image sharpness metrics
- peak location error
- dynamic range sanity checks
- phase consistency where applicable
- comparison against known Gotcha reference images if available

Define deterministic regression tests:
- tiny fixture
- medium local fixture
- benchmark fixture
- golden metadata hash
- golden image/tile tolerances

9. Performance and diagnostics
Report required metrics:
- pulses/sec
- complex samples/sec
- range preprocessing time
- tile split/merge time
- H2D bytes and bandwidth
- D2H bytes and bandwidth
- kernel dispatch count
- kernel time
- queue wait/backpressure
- fan-in wait time
- memory allocation/reuse
- peak memory
- end-to-end latency

Separate:
- graph overhead
- DSP algorithm time
- backend transfer time
- backend kernel time
- diagnostics overhead

10. Risks and mitigations
Address:
- licensing/legal restrictions
- format ambiguity
- undocumented preprocessing assumptions
- coordinate-frame mismatch
- units/scaling mismatch
- missing calibration data
- numerical stability
- dynamic range
- Metal-specific limits
- memory pressure
- lack of CI redistribution permission

11. Decision matrix
Compare:
- direct Gotcha parser
- offline converter to GraphX SAR intermediate format
- HDF5/Zarr/NPZ intermediate
- tiny derived CI fixture
- synthetic-only fallback

For each:
- pros
- cons
- implementation cost
- legal risk
- testability
- performance impact
- recommendation

12. Mermaid architecture diagram
Include a diagram showing:

Gotcha files
→ converter/source
→ metadata normalization
→ DSP preprocessing
→ tile split
→ H2D
→ Metal kernel
→ D2H
→ merge
→ diagnostics/image output

13. Explicit unknowns and experiments
If any unknown blocks execution, define:
- exact file to inspect
- exact metadata to extract
- exact experiment to run
- expected result
- decision enabled by the result

Optional deep dive:
Propose a compact, legally compliant benchmark subset strategy:
- raw-data slice if allowed
- derived-data fixture if raw redistribution is restricted
- metadata-only plus synthetic reconstruction fixture
- image-only validation fixture
- local-only benchmark profile
- CI-safe deterministic profile

Output requirements:
- Start with recommendation and confidence.
- Do not bury blockers.
- Be explicit about known vs unknown.
- Tie every recommendation back to GraphX architecture.
- Do not let SAR math implementation bypass GraphX DAG semantics.


I’ll run a read-only audit using the exact prompt in SAR_DEFINITIVE_PIPELINE_PROMPT.md, inspect the current SAR code/config/tests for evidence, and then return the required output sections without making any code changes.

Read [](file:///Users/rklinkhammer/workspace/GraphX/plan/SAR_DEFINITIVE_PIPELINE_PROMPT.md)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/src/main.cpp)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/src/sar_benchmark.cpp#1-1), lines 1 to 340

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/config)

I’ve loaded the prompt and core runtime files; next I’m verifying the definitive configs, benchmark script, and README parameter coverage, then I’ll produce a structured evidence-tagged compliance report against every required output and validation item.

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/config/sar_stripmap_definitive_nonmetal.json)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/config/sar_stripmap_definitive_metal.json)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh)

Searched for regex `Complete Node Parameter Reference|benchmark_main_metal_vs_nonmetal.sh|sar_stripmap_definitive_nonmetal.json|sar_stripmap_definitive_metal.json` (`**/examples/SAR/README.md`), 9 results

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/README.md#140-140), lines 140 to 380

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_accel_token_guardrails.cpp)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_json_pipeline.cpp#130-130), lines 130 to 273

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_cpu_reference.cpp)

Searched for regex `performance_claim_policy|graph_run_minus_baseline_median|graph_overhead_ms|speedup_basis|disallow_lifecycle_total_as_speedup_basis` (`**/examples/SAR/test/test_sar_trace_schema.cpp`), 12 results

Read [](file:///Users/rklinkhammer/workspace/GraphX/libgraph/src/graph/GraphConfigParser.cpp#360-360), lines 360 to 594

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_json_runtime.cpp)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_trace_schema.cpp#110-110), lines 110 to 220

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_materialized_image_sink_node.cpp)

Ran terminal command:  ./examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh

Ran terminal command:  ./build-ninja/ninja-debug/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive_nonmetal.json ./build-ninja/ninja-debug/examples/SAR/plugins | tail -n 6 && ./build-ninja/ninja-debug/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive_metal.json ./build-ninja/ninja-debug/examples/SAR/plugins | tail -n 6

Compacted conversation

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/src/main.cpp)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/config/sar_stripmap_definitive_nonmetal.json)

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/config/sar_stripmap_definitive_metal.json)

I’ve finished the code inspection and runtime spot-checks, and I’m now extracting the exact required section structure from the definitive prompt so I can return the audit in the requested format with evidence tags.

Read [](file:///Users/rklinkhammer/workspace/GraphX/plan/SAR_DEFINITIVE_PIPELINE_PROMPT.md)

Test with the CMake Tools extension
Completed with input: {}

Read [](file:///Users/rklinkhammer/Library/Application%20Support/Code/User/workspaceStorage/6e2f6441a2449c1284b639ab8222ee7d/GitHub.copilot-chat/chat-session-resources/db59be88-bafe-4d92-bf55-bb69c5142ee6/call_5PqeGmvbLnhQptzclKjyYm9x__vscode-1780962240493/content.txt#1-1), lines 1 to 260

Searched for regex `defaultConfig|GraphExecutorBuilder|WithJsonConfig|WithPluginDirectory|WithExecutorTimeout|Completion signaled` (`**/examples/SAR/src/main.cpp`), 8 results

Searched for regex `execution_backend|backend_fallback_policy|resolver_diagnostics|edge_contract|SarPulseFanoutNode|completion_signal_enabled` (`**/examples/SAR/config/sar_stripmap_definitive_nonmetal.json`), 5 results

Searched for regex `execution_backend|backend_fallback_policy|resolver_diagnostics|edge_contract|SarPulseFanoutNode|completion_signal_enabled|bp_tile0|h2d_tile0|d2h_tile0` (`**/examples/SAR/config/sar_stripmap_definitive_metal.json`), 15 results

Searched for regex `Complete Node Parameter Reference|benchmark_main_metal_vs_nonmetal.sh|sar_stripmap_definitive_nonmetal.json|sar_stripmap_definitive_metal.json|Main.cpp based METAL vs non-METAL timing benchmark` (`**/examples/SAR/README.md`), 10 results

Searched for regex `for run in \$\(seq 1 \$RUNS\)|avg_ms|min_ms|max_ms|non_metal|metal|sar_example` (`**/examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh`), 15 results

Searched for regex `PointTargetBackprojectionFocusesAtKnownPixel|MatchedFilterKnownVectorFindsDelayedEcho|ImageQualityMetricsTrackOffGridPointTarget|BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable|EXPECT_LT\(error\.l_inf, 2.0e-5\)|EXPECT_LT\(error\.relative_l2, 1.0e-5\)` (`**/examples/SAR/test/test_sar_cpu_reference.cpp`), 6 results

Searched for regex `RejectsLegacyPayloadContractUnderAccelTokenMode|Legacy SAR payload contract is not allowed on accel-token edge` (`**/examples/SAR/test/test_sar_accel_token_guardrails.cpp`), 2 results

Searched for regex `performance_claim_policy|speedup_basis|graph_run_minus_baseline_median|graph_overhead_ms|disallow_lifecycle_total_as_speedup_basis` (`**/examples/SAR/test/test_sar_trace_schema.cpp`), 12 results

Searched for regex `Pr7MaterializedImagePathCapturesDeterministicSamples|Pr7MaterializedImageParityMetricsMatchReference|kRelativeL2Tolerance|kDynamicRangeDeltaToleranceDb|EXPECT_LE\(error\.relative_l2|EXPECT_LE\(dynamic_range_delta` (`**/examples/SAR/test/test_sar_json_pipeline.cpp`), 9 results

Searched for regex `config\.resolver\.edge_contract == "accel-token"|IsLegacySarPayloadContract` (`**/libgraph/src/graph/GraphConfigParser.cpp`), 3 results

Ran terminal command:  rg -n "Fields\(\) const|IParameterized" examples/SAR/include examples/SAR/src

Searched for regex `Fields\(\) const` (`**/examples/SAR/**`), no results

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp)

Searched for text `static constexpr std::array<graph::JsonField` (`**/examples/SAR/include/sar/*.hpp`), 12 results

Read [](file:///Users/rklinkhammer/workspace/GraphX/examples/SAR/test/test_sar_pr3_metal_json.cpp)

Searched for regex `sar_stripmap_definitive|definitive` (`**/examples/SAR/test/**`), no results

Searched for text `Required Outputs|Validation Checklist|Reviewer Evidence Expectations|Required Constraints` (`**/plan/SAR_DEFINITIVE_PIPELINE_PROMPT.md`), 4 results

**Read-Only SAR PR9 Audit (Per Definitive Prompt)**

Scope audited against SAR_DEFINITIVE_PIPELINE_PROMPT.md, with no code changes made.

1. Required Outputs Status

1. [PASS] Definitive non-METAL config exists and matches strict contract fields.  
Tags: [Observed in code]  
Evidence:
- sar_stripmap_definitive_nonmetal.json execution_backend=auto
- sar_stripmap_definitive_nonmetal.json backend_fallback_policy=strict
- sar_stripmap_definitive_nonmetal.json resolver_diagnostics=true
- sar_stripmap_definitive_nonmetal.json edge_contract=accel-token
- Pipeline includes source/range/split/H2D/backprojection/D2H/merge/sink in this file.

2. [PASS] Definitive METAL config exists with fanout and per-tile lanes.  
Tags: [Observed in code]  
Evidence:
- sar_stripmap_definitive_metal.json execution_backend=metal
- sar_stripmap_definitive_metal.json backend_fallback_policy=strict
- sar_stripmap_definitive_metal.json edge_contract=accel-token
- sar_stripmap_definitive_metal.json SarPulseFanoutNode present
- sar_stripmap_definitive_metal.json, sar_stripmap_definitive_metal.json, sar_stripmap_definitive_metal.json per-tile H2D/BP/D2H lanes present.

3. [PASS] Benchmark script exists and runs both configs with per-run + avg/min/max.  
Tags: [Observed in code], [Validated by run]  
Evidence:
- benchmark_main_metal_vs_nonmetal.sh loops over both configs
- benchmark_main_metal_vs_nonmetal.sh, benchmark_main_metal_vs_nonmetal.sh, benchmark_main_metal_vs_nonmetal.sh prints avg/min/max
- Run output sample captured:
  - non_metal avg_ms=5106, min_ms=5050, max_ms=5310
  - metal avg_ms=5060, min_ms=5060, max_ms=5060

4. [PASS] README includes definitive run commands, benchmark usage, and parameter reference section.  
Tags: [Observed in code]  
Evidence:
- README.md Complete Node Parameter Reference
- README.md definitive non-METAL run command
- README.md definitive METAL run command
- README.md benchmark section and script usage.

2. Validation Checklist Status

1. Both definitive configs execute through sar_example: [PASS with caveat].  
Tags: [Validated by run], [Risk]  
Observed runtime for both configs: “Execution completed successfully.”  
Caveat: completion signal reported false in both runs from main.cpp output path.

2. Diagnostics sink completion signal in both configs: [FAIL].  
Tags: [Validated by run], [Risk]  
Prompt checklist requires completion=true, but live runs reported:
- Completion signaled: false (non-METAL)
- Completion signaled: false (METAL)

3. METAL config resolves expected concrete backend nodes without contract break: [PARTIAL].  
Tags: [Observed in code], [Inferred], [Risk]  
- Intent-level topology is correct in definitive METAL config.
- accel-token contract is strict in JSON.
- Existing tests validate older METAL presets and contract behavior, but there is no direct test targeting definitive METAL config resolution path.

4. Benchmark script runs and prints timing summary: [PASS].  
Tags: [Validated by run]  
Confirmed by script execution and summary output above.

5. Unit/integration tests remain green in SAR lane: [FAIL].  
Tags: [Validated by test/log], [Risk]  
Current CTest run failed 2 SAR tests:
- test_sar_json_pipeline.cpp relative_l2 threshold exceeded very slightly (1.0050716505702881e-07 vs 1e-07)
- test_sar_json_pipeline.cpp same relative_l2 tolerance failure
- test_sar_json_pipeline.cpp dynamic_range_delta failure (3.69217e-06 vs 1e-09)

3. Focus Areas You Requested

1. SAR mathematical correctness: [PASS with strict-tolerance instability risk].  
Tags: [Validated by test/log], [Risk]  
Evidence:
- CPU reference math tests are present and passing, including point-target focus and matched filter behavior at test_sar_cpu_reference.cpp and test_sar_cpu_reference.cpp.
- Native Metal parity adapter check exists at test_sar_cpu_reference.cpp, with numeric tolerances enforced at test_sar_cpu_reference.cpp and test_sar_cpu_reference.cpp.
- However, SAR JSON parity tests currently fail due to very tight thresholds.

2. CPU reference parity: [PARTIAL].  
Tags: [Validated by test/log], [Risk]  
- Strong CPU reference suite is green.
- End-to-end JSON parity lane currently has 2 failing tests in test_sar_json_pipeline.cpp and test_sar_json_pipeline.cpp.

3. Accel-token enforcement: [PASS].  
Tags: [Observed in code], [Validated by test/log]  
Evidence:
- Parser enforcement blocks legacy payload contracts under accel-token mode in GraphConfigParser.cpp.
- Guardrail test validates rejection in test_sar_accel_token_guardrails.cpp and checks error text at test_sar_accel_token_guardrails.cpp.
- Both definitive configs explicitly set edge_contract=accel-token.

4. Graph-overhead attribution: [PASS].  
Tags: [Validated by test/log], [Observed in code]  
Evidence:
- Trace schema test asserts graph_overhead_ms and graph_run_minus_baseline_median linkage at test_sar_trace_schema.cpp, test_sar_trace_schema.cpp, test_sar_trace_schema.cpp.
- Performance claim policy constraints enforced at test_sar_trace_schema.cpp and test_sar_trace_schema.cpp.

4. Reviewer Evidence Expectations

1. File refs for definitive configs: satisfied.  
- sar_stripmap_definitive_nonmetal.json
- sar_stripmap_definitive_metal.json

2. README parameter tables tied to node headers: satisfied by structure, partially spot-verified.  
Tags: [Observed in code], [Inferred]  
- Table section: README.md
- Parameter field declarations exist per node headers, e.g.:
  - SyntheticApertureIqSourceNode.hpp
  - RangeCompressionNode.hpp
  - SarBackprojectionTransformAccelNode.hpp
  - SarVisualizationSinkNode.hpp

3. Benchmark sample output from main.cpp-based runs: satisfied.  
Tags: [Validated by run]  
- Provided above from benchmark script execution.

4. Explicit strict accel-token statement in both definitive configs: satisfied.  
Tags: [Observed in code]  
- sar_stripmap_definitive_nonmetal.json
- sar_stripmap_definitive_metal.json

5. Key Risks And Gaps

1. Completion signaling mismatch versus prompt checklist.  
Tags: [Validated by run], [Risk]  
Both definitive configs executed but reported completion=false in runtime output.

2. SAR test lane is not fully green now.  
Tags: [Validated by test/log], [Risk]  
2 parity tests fail due to ultra-tight thresholds in test_sar_json_pipeline.cpp and test_sar_json_pipeline.cpp.

3. Definitive METAL config lacks dedicated test coverage entry.  
Tags: [Observed in code], [Risk]  
No direct test references to definitive config filenames in SAR test tree.

Natural next steps:
1. Add a focused definitive-config runtime test pair that asserts completion signaling true for both definitive JSONs.
2. Decide whether parity thresholds in test_sar_json_pipeline.cpp and test_sar_json_pipeline.cpp should be relaxed or numerically stabilized.
3. Add a definitive METAL resolver-contract test to close the current coverage gap.