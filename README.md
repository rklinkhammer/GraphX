# GraphX

GraphX is a C++26 graph runtime and example workspace for typed graph nodes,
JSON graph configuration, plugin-loaded nodes, accelerator-ready token
contracts, DSP fixtures, FHSS decoding experiments, and SAR image-formation
pipelines.

This README is now the consolidated user guide for building, running, and
testing the repository. Historical docs were archived during the 2026-06
baseline consolidation:

- active planning baseline: `plan/BASELINE.md`
- completed cleanup roadmap: `plan/roadmap/GRAPHX_PR_ROADMAP.md`
- cleanup implementer/verifier prompts: `plan/agents/GRAPHX_PR_AGENTS.md`
- archived plan material: `plan/archive/2026-06-baseline/`
- archived user docs: `docs/archive/2026-06-baseline/`

## Repository Layout

| Path | Purpose |
|---|---|
| `libgraph/` | Graph runtime, executor, nodes, edges, plugins, config loading, policies. |
| `libgpu/` | GPU/accelerator token contracts and backend nodes for CUDA/SYCL/Metal where enabled. |
| `libdsp/` | DSP nodes, spectrum demo nodes, FHSS protocol/generator/decoder nodes and configs. |
| `libsensor/` | Sensor-side support library. |
| `examples/DSP/` | User-runnable DSP spectrum and FHSS demos plus tests. |
| `examples/SAR/` | SAR example nodes, configs, tools, fixtures, and tests. |
| `scripts/` | Repo helper scripts, including GOTCHA-to-CRSD conversion workflow. |
| `tools/sarpy/` | Local-only SarPy/reference helper tooling. |
| `plan/BASELINE.md` | Active SAR/FHSS architecture, next steps, issues, and future work. |

## Requirements

- CMake 3.23 or newer.
- Ninja. The project requires Ninja by default.
- A C++ compiler with C++26 support.
- Optional GPU/backend prerequisites:
  - CUDA: CUDAToolkit discoverable by CMake.
  - SYCL: compiler support for `-fsycl`.
  - Metal: Apple platform for Metal graph nodes.
  - Native Metal runtime: Apple frameworks plus `metal-cpp` headers.

MATLAB is not a GraphX build-time, runtime, or test-time dependency.

## Quick Start

Configure and build the default debug tree:

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
```

Configure and build the native-Metal-requested tree used by most current DSP
and FHSS examples:

```bash
cmake --preset ninja-debug-metal-native
cmake --build --preset build-debug-metal-native
```

Run the main libgraph test lane:

```bash
ctest --preset test-libgraph-unit --output-on-failure
```

Run Metal runtime tests where native Metal is available:

```bash
ctest --preset test-libgpu-metal-runtime --output-on-failure
```

## Manual Build

Without presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

If you intentionally need a non-Ninja generator:

```bash
cmake -S . -B build -G "Unix Makefiles" -DGRAPHX_REQUIRE_NINJA=OFF
```

## CMake Options

Top-level options:

| Option | Default | Description |
|---|---|---|
| `GRAPHX_ENABLE_MODULE_PILOT` | `OFF` | Enable experimental C++ module pilot mode. |
| `GRAPHX_REQUIRE_NINJA` | `ON` | Fail configure if the generator is not Ninja. |
| `BUILD_TESTS` | `ON` | Build test targets and enable CTest integration. |
| `BUILD_DOCS` | `OFF` | Enable docs build targets where available. |
| `GRAPHX_BUILD_EXAMPLES_SAR` | `ON` | Build SAR examples. |
| `GRAPHX_BUILD_EXAMPLES_DSP` | `ON` | Build DSP examples. |
| `ENABLE_TSAN` | `OFF` | Add ThreadSanitizer flags on supported compilers. |
| `ENABLE_CUDA_GRAPH_NODES` | `OFF` | Request CUDA graph node support. |
| `ENABLE_SYCL_GRAPH_NODES` | `ON` | Request SYCL graph node support. |
| `ENABLE_METAL_GRAPH_NODES` | `ON` | Request Metal graph node support. |
| `ENABLE_METAL_NATIVE_RUNTIME` | `ON` | Request native Metal runtime on Apple. |
| `GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME` | `OFF` | Fail if native Metal runtime cannot be enabled. |
| `MULTI_GPU_TESTS` | `OFF` | Enable multi-GPU test lanes when supported. |

Useful cache variables:

| Variable | Description |
|---|---|
| `GRAPHX_METAL_CPP_INCLUDE_DIR` | Optional path to the `metal-cpp` include root. |
| `CMAKE_CXX_STANDARD` | Must remain `26`. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | Enabled by default. |

## Core Test Commands

```bash
# All tests known to a build directory.
ctest --test-dir build-ninja/ninja-debug --output-on-failure

