# FHSS Upgrade Prompt

```text
You are working in the GraphX repository. Upgrade the existing frequency-hopping spread-spectrum (FHSS) graph into an accelerator-aware libaccelgraph RF/DSP workload.

Ignore SAR for this effort. SAR is out of scope.

Work autonomously: inspect the repository, implement the requested phase, build it, run relevant tests, fix failures, and clearly report any hardware-specific lanes that must be verified on macOS Metal or Jetson CUDA. Preserve unrelated user changes. Follow the repository's existing CMake, plugin, GraphExecutorBuilder, JSON topology, descriptor metadata, and graph_rev2 conventions.

Core invariant:

- topology-style graph tests must use checked-in JSON topology files;
- graph construction must flow through GraphExecutorBuilder, plugin descriptors, and the JSON loader;
- node initialization must flow through JSON node_config plus IConfigurable;
- topology tests must not manually call Configure(...), ConfigureNode, ConfigureTransferNode, or ad hoc JsonView(...) setup;
- CPU reference behavior must remain available for parity and diagnostics;
- backend-specific tests must report exact, actionable diagnostics or clean skips when native Metal/CUDA support is unavailable.

## Current Repository Context

Existing FHSS system:

- libdsp/include/dsp/fhss
- libdsp/src/dsp
- libdsp/plugins
- libdsp/config/fhss_cpsm_channelized_fixture_500msps.json
- examples/DSP/src/fhss_demo.cpp

Existing accelerator framework:

- libaccelgraph/include/accelgraph/Accelerator.hpp
- libaccelgraph/include/accelgraph/TransferGraphNodes.hpp
- libaccelgraph/include/accelgraph/SpectrumGraphNodes.hpp
- libaccelgraph/src/CpuAcceleratorProvider.cpp
- libaccelgraph/src/CudaAcceleratorProvider.cpp
- libaccelgraph/test/config/topologies
- libaccelgraph/test/unit

Important observation:

The FHSS graph already uses accelerator-ready token wrappers:

- FHSSGraphXToken = graph::gpu::accel::ControlToken<T>
- FHSSSyntheticIqToken
- FHSSDownconvertedIqToken
- FHSSChannelizedIqToken
- FHSSPerChannelPulseEvidenceToken
- FHSSCpsmBranchMetricToken
- FHSSCpsmSymbolDecisionToken
- FHSSDecodedPulseWordToken
- FHSSAssembledMessageToken

Use this existing token model rather than inventing a parallel transport model.

## Target Architecture

Create a libaccelgraph FHSS vertical slice that can execute through JSON topology and progressively move numerically heavy RF/DSP stages onto the accelerator framework.

Target pipeline:

FHSSSyntheticIqSource -> FHSSDownconverter -> FHSSChannelizer -> PerChannelPulseDetector -> PulseMerge -> CPSM/BranchMetrics -> Viterbi/WordDecode -> PreambleDetect -> MessageAssemble -> MessageSink

Recommended migration order:

1. AccelFhssDownconverterNode
2. AccelFhssChannelizerNode
3. AccelFhssPerChannelDetectorNode
4. AccelFhssBranchMetricNode
5. optional later GPU Viterbi/batched decode if profiling justifies it

Keep stateful protocol stages on CPU until numeric GPU stages are stable.

## Phase 0: Audit And Design Record

### macOS Implementor

Inspect the current FHSS and accelgraph code and write a short design record before implementation.

Audit:

- existing FHSS JSON graph and plugin nodes;
- FHSS node config fields and IConfigurable usage;
- existing FHSS token and packet types;
- accelgraph session, transfer, fallback, and topology-test conventions;
- current CPU/Metal/CUDA provider capabilities;
- what should remain in libdsp versus move to libaccelgraph.

Design record should cover:

- first libaccelgraph FHSS nodes to implement;
- shared packet/token ownership strategy;
- CPU reference parity strategy;
- backend selection and fallback policy;
- topology JSON naming conventions;
- macOS and Jetson verification lanes.

Suggested output file:

- plan/reviews/FHSS_ACCELGRAPH_UPGRADE_DESIGN.md

Verification:

- git status --short
- rg "FHSS|fhss" libdsp libaccelgraph examples/DSP -g '!examples/SAR/**'
- no code behavior changes required in Phase 0 unless a trivial build/documentation issue is discovered.

### Jetson CUDA Verifier

No Jetson implementation is required for Phase 0. If requested, verify that the current CUDA-enabled tree still builds and that existing libaccelgraph CUDA smoke tests pass.

## Phase 1: Shared FHSS Accel Contracts

### macOS Implementor

Create the minimal shared FHSS accel contracts in libaccelgraph without changing the existing libdsp FHSS graph behavior.

Tasks:

1. Add libaccelgraph FHSS headers under a clear namespace, for example:
   - libaccelgraph/include/accelgraph/fhss/FHSSAccelTypes.hpp
   - libaccelgraph/include/accelgraph/fhss/FHSSAccelConfig.hpp
2. Reuse or bridge existing libdsp FHSS packet/token types where practical.
3. Define backend/fallback config shared by future FHSS accel nodes:
   - backend: cpu, metal, cuda
   - fallback_policy: strict, allow
   - strict_fallback
   - provider_id
   - session_key
   - cuda_device_ordinal
4. Add descriptor metadata helpers or Fields() conventions for all config keys.
5. Do not move DSP algorithms yet.
6. Add narrow unit tests proving config parsing and descriptor metadata.

Verification:

- cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke
- ./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests
- focused new FHSS accel contract tests
- jq -e . libaccelgraph/test/config/topologies/*.json
- rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit
- git diff --check

### Jetson CUDA Verifier

Build the CUDA-enabled tree and verify the new shared contracts compile with ACCELGRAPH_ENABLE_CUDA=ON.

Run:

- cmake --build <jetson-build-dir> --target test_libaccelgraph_smoke
- <jetson-build-dir>/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests
- focused new FHSS accel contract tests
- ctest --test-dir <jetson-build-dir> -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure
- git diff --check

Report whether CUDA-specific config fields and discovery expectations compile cleanly.

## Phase 2: Accel FHSS Downconverter Node

### macOS Implementor

Implement the first real RF/DSP accelerator-aware node:

- AccelFhssDownconverterNode

This node should match the existing FHSSDownconverterNode semantics:

- passthrough mode;
- translated mode;
- input/output IQ center frequency metadata;
- reference frequency metadata;
- translation frequency;
- phase convention;
- sample rate;
- input/output sample-time map preservation.

Tasks:

1. Add node implementation in libaccelgraph.
2. Add plugin registration.
3. Add checked-in topology JSON files:
   - accelgraph_fhss_downconverter_cpu_topology.json
   - accelgraph_fhss_downconverter_metal_topology.json
   - accelgraph_fhss_downconverter_metal_allow_fallback_topology.json
   - accelgraph_fhss_downconverter_cuda_topology.json if CUDA topology can be checked in without requiring macOS execution
   - accelgraph_fhss_downconverter_cuda_allow_fallback_topology.json if useful
4. Use existing FHSS synthetic IQ source behavior or add a minimal accelgraph-compatible source wrapper.
5. Add CPU reference parity tests against existing libdsp FHSSDownconverterNode.
6. Add Metal path tests that execute or skip with exact diagnostics.
7. Ensure all config is JSON-owned.

Implementation guidance:

- CPU implementation may call the existing libdsp downconverter kernel/logic to preserve correctness.
- Metal/CUDA can initially use provider selection plus CPU fallback if native kernel is not yet implemented, but strict native mode must fail honestly.
- Do not claim native GPU execution until a native kernel path actually runs.

Verification:

- cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke
- ./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Downconverter*' --gtest_brief=1
- ./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests
- ctest --test-dir build-ninja/ninja-debug -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure
- jq -e . libaccelgraph/test/config/topologies/*.json
- rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit
- git diff --check

### Jetson CUDA Verifier

Verify the CUDA-enabled downconverter lane.

Run:

- cmake --build <jetson-build-dir> --target test_libaccelgraph_smoke
- <jetson-build-dir>/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests
- <jetson-build-dir>/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Downconverter*' --gtest_brief=1
- ctest --test-dir <jetson-build-dir> -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure
- jq -e . libaccelgraph/test/config/topologies/*.json
- git diff --check

Verify:

- CUDA strict topology executes natively if implemented;
- otherwise strict CUDA fails/skips with exact diagnostic;
- CUDA allow-fallback topology passes if fallback is supported;
- CPU parity remains green.

## Phase 3: Accel FHSS Channelizer Node

### macOS Implementor

Implement an accelgraph FHSS channelizer path.

Start with a fixture-compatible channelizer if that is the smallest safe step. A production polyphase filter bank can follow later.

Tasks:

1. Add AccelFhssChannelizerNode.
2. Preserve the existing FHSS invariant of one output port per configured frequency where applicable.
3. Support at least the existing 64-channel fixture contract or a deliberately smaller synthetic subset with explicit naming.
4. Add JSON topologies for CPU and Metal, plus CUDA topologies for Jetson verification.
5. Add parity tests against existing FHSSFixtureFrequencyChannelizerNode.
6. Add topology contract matrix entries and discovery expectations.

Verification:

- cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke
- ./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Channelizer*' --gtest_brief=1
- ctest --test-dir build-ninja/ninja-debug -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure
- jq -e . libaccelgraph/test/config/topologies/*.json
- guardrail rg for manual Configure bypasses
- git diff --check

### Jetson CUDA Verifier

Run the same focused FHSS channelizer tests on CUDA-enabled Jetson.

Verify:

- CUDA strict/allow behavior is truthful;
- channel count and output port mapping match contract;
- no aggregate "all channels at once" shortcut sneaks in unless explicitly designed and tested;
- CPU/CUDA parity is within declared tolerance.

## Phase 4: Accel FHSS Per-Channel Detector

### macOS Implementor

Implement an accelgraph per-channel pulse detector stage.

Tasks:

1. Add AccelFhssPerChannelPulseDetectorNode.
2. Preserve existing metadata requirements:
   - channel id;
   - frequency index;
   - global sample timing;
   - sample rate;
   - power/SNR/noise-floor diagnostics where currently available.
3. Add CPU parity tests against existing PerChannelPulseDetectorNode.
4. Add GPU strict/fallback topologies.
5. Ensure detector thresholds and config fields are declared by descriptor metadata.

Verification:

- focused FHSS detector tests
- full libaccelgraph smoke/discovery CTest
- JSON validation
- manual Configure bypass guard
- git diff --check

### Jetson CUDA Verifier

Run detector tests on CUDA-enabled Jetson and verify:

- strict CUDA path behavior;
- allow-fallback behavior;
- output pulse metadata parity;
- no diagnostic regressions.

## Phase 5: Accel FHSS Branch Metrics

### macOS Implementor

Implement GPU-friendly branch metric computation for CPSM/FHSS symbol decisions.

Tasks:

1. Add AccelFhssBranchMetricNode.
2. Keep Viterbi/word decode CPU unless profiling proves GPU value.
3. Add parity tests against existing CPSMBranchMetricNode.
4. Add topologies that connect:
   - source/downconverter/channelizer/detector/branch-metric/sink or CPU decoder boundary.
5. Preserve packet metadata and timing.

Verification:

- focused branch metric tests
- CPU/GPU parity
- libaccelgraph smoke/discovery CTest
- JSON and guardrail checks

### Jetson CUDA Verifier

Run branch metric CUDA tests and confirm strict/allow behavior and parity.

## Phase 6: End-To-End Hybrid FHSS Graph

### macOS Implementor

Create the first end-to-end hybrid FHSS accelgraph topology.

Suggested split:

- accelerator-aware source/downconverter/channelizer/detector/branch-metric;
- existing CPU merge/decode/preamble/message assembly/sink.

Tasks:

1. Add an end-to-end JSON topology under libaccelgraph/test/config/topologies.
2. Load only through GraphExecutorBuilder.
3. Compare decoded messages against the existing deterministic FHSS fixture expectation.
4. Add backend variants:
   - CPU reference;
   - Metal strict;
   - Metal allow fallback;
   - CUDA strict topology checked in for Jetson;
   - CUDA allow fallback topology checked in for Jetson.
5. Add dashboard/demo integration only if the test lane is already stable.

Verification:

- full focused FHSS accelgraph tests;
- existing libdsp FHSS tests if shared code changed;
- libaccelgraph smoke/discovery;
- JSON validation;
- guardrail search;
- git diff --check.

### Jetson CUDA Verifier

Run the end-to-end hybrid FHSS graph on CUDA-enabled Jetson.

Verify:

- decoded message parity;
- strict CUDA behavior;
- allow-fallback behavior;
- no CPU-only path is mislabeled as native CUDA;
- no manual configuration bypasses.

## Phase 7: Benchmark And Evidence Artifacts

### macOS Implementor

Add benchmark/evidence reporting for FHSS accelgraph.

Tasks:

1. Add benchmark config files under libaccelgraph/test/config/benchmarks or an FHSS-specific benchmark directory.
2. Measure:
   - total graph execution time;
   - per-stage timing where available;
   - selected backend;
   - fallback usage;
   - decoded-message correctness;
   - throughput in samples/sec or pulses/sec.
3. Emit JSON artifacts suitable for macOS and Jetson comparison.
4. Avoid direct ConfigureNode-style benchmark setup; use topology JSON and GraphExecutorBuilder.

Verification:

- benchmark smoke test;
- correctness parity;
- JSON artifact schema sanity;
- guardrail search.

### Jetson CUDA Verifier

Run the FHSS accelgraph benchmark on Jetson CUDA and report:

- native CUDA versus CPU reference timing;
- correctness parity;
- fallback status;
- hardware diagnostics.

## Phase 8: Cleanup, Documentation, And Closure

### macOS Implementor

Finalize the FHSS accelgraph upgrade.

Tasks:

1. Document how to add new FHSS accelgraph nodes.
2. Document topology naming and backend/fallback rules.
3. Ensure discovery tests include FHSS accel suites.
4. Ensure no stale or disconnected topology nodes remain.
5. Update this prompt's completion notes or create a final report.
6. Confirm libdsp FHSS remains intact and documented as CPU/reference or legacy path.

Verification:

- full relevant libaccelgraph smoke/discovery;
- focused libdsp FHSS tests if shared contracts changed;
- JSON validation;
- guardrail search;
- git diff --check.

### Jetson CUDA Verifier

Run final CUDA verification:

- build test_libaccelgraph_smoke;
- run all FHSS accelgraph focused suites;
- run libaccelgraph smoke/discovery CTest;
- run benchmark smoke if present;
- report final CUDA status and any pushed changes.

## Acceptance Criteria

- FHSS has a libaccelgraph accelerator-aware vertical slice.
- At least downconverter and one additional numerically meaningful FHSS stage are modeled as libaccelgraph nodes.
- Checked-in JSON topologies cover CPU, Metal, CUDA, strict fallback, and allow-fallback variants where meaningful.
- CPU reference parity remains available.
- Tests are in standard libaccelgraph smoke/discovery lanes.
- CUDA-specific behavior is verified on Jetson.
- No topology-style tests manually configure nodes after graph construction.
- Backend diagnostics truthfully distinguish native GPU execution, fallback, and unavailable hardware.

## Final Report Requirements

Finish each phase with:

- files changed;
- new nodes/plugins/topologies;
- descriptor/config metadata changes;
- tests added or updated;
- build/test commands run;
- pass/fail status;
- hardware skip reasons;
- whether Jetson verification is required or complete;
- remaining follow-up work.
```