# Main graph runtime unit lane.
ctest --preset test-libgraph-unit --output-on-failure

# GPU/Metal runtime lane.
ctest --preset test-libgpu-metal-runtime --output-on-failure

# Build a summary target in test-enabled builds.
cmake --build build-ninja/ninja-debug --target test-summary
```

Focused binaries:

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit
./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
```

## GraphX Runtime And Plugin Notes

JSON graph examples should use repository-native runtime paths:

- `graph::GraphExecutorBuilder`
- `WithJsonConfig(...)`
- `WithPluginDirectory(...)`
- `WithExecutorTimeout(...)`
- real GraphX node classes
- `graph::gpu::accel::ControlToken<...>` where the node contract is
  accelerator-ready

Avoid inventing local graph adaptors or bypassing the GraphX executor in user
examples. Historical helper/pseudo-node documentation is archived and is not the
active architecture.

## DSP Spectrum Demo And GPU DFT Lane

The runnable spectrum demo is a CPU-only direct DFT reference lane. CPU-only
direct DFT is the intended truth-in-labeling description for this path:

```text
SineSignalNode<256>
  -> CpuSpectrumDftNode<float, 256>
  -> SpectrumSinkNode<float, 256>
```

Build and run:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target dsp_spectrum_demo

./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins
```

Write a deterministic summary:

```bash
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins \
  --summary-json "$tmpdir/spectrum_summary.json"
```

Run the informational CPU-vs-Metal direct DFT comparison:

```bash
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --warmup-iterations 1 \
  --measured-iterations 3 \
  --executor-timeout-s 8 \
  --report-json "$tmpdir/dsp_cpu_vs_metal_report.json"
```

Truth-in-labeling:

- The default spectrum demo is CPU-only.
- `CpuSpectrumDftNode<float, 256>` is a direct DFT implementation.
- `MetalSpectrumDftNode<256>` is a Metal direct DFT, not a GPU FFT.
- CPU-vs-Metal reports are execute-timing comparisons measured on the current host/config.
  They are not general performance claims.
- Timing comes from the `GraphExecutor::Execute()` ExecutionResult fields used
  by the demo runner.
- The optional strict speedup gate is local-only and not part of default CI.
- A future true Metal FFT lane must use FFT naming only after a real FFT
  algorithm is implemented.

Focused tests:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_dsp_example_unit

./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit \
  --gtest_filter='DspSpectrumDemoExecutableTest.*:DspSpectrumDemoGuardrailTest.*:DspCpuVsMetalExecuteTimingTest.*'
```

## DSP FHSS Demo

The FHSS lane is a deterministic CPU fixture and decoder. It is not a
production RF receiver.

Canonical channelized graph:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
  -> PerChannelPulseDetectorNode[64]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

Build and run the bundled canonical graph:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target dsp_fhss_demo