## Completion Notes

- Phase 0 complete (macOS): Added design record [plan/reviews/FHSS_ACCELGRAPH_UPGRADE_DESIGN.md](plan/reviews/FHSS_ACCELGRAPH_UPGRADE_DESIGN.md) with node sequence, fallback policy guidance, topology naming, and Jetson verification lane planning. No code behavior changes in this phase.
- Phase 1 complete (macOS): Added shared FHSS accel contract surface in [libaccelgraph/include/accelgraph/fhss/FHSSAccelTypes.hpp](libaccelgraph/include/accelgraph/fhss/FHSSAccelTypes.hpp) and [libaccelgraph/include/accelgraph/fhss/FHSSAccelConfig.hpp](libaccelgraph/include/accelgraph/fhss/FHSSAccelConfig.hpp), plus focused unit coverage in [libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_accel_contract.cpp). Wired smoke discovery expectations in [libaccelgraph/test/CMakeLists.txt](libaccelgraph/test/CMakeLists.txt).
- Phase 1 verification (macOS):
   - `git status --short`
   - `cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke`
   - `build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests`
   - `build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*AccelContract*' --gtest_brief=1` (8/8 passed)
   - `jq -e . libaccelgraph/test/config/topologies/*.json >/dev/null`
   - `rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit`
   - `git diff --check`