./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_cpsm_channelized_fixture_500msps.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --executor-timeout-s 12
```

Run with an external message schedule and write investigation artifacts:

```bash
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-fhss-demo \
  --message-json examples/DSP/fixtures/fhss_demo_messages.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --summary-json "$tmpdir/fhss_summary.json" \
  --effective-config-json "$tmpdir/fhss_effective_graph.json" \
  --decoded-pulse-limit 8 \
  --executor-timeout-s 12
```

External message JSON may be a full `FHSSSyntheticIqSourceNode` `node_config`
object or an object with a `node_config` field. It must provide `messages[]`.
Each pulse must provide `frequency_index`, `value`, and `role`.

Example pulse:

```json
{
  "frequency_index": 24,
  "value": 2863311530,
  "role": "preamble"
}
```

FHSS fixture limits:

- sample rate: `500 Msps`;
- bit rate: `5 Mbps`;
- 64 RF metadata entries;
- selectable transmit indices `[1, 62]`;
- reserved receiver guard/metadata indices `0` and `63`;
- 16 hop-only preamble pulses;
- four active transmit frequencies;
- maximum 256 pulses including preamble.

FHSS truth-in-labeling:

- 1 GHz RF frequencies are metadata; fixture IQ uses baseband/IF offsets.
- The 500 Msps fixture cannot represent the full 64-entry 1 GHz RF table as
  direct sampled RF, and it does not use or implement direct 1 GHz RF sampling.
- Guardrail wording: cannot represent the full 64-entry 1 GHz RF table as direct sampled RF.
- The full 64-entry RF table is not one simultaneous alias-free 500 Msps RF
  capture.
- Magnitude-only DFT/FFT output is not the canonical decoder input.
- Guardrail wording: complex IQ evidence; CPSM branch metrics; Viterbi/MLSE.
- The deterministic CPU fixture channelizer is not a production channelizer.
  Do not claim production channelizer separation claims or production
  channelizer performance.
- Doppler, noise, multipath, overlap-aware separation, real RF capture, and
  production channelizer performance are not implemented.
- Overlap is unsupported.
- Metal/GPU acceleration of the FHSS lane is future work.
- PDW diagnostics as canonical decoder output are not part of the active
  decoder contract.
- Occupied-bandwidth and channel-filter requirements remain unresolved, so the
  current docs must not claim production channelizer separation.
- Receiver configuration preserves one logical GraphX channel output port per
  configured frequency.
- Guardrail wording: channel output port per configured frequency.
- Guardrail wording: retuned sub-band windows.
- Guardrail wording: explicit alias/downconvert modeling.
- The RF table must not be described as one simultaneous alias-free 500 Msps
  complex-baseband capture.
- Canonical impairment status values include `configured_rejected` and
  `unsupported_impairments_rejected`; PDW diagnostics remain optional and
  non-canonical.
- Guardrail wording: PDW diagnostics remain optional and non-canonical.
- Guardrail wording: deleted pre-GraphX pseudo-node scaffolding is not the current node model.
- The canonical channelized graph is the only active FHSS receiver topology.
- The old correlator-bank graph, node, config, and plugin were removed from
  active support; do not describe them as retained, canonical, or
  production-like.

Focused tests:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_dsp_example_unit

./build-ninja/ninja-debug-metal-native/examples/DSP/test/test_dsp_example_unit \
  --gtest_filter='DspFhssDemoExecutableTest.*'
```

FHSS library/graph tests:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit

./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit \
  --gtest_filter='*FHSS*:*CPSM*'