- Phase 2 complete (macOS): Implemented first accelerator-aware FHSS RF/DSP node `AccelFhssDownconverterNode` in [libaccelgraph/include/accelgraph/fhss/FHSSDownconverterGraphNode.hpp](libaccelgraph/include/accelgraph/fhss/FHSSDownconverterGraphNode.hpp) and [libaccelgraph/src/FHSSDownconverterGraphNode.cpp](libaccelgraph/src/FHSSDownconverterGraphNode.cpp), with plugin registration in [libaccelgraph/plugins/accel_fhss_downconverter_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_downconverter_node_plugin.cpp). Added topology sink plugin [libaccelgraph/plugins/accel_fhss_downconverter_sink_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_downconverter_sink_node_plugin.cpp) for JSON-owned topology validation.
- Phase 2 topology set added:
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cpu_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cpu_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_metal_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_metal_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_metal_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_metal_allow_fallback_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cuda_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cuda_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cuda_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_downconverter_cuda_allow_fallback_topology.json)
- Phase 2 tests added/updated:
   - [libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_downconverter.cpp)
   - [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) matrix and descriptor coverage updated for new FHSS topologies/node descriptors.
   - [libaccelgraph/test/CMakeLists.txt](libaccelgraph/test/CMakeLists.txt) smoke/discovery updated with new FHSS suite and plugin dependencies.