```

## SAR Architecture

Active SAR lanes:

1. deterministic synthetic stripmap examples;
2. GOTCHA `.mat` to CRSD conversion;
3. ordered CRSD set ingest;
4. CRSD aperture assembly into SAR phase-history packets;
5. CPU focused-image transform;
6. Metal focused-image transform where native Metal is available;
7. focused-image sink artifact generation;
8. optional local-only reference comparison.

Important SAR boundaries:

- CRSD is the supported conversion output for SAR ingest and comparison flows.
- Real GOTCHA data validation is local-only and disabled by default.
- SarPy and gotcha-back are optional local reference tools, not GraphX runtime
  dependencies.
- Quick-look CRSD signal inspection is not focused-image acceptance evidence.
- Metal transfer/sync/memory nodes are valid Metal nodes, but they are not proof
  of domain compute acceleration by themselves.

### SAR Metal Truth-In-Labeling

Transfer/memory/sync/control nodes are valid Metal nodes without kernels. They
must not be described as proof of domain algorithm acceleration. Domain
algorithm nodes and generic kernel nodes must report diagnostics that make their
execution status explicit.

Current inventory:

| Node | Classification |
|---|---|
| `HostIngressPinnedSourceNodeMetal` | memory/control |
| `H2DAsyncNodeMetal` | transfer |
| `D2HAsyncNodeMetal` | transfer |
| `PeerCopyNodeMetal` | transfer |
| `DeviceShardNodeMetal` | memory/control |
| `LeaseReleaseNodeMetal` | memory |
| `QueueSyncNodeMetal` | sync/control |
| `HostEgressSinkNodeMetal` | memory/control |
| `DeviceKernelNodeMetal` | generic kernel |
| `DeviceTransformNodeMetal` | generic kernel |
| `DeviceReduceNodeMetal` | generic kernel |
| `CollectiveReduceNodeMetal` | unsupported |
| `CrsdFocusedImageTransformMetalNode` | domain algorithm |

Guardrail labels: H2DAsyncNodeMetal | transfer; QueueSyncNodeMetal | sync/control; LeaseReleaseNodeMetal | memory.

PR6 gate: blocked until unsupported/experimental Metal behavior is either
implemented, removed from the active lane, or clearly reported as disabled.
`CollectiveReduceNodeMetal | unsupported` remains a truth-in-labeling guardrail.
`CrsdFocusedImageTransformMetalNode | domain algorithm` is allowed only with
explicit diagnostics. Any experimental incomplete path must remain labeled
`experimental incomplete`.

## SAR Build And Test

Build SAR examples and tests:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target sar_example test_sar_example_unit graphx_gotcha_to_crsd
```

Run all SAR example tests:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
```

Useful focused filters:

```bash
# CI-safe correctness lane.
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarCiCorrectnessLaneTest.*:SarCiValidationLaneTest.*:SarCiTinyFixtureTest.*'

# CRSD input and focused-image path.
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdInputSourceNodeTest.*:CrsdApertureAssemblyAdapterNodeTest.*:CrsdFocusedImageTransformNodeTest.*:CrsdFocusedImageSinkTest.*'

# Metal SAR truth-in-labeling and focused-image transform.
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='MetalTruthInLabelingGuardrailTest.*:CrsdFocusedImageMetalTest.*'
```

Registered CTest lanes include:

| CTest lane | Purpose |
|---|---|
| `sar_example_unit` | Full SAR example unit test binary. |
| `sar_example_ci_lane` | CI-safe SAR validation lane. |
| `sar_example_main_executable` | `examples/SAR/main.cpp` executable coverage. |
| `sar_example_sarpy_probe_lane` | Local-only/gated SarPy probe checks. |
| `sar_example_sarpy_integration_lane` | Local-only/gated SarPy integration checks. |
| `sar_real_gotcha_local_validation` | Disabled local-only real-data GOTCHA validation. |

Run through CTest:

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native -R sar_example_unit --output-on-failure
ctest --test-dir build-ninja/ninja-debug-metal-native -R sar_example_ci_lane --output-on-failure
```

## SAR Example Graphs

PR7 canonical SAR configs (active set of 13 configs):

| Config | Purpose |
|---|---|
| `examples/SAR/config/sar_stripmap_simulated.json` | Canonical CPU stripmap graph. |
| `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json` | Canonical CPU CRSD focused-image lane. |
| `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json` | Canonical GPU CRSD focused-image Metal lane (experimental). |
| `examples/SAR/config/sar_crsd_gotcha_local_validation.json` | Local-only GOTCHA-derived CRSD validation graph. |