- Phase 2 verification (macOS):
   - `git status --short`
   - `cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Downconverter*' --gtest_brief=1`
   - `ctest --test-dir build-ninja/ninja-debug -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure`
   - `jq -e . libaccelgraph/test/config/topologies/*.json >/dev/null`
   - `rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit`
   - `git diff --check`
- Phase 3 complete (macOS): Implemented `AccelFhssChannelizerNode` and topology sink in [libaccelgraph/include/accelgraph/fhss/FHSSChannelizerGraphNode.hpp](libaccelgraph/include/accelgraph/fhss/FHSSChannelizerGraphNode.hpp) and [libaccelgraph/src/FHSSChannelizerGraphNode.cpp](libaccelgraph/src/FHSSChannelizerGraphNode.cpp), with plugin registration in [libaccelgraph/plugins/accel_fhss_channelizer_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_channelizer_node_plugin.cpp) and [libaccelgraph/plugins/accel_fhss_channelizer_sink_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_channelizer_sink_node_plugin.cpp). Descriptor metadata now includes shared accel config fields plus channelizer-specific fields including `iq_capture`.
- Phase 3 topology set added:
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cpu_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cpu_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_metal_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_metal_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_metal_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_metal_allow_fallback_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cuda_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cuda_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cuda_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_channelizer_cuda_allow_fallback_topology.json)
- Phase 3 tests added/updated:
   - [libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_channelizer.cpp) (CPU parity vs `FHSSFixtureFrequencyChannelizerNode`, topology execution via `GraphExecutorBuilder`, strict-metal execute-or-skip, allow-fallback behavior, descriptor-field coverage).
   - [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) matrix expanded for channelizer CPU/Metal/CUDA strict+allow topologies, descriptor checks expanded, strict-skip diagnostics updated, and sink connectivity allowances updated.
   - [libaccelgraph/test/CMakeLists.txt](libaccelgraph/test/CMakeLists.txt) updated with channelizer plugin dependencies and discovery expectation for `AccelGraphFhssChannelizerTest`.
- Phase 3 verification (macOS):
   - `cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Channelizer*' --gtest_brief=1`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests`
   - `ctest --test-dir build-ninja/ninja-debug -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure`
   - `jq -e . libaccelgraph/test/config/topologies/*.json >/dev/null`
   - `rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit`
   - `git diff --check`
- Phase 3 status: macOS lane complete and green. Jetson CUDA verification remains required to validate native CUDA strict/allow behavior on ACCELGRAPH_ENABLE_CUDA=ON builds.
- Phase 4 complete (macOS): Implemented `AccelFhssPerChannelPulseDetectorNode` and topology sink in [libaccelgraph/include/accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp](libaccelgraph/include/accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp) and [libaccelgraph/src/FHSSPerChannelPulseDetectorGraphNode.cpp](libaccelgraph/src/FHSSPerChannelPulseDetectorGraphNode.cpp), with plugin registration in [libaccelgraph/plugins/accel_fhss_per_channel_pulse_detector_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_per_channel_pulse_detector_node_plugin.cpp) and [libaccelgraph/plugins/accel_fhss_per_channel_pulse_detector_sink_node_plugin.cpp](libaccelgraph/plugins/accel_fhss_per_channel_pulse_detector_sink_node_plugin.cpp). Detector node exposes backend/fallback controls plus detector threshold/config fields through descriptor metadata.
- Phase 4 topology set added:
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cpu_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cpu_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_detector_metal_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_detector_metal_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_detector_metal_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_detector_metal_allow_fallback_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cuda_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cuda_topology.json)
   - [libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cuda_allow_fallback_topology.json](libaccelgraph/test/config/topologies/accelgraph_fhss_detector_cuda_allow_fallback_topology.json)
- Phase 4 tests added/updated:
   - [libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp](libaccelgraph/test/unit/test_accelgraph_fhss_detector.cpp) (CPU parity vs `PerChannelPulseDetectorNode`, topology execution via `GraphExecutorBuilder`, strict-metal execute-or-skip, allow-fallback behavior, metadata preservation checks, descriptor-field coverage).
   - [libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp](libaccelgraph/test/unit/test_accelgraph_phase2_topology_contract.cpp) matrix expanded for detector CPU/Metal/CUDA strict+allow topologies, descriptor checks expanded, strict-skip diagnostics updated, and sink connectivity allowances updated.
   - [libaccelgraph/test/CMakeLists.txt](libaccelgraph/test/CMakeLists.txt) updated with detector plugin dependencies and discovery expectation for `AccelGraphFhssDetectorTest`.
- Phase 4 verification (macOS):
   - `cmake --build build-ninja/ninja-debug --target test_libaccelgraph_smoke`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_list_tests`
   - `./build-ninja/ninja-debug/libaccelgraph/test/test_libaccelgraph_smoke --gtest_filter='*Fhss*:*FHSS*:*Detector*:*Pulse*' --gtest_brief=1`
   - `ctest --test-dir build-ninja/ninja-debug -R "libaccelgraph_smoke|libaccelgraph_smoke_discovery" --output-on-failure`
   - `jq -e . libaccelgraph/test/config/topologies/*.json >/dev/null`
   - `rg "ConfigureNode|ConfigureTransferNode|JsonView\\(|->Configure\\(|\\.Configure\\(" libaccelgraph/test/unit`
   - `git diff --check`
- Phase 4 status: macOS lane complete and green. Jetson CUDA verification remains required to validate native CUDA strict/allow behavior on ACCELGRAPH_ENABLE_CUDA=ON builds.