Specialized variants for dedicated test scenarios:

| Config | Purpose |
|---|---|
| `examples/SAR/config/sar_stripmap_definitive.json` | Definitive stripmap example graph. |
| `examples/SAR/config/sar_stripmap_fanout.json` | Stripmap fanout variant. |
| `examples/SAR/config/sar_stripmap_matched_filter.json` | Matched filter variant. |
| `examples/SAR/config/sar_stripmap_materialized_image.json` | Materialized image variant. |
| `examples/SAR/config/sar_projectile_approach.json` | Projectile scenario. |
| `examples/SAR/config/sar_crsd_tiny_fixture_full_pipeline.json` | Full pipeline variant. |
| `examples/SAR/config/sar_crsd_tiny_fixture_set_input.json` | Path-based input mode. |
| `examples/SAR/config/sar_crsd_tiny_fixture_set_input_directory.json` | Directory-based input mode. |
| `examples/SAR/config/sar_crsd_tiny_fixture_set_input_manifest.json` | Manifest-based input mode. |

SAR GPU-path truth-in-labeling:

- Current canonical SAR GPU path:
  `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json`.
- It remains experimental/incomplete until explicitly promoted by tests and
  baseline updates.
- PR7 deleted orphaned and unused Metal configs. No second canonical SAR GPU
  path is supported.

Example run of the SAR executable:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/sar_example \
  examples/SAR/config/sar_stripmap_definitive.json
```

## GOTCHA To CRSD Conversion

Build the converter:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target graphx_gotcha_to_crsd
```

Convert a local GOTCHA directory to CRSD products:

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /path/to/gotcha_or_synthetic_mat_dir \
  /tmp/gotcha_crsd_out
```

The helper script verifies input structure and builds the converter when
needed. Classic MATLAB MAT v5 input may be preprocessed to HDF5-backed MAT
files before conversion.

Expected output shape:

| Output | Purpose |
|---|---|
| `gotcha_crsd_chunk_0000.crsd/product.crsd` | CRSD product container. |
| `gotcha_crsd_chunk_0000.crsd/metadata.json` | Product metadata sidecar. |
| `gotcha_crsd_chunk_0000.crsd/signal.bin` | Signal payload for fixture/product tests. |
| `gotcha_crsd_chunk_0000.crsd/index.json` | Chunk index. |
| `gotcha_crsd_index.json` | Optional root index when enabled. |
| `conversion_report.json` | Optional conversion report. |
| `conversion_warnings.log` | Optional warning log. |

Manual converter invocation:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/graphx-gotcha-to-crsd \
  --input /path/to/input.mat \
  --output-dir /tmp/gotcha_crsd_out
```

GOTCHA input manifests use schema `graphx.gotcha.input_manifest.v1`.
Manifest order is authoritative. The ordering layer does not use MATLAB and
does not parse MAT contents; it only resolves and validates the declared file
order before downstream readers inspect payloads.

## Local-Only Reference And Comparison Tools

The following tools are for local validation and investigation. They are not
GraphX runtime dependencies and should not be required by default CI. SarPy is a
local-only product/metadata validation only harness.
Guardrail wording: not proof of GraphX phase-history image-formation correctness.

| Tool | Purpose |
|---|---|
| `examples/SAR/tools/sar_local_runner.py` | Scaffold local SAR run layouts. |
| `examples/SAR/tools/sar_local_baseline_runner.py` | Local-only external baseline smoke runner with explicit opt-in gating. |
| `examples/SAR/tools/sar_graphx_vs_baseline_harness.py` | GraphX-vs-baseline comparison harness for CI-safe tiny fixture and local-only baseline comparisons. |
| `examples/SAR/tools/sar_baseline_substitution_experiment.py` | Local-only experiment replacing the selected baseline image-formation stage with GraphX output at an artifact-contract boundary. |
| `examples/SAR/tools/sar_scenario_to_run.py` | Convert scenario JSON to local run setup. |
| `examples/SAR/tools/sar_image_comparator.py` | Compare focused-image artifacts. |
| `examples/SAR/tools/gotcha_back_adapter.py` | Prepare local gotcha-back reference runs. |
| `tools/sarpy/reference_image_from_gotcha.py` | Local SarPy/GOTCHA reference helper. |
| `tools/sarpy/reference_image_from_crsd.py` | Local SarPy/CRSD reference helper. |
| `tools/sarpy/compare_images.py` | Local image comparison helper. |

Example local scaffold:

```bash
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_scenario_001
```

PR14 local-only baseline runner (SarPy selected baseline):

```bash
python3 examples/SAR/tools/sar_local_baseline_runner.py \
  probe-environment \
  --output-json /tmp/graphx_sar_local_baseline_probe.json

GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1 \
GRAPHX_SARPY_CRSD_FILE=/path/to/local/product.crsd \
python3 examples/SAR/tools/sar_local_baseline_runner.py \
  run-local-smoke \
  --output-json /tmp/graphx_sar_local_baseline_smoke.json
```

Truth-in-labeling: this runner is local-only, opt-in, and not a GraphX runtime dependency.

PR15 GraphX-vs-baseline SAR comparison harness:

```bash
python3 examples/SAR/tools/sar_graphx_vs_baseline_harness.py \
  run-ci-tiny-fixture \
  --output-dir /tmp/graphx_vs_baseline_tiny \
  --output-json /tmp/graphx_vs_baseline_tiny_result.json \
  --strict

GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1 \
python3 examples/SAR/tools/sar_graphx_vs_baseline_harness.py \
  run-local-comparison \
  --graphx-contract /path/to/graphx_contract.json \
  --reference-contract /path/to/reference_contract.json \
  --output-json /tmp/graphx_vs_baseline_local_result.json \
  --strict
```

Comparison metrics are validation aids and are not production SAR claims.

PR16 local-only stage substitution experiment:

```bash
GRAPHX_SAR_BASELINE_SUBSTITUTION_ENABLE=1 \
python3 examples/SAR/tools/sar_baseline_substitution_experiment.py \
  run-local-substitution \
  --graphx-stage-contract /path/to/graphx_contract.json \
  --baseline-reference-contract /path/to/sarpy_contract.json \
  --output-json /tmp/graphx_sar_substitution_result.json \
  --strict
```

The experiment replaces the selected baseline image-formation stage only at
the artifact-contract boundary. It does not modify SarPy, does not create a
second canonical SAR GPU path, and is not a production SAR claim.

## Real GOTCHA Validation

Real GOTCHA validation is opt-in/local-only.

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/gotcha_mat_directory
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  "$GRAPHX_SAR_GOTCHA_DATASET" \
  /tmp/gotcha_crsd_out
```

The disabled CTest lane can be enabled manually when local data exists:

```bash
ctest --test-dir build-ninja/ninja-debug-metal-native \
  -R sar_real_gotcha_local_validation \
  --output-on-failure
```

## Documentation Baseline

Active documentation is intentionally small:

- `README.md`: build, run, test, and user operations.
- `plan/BASELINE.md`: active architecture, next steps, issues, and future work.

Historical documentation remains available for traceability:

- `docs/archive/2026-06-baseline/`
- `plan/archive/2026-06-baseline/`

Do not add new user-facing docs under `docs/` unless the project intentionally
splits the README again. Do not add new active plan roadmaps under `plan/`
unless they become the new baseline or are explicitly archived after use.

The cleanup roadmap in `plan/roadmap/GRAPHX_PR_ROADMAP.md` is complete. New
work should begin from `plan/BASELINE.md` rather than extending the closed
cleanup sequence.
