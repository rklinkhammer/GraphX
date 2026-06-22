# GraphX Inspector Report

Date: 2026-06-22

Role source: `plan/agents/GRAPHX_AGENT_ROLES.md`

Scope: Current repository inspection only. No redesign or implementation work
was performed.

## 1. Current Architecture Summary

### Observed

- GraphX is configured as a C++26 project. The top-level `CMakeLists.txt`
  requires `CMAKE_CXX_STANDARD 26` and fails configure if another standard is
  selected.
- The active baseline is `plan/BASELINE.md`; the active user guide is
  `README.md`.
- The core runtime is organized under `libgraph/` with:
  - typed graph nodes and port abstractions;
  - `GraphExecutor` and `GraphExecutorBuilder`;
  - JSON config parsing/loading;
  - dynamic plugin/provider loading;
  - node facade/interoperability layers;
  - policies for metrics, completion, data injection, commands, and dashboard
    integration;
  - repeated-port helpers such as `RoutedInputFn`, `RoutedOutputFn`,
    `RoutedTransferFn`, and `NamedFixedFanInOutNode`.
- GPU/accelerator support is organized under `libgpu/` with backend-neutral
  accelerator types, CUDA/SYCL/Metal capability stubs, native Metal capability
  implementation, and plugin-loadable backend nodes.
- DSP support is organized under `libdsp/` with spectrum nodes, DSP GPU token
  bridge nodes, FHSS protocol/generator/decoder nodes, FHSS graph packet
  contracts, and DSP/FHSS plugins.
- SAR is implemented as an example domain under `examples/SAR/` with GraphX
  nodes, plugins, JSON configs, CRSD/GOTCHA tooling, local reference helpers,
  and a large focused test suite.
- User-runnable examples exist for:
  - DSP spectrum: `examples/DSP/graphx-dsp-spectrum-demo`;
  - DSP FHSS: `examples/DSP/graphx-dsp-fhss-demo`;
  - SAR example graph execution: `examples/SAR/sar_example`;
  - SAR benchmarking: `examples/SAR/sar_benchmark`;
  - GOTCHA-to-CRSD conversion: `examples/SAR/graphx-gotcha-to-crsd`.

### Inferred

- The current project center of gravity is now broader than SAR: GraphX core,
  DSP spectrum, FHSS/SDR-like IQ processing, GPU acceleration contracts, and
  SAR all have active code and tests.
- The repository is in a documentation-baseline transition state: active docs
  are consolidated, while older `doc/` and archived plan/docs trees remain
  present for reference.

### Unknown

- I did not run the full test suite during this inspection, so current
  whole-repository pass/fail status is unknown.

## 2. Current Type And Packet Model

### Observed

- Backend-neutral accelerator types are defined in
  `libgpu/include/gpu/accel/types/AccelTypes.hpp`.
- The accelerator model includes:
  - `BackendKind`;
  - `DataType`;
  - `TensorLayout`;
  - `DeviceBufferView`;
  - `HostPinnedBufferView`;
  - `BufferLease`;
  - `TransferTicket`;
  - `KernelTicket`;
  - `ControlToken<SidecarT>`.
- DSP spectrum packet types include `IqPacket` and `MagnitudePacket`.
- DSP spectrum GraphX edges use accelerator tokens in the current node
  contracts. Examples include:
  - `ControlToken<graph::message::Message>` for IQ-bearing message sidecars;
  - `ControlToken<MagnitudePacket<SampleT, N>>` for magnitude spectra.
- FHSS packet contracts are defined in
  `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`.
- FHSS token aliases are centralized in
  `libdsp/include/dsp/fhss/FHSSGraphXNodeUtils.hpp` as
  `graph::gpu::accel::ControlToken<PacketT>` wrappers.
- FHSS packet contracts include:
  - `FHSSSyntheticIqOutputPacket`;
  - `FHSSDownconvertedIqPacket`;
  - `FHSSChannelizedIqPacket`;
  - `FHSSPerChannelPulseEvidencePacket`;
  - `FHSSDetectedPulseEvidencePacket`;
  - `FHSSPulseCandidateEvidencePacket`;
  - `FHSSCpsmBranchMetricPacket`;
  - `FHSSCpsmSymbolDecisionPacket`;
  - `FHSSDecodedPulseWordsPacket`;
  - `FHSSAssembledMessagePacket`;
  - `FHSSDiagnosticsPacket`.
- FHSS complex evidence is represented by shared immutable host sample vectors
  plus sample-time metadata, residency, sample format, and decoder-use flags.
- SAR domain token identity is represented by `SarSidecar` and
  `SarAccelControlToken = graph::gpu::accel::ControlToken<SarSidecar>` in
  `examples/SAR/include/sar/SarMessages.hpp`.
- SAR sidecar metadata includes sequence, batch, aperture, pulse range, stream,
  tile, backend, frame marker, payload byte count, queue ids, transfer/kernel
  timings, merge diagnostics, and tile accounting.

### Inferred

- The accelerator-token sidecar model is established across DSP, FHSS, and SAR,
  but payload semantics differ by domain.
- SAR and FHSS both explicitly separate domain identity from GPU transport
  details.

### Unknown

- I did not inspect every packet transformation for full sidecar preservation;
  only representative headers and source matches were sampled.

## 3. Current Node And Port Model

### Observed

- Core GraphX provides `SourceNode`, `SinkNode`, `NamedInteriorNode`,
  `NamedSourceNode`, `NamedSinkNode`, `NodeFacade`, runtime ports, and dynamic
  edge support.
- `RoutedInputFn`, `RoutedOutputFn`, and `RoutedTransferFn` are present and
  route CRTP port calls to templated node methods.
- `NamedFixedFanInOutNode` is present in `FixedFanInOutNode.hpp` and builds on
  routed input/output helpers for fixed fan-in/fan-out nodes.
- FHSS nodes are split into per-node headers/sources under
  `libdsp/include/dsp/fhss/` and `libdsp/src/dsp/`.
- FHSS public node classes observed include:
  - `FHSSSyntheticIqSourceNode`;
  - `FHSSCorrelatorBankDetectorNode`;
  - `FHSSDownconverterNode`;
  - `ChannelizerNode`;
  - `PerChannelPulseDetectorNode`;
  - `FHSSPulseMergeNode`;
  - `FHSSPulseCandidateNode`;
  - `CPSMBranchMetricNode`;
  - `CPSMViterbiDecoderNode`;
  - `FHSSPulseWordDecoderNode`;
  - `FHSSPreambleDetectorNode`;
  - `FHSSMessageAssemblerNode`;
  - `FHSSMessageSinkNode`.
- `ChannelizerNode` declares one input token type and 64 output ports by
  deriving from `SourceNode<...>` over a generated repeated type list.
- `FHSSPulseMergeNode` derives from `NamedFixedFanInOutNode` and accepts:
  - one `FHSSDetectedPulseToken` input;
  - 64 `FHSSPerChannelPulseEvidenceToken` inputs;
  - two `FHSSPulseCandidateToken` outputs.
- SAR public example nodes use GraphX node bases and `SarAccelControlToken`.
  Examples include `OrderedCrsdSetInputSourceNode`,
  `CrsdApertureAssemblyAdapterNode`, `CrsdFocusedImageTransformNode`,
  `CrsdFocusedImageTransformMetalNode`, `H2DAsyncAccelNode`,
  `D2HAsyncAccelNode`, `ImageTileMergeNode`, and
  `SarDiagnosticsSinkNode`.
- GPU backend nodes exist for CUDA/SYCL stubs and Metal, including transfer,
  memory, sync/control, device transform, reduce, kernel, shard, peer copy, and
  collective reduce nodes.

### Inferred

- FHSS repeated-port work is partially generalized through GraphX base helpers,
  with `FHSSPulseMergeNode` using the newer fixed fan-in/out base.
- `ChannelizerNode` still uses a custom generated `TypeList`/`SourceNode`
  approach rather than the fixed fan-in/out base because it is a source-style
  64-output node with one consumed input.

### Unknown

- I did not perform a line-by-line audit of every node's lifecycle or port
  metadata behavior.

## 4. Current Token/Data Flow

### Observed

- DSP CPU spectrum config:
  `SineSignalNode<256> -> CpuSpectrumDftNode<256> -> SpectrumSinkNode<256>`.
- DSP Metal DFT config:
  `SineSignalNode<256> -> DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256> -> SpectrumSinkNode<256>`.
- FHSS canonical channelized config is
  `libdsp/config/fhss_cpsm_channelized_fixture_500msps.json`.
- FHSS canonical graph role fields are present:
  - `"fhss_graph_role": "canonical_channelized_fixture"`;
  - `"canonical_fhss_graph": true`;
  - `"reference_only": false`.
- FHSS reference correlator-bank config is
  `libdsp/config/fhss_cpsm_fixture_500msps.json` with
  `"reference_only": true`.
- FHSS canonical node chain in the config includes source, downconverter,
  channelizer, per-channel detectors, merge, candidate, branch metric, Viterbi,
  word decoder, preamble detector, assembler, and sink.
- SAR configs generally declare `"edge_contract": "accel-token"` and
  `"resolver_diagnostics": true`.
- SAR token transport fields `host_ptr` and `ready_event` still exist as
  accelerator transport fields. SAR code comments and tests state these are
  opaque transport metadata and not SAR identity.

### Inferred

- DSP and FHSS are moving toward a consistent token sidecar model, while SAR
  already has a domain-specific `SarAccelControlToken` sidecar contract.
- The FHSS canonical path preserves complex IQ evidence through downstream
  decoding packets by contract.

### Unknown

- I did not run the FHSS graph executor in this inspection turn; runtime
  behavior is based on source/config/test inspection.

## 5. Current Plugin/Provider And Resolver Flow

### Observed

- `GraphExecutorBuilder` is used by DSP examples, FHSS demo, SAR examples,
  SAR benchmark, libgraph tests, and SAR tests.
- Plugin targets are defined in:
  - `libdsp/plugins/CMakeLists.txt`;
  - `libgpu/plugins/CMakeLists.txt`;
  - `examples/SAR/plugins/CMakeLists.txt`;
  - `libgraph/test/plugins/CMakeLists.txt`.
- Built plugins currently present in the metal-native build tree include DSP,
  FHSS, GPU, and test plugins.
- FHSS plugin dylibs are present for every observed FHSS graph node, including
  channelizer, per-channel detector, pulse merge, CPSM, word decode, message
  assembly, and sink nodes.
- Graph config parser support for resolver diagnostics and `edge_contract` is
  present in `GraphConfigParser.cpp`.
- Resolver diagnostics are stored in graph build results and tested in SAR and
  DSP graph runtime tests.
- `NodeProviderBootstrap`, `RegisteredNodeProvider`, `ResolvingNodeProvider`,
  `PluginLoader`, and `PluginRegistry` exist in the runtime.

### Inferred

- Dynamic loading is a first-class path for examples and tests, not only a
  legacy plugin feature.
- The resolver is used for backend intent mapping and diagnostics in SAR
  accelerator configs.

### Unknown

- I did not inspect every plugin descriptor for all advertised metadata fields.

## 6. SDR/DSP/FHSS/SAR Capability Status

### DSP

Observed:

- CPU direct DFT spectrum lane exists and is documented as CPU-only.
- Metal direct DFT lane exists and is explicitly labeled not a GPU FFT.
- CPU-vs-Metal demo reporting exists and uses
  `GraphExecutor::Execute()` result timing.
- DSP tests include spectrum executable tests, CPU-vs-Metal report schema
  checks, GPU buffer layout tests, and DFT truth-in-labeling guardrails.

Inferred:

- DSP spectrum functionality is fixture/demo-grade and well covered by
  guardrails around naming and performance claims.

### SDR/FHSS

Observed:

- FHSS protocol, frequency map, synthetic IQ generation, explicit message
  scheduling, downconversion, 64-output channelization, per-channel detection,
  pulse merge, CPSM metrics/Viterbi, word decode, preamble lock, message
  assembly, diagnostics, and executor tests are represented in code/tests.
- FHSS fixture constants in the active baseline and code include 500 Msps,
  5 Mbps, 100 samples/symbol, 3200 pulse samples, 3300 gap samples, 6500 period
  samples, 64 RF metadata frequencies, selectable transmit indices [1, 62],
  and reserved receiver indices 0 and 63.
- FHSS docs/tests preserve truth-in-labeling boundaries:
  - 1 GHz RF values are metadata;
  - fixture IQ uses baseband/IF offsets;
  - magnitude-only DFT/FFT is not canonical decoder input;
  - overlap-aware separation, Doppler/noise/CFO/multipath support, production
    channelizer claims, and FHSS GPU execution are not implemented.
- FHSS demo exists at `examples/DSP/src/fhss_demo.cpp` and accepts external
  message JSON.

Inferred:

- FHSS is the most complete SDR-like lane in the repo, but it remains a
  deterministic CPU fixture rather than a production RF receiver.

### SAR

Observed:

- SAR has synthetic stripmap configs, CRSD input configs, GOTCHA-to-CRSD
  conversion tooling, CRSD focused-image CPU and Metal nodes, sinks, reference
  comparison helpers, and local-only GOTCHA validation tests.
- SarPy and gotcha-back are represented as local-only comparison/reference
  boundaries in tests and docs.
- Metal focused-image transform is explicitly labeled experimental/incomplete
  in code/plugins/docs/tests.

Inferred:

- SAR has broad test coverage and multiple graph configurations, but there is
  still config sprawl and truth-in-labeling sensitivity around Metal and
  reference tools.

## 7. GPU/Accelerator Readiness Status

### Observed

- Backend-neutral accelerator contracts exist in `libgpu`.
- CUDA and SYCL capability implementations are present as stubs/simulated
  capability layers.
- Native Metal capability implementation exists and has runtime tests.
- Metal node inventory includes transfer, memory, sync/control, generic kernel,
  reduce, shard, peer copy, queue sync, and collective reduce nodes.
- `CollectiveReduceNodeMetal` is documented/tested as runtime unsupported.
- DSP Metal DFT and SAR Metal focused image both have truth-in-labeling
  guardrails.
- GPU runtime tests include Metal smoke, stress, graph pipeline, and failure
  injection tests.

### Inferred

- The GPU architecture is token/sidecar-first and has stronger truth-in-labeling
  than production-performance claims.
- FHSS is accelerator-ready at the token-contract level but not GPU-executed.

### Unknown

- I did not execute native Metal tests in this inspection.
- Actual performance characteristics are unknown from this report.

## 8. C++26 Usage Observations

### Observed

- The project requires C++26 at CMake configure time.
- `std::expected` is used in CSV parsing, JSON/runtime helpers, node creation,
  and config parsing paths.
- `std::span`, `std::optional`, `std::variant`, `std::filesystem`,
  `constexpr`, `consteval`, and compile-time type checks are used across the
  repo.
- `FixedFanInOutNode` uses template metaprogramming, `TypeList`, consteval port
  tables, and CRTP routed functions.
- FHSS uses generated type lists for 64 repeated channelizer outputs and
  compile-time static assertions for token aliases.
- Tests include compile-time/static-assert checks for token contracts and node
  port types.

### Inferred

- C++26 is not just a build setting; it is actively used for typed contracts,
  expected/error handling, port metadata, and compile-time assertions.

### Unknown

- I did not audit template diagnostic quality or compile-time cost.

## 9. Complexity Hotspots And Obsolete Abstractions

### Observed

- The repo contains both active consolidated docs and a large historical `doc/`
  tree plus `docs/archive/` and `plan/archive/`. This is visible documentation
  complexity, though active docs point to `README.md` and `plan/BASELINE.md`.
- `plan/agents/GRAPHX_AGENT_ROLES.md` exists as active agent guidance, while
  the archived old path for the same file appears deleted in the current git
  status.
- `GraphConfigParser.cpp`, `NodeFacade.cpp`, and plugin interop paths are large
  central runtime surfaces.
- FHSS has both `FHSSPulseMergeNode` and `FHSSPulseMergeInteriorNode`. The
  active baseline indicates conversion to shared repeated-port helpers; both
  headers/sources are still present.
- FHSS keeps a reference correlator-bank graph alongside the canonical
  channelized graph.
- SAR has many JSON configs representing synthetic, CRSD, GOTCHA, CPU, Metal,
  local-validation, and benchmark lanes.
- `host_ptr` and `ready_event` remain widely present in accelerator transport
  code and tests. SAR comments/tests say they are opaque transport metadata,
  not identity fields.
- `EdgeRegistration.cpp` contains an observed placeholder comment saying
  `RegisterAllEdges()` is to be populated.
- `PluginInspector.cpp`, `StaticNodeAdapter.cpp`, `BuiltinCommands.cpp`, and
  `NodePluginTemplate.hpp` contain placeholder/future-use comments.
- A `.swp` file exists under `libgpu/include/metal-cpp/Metal/`, indicating an
  editor artifact inside a third-party header tree.

### Inferred

- The largest current architectural complexity areas are:
  - dynamic plugin/facade/provider code;
  - repeated-port GraphX abstractions;
  - FHSS canonical versus reference graph coexistence;
  - SAR config surface area;
  - GPU token transport fields that are legitimate but easy to misuse.
- Some old documentation was intentionally archived, but the `doc/` tree
  remains outside the newer `docs/archive/2026-06-baseline/` scheme.

### Unknown

- I did not classify each placeholder as harmful, intentional, or obsolete.

## 10. Test And Documentation Coverage Gaps

### Observed

- Test targets exist for:
  - `test_libgraph_unit`;
  - `test_libgraph_integration`;
  - `test_libgpu_stub_unit`;
  - `test_libgpu_metal_runtime`;
  - `test_libgpu_backend_unit`;
  - `test_libgpu_integration`;
  - `test_libgpu_perf`;
  - `test_dsp_example_unit`;
  - `test_sar_example_unit`;
  - SAR local-only lanes.
- Built binaries observed in `build-ninja/ninja-debug-metal-native` include
  `test_libgraph_unit`, `test_libgpu_stub_unit`,
  `test_libgpu_metal_runtime`, DSP demos, and SAR converter.
- FHSS tests cover protocol, synthetic IQ, graph packets, graph nodes, pulse
  merge, CPSM decoder, word decoder, message assembly, graph executor, and
  guardrails.
- DSP tests cover spectrum graph runtime, GPU spectrum parity, GPU buffer
  layout, truth-in-labeling, and example demo behavior.
- SAR tests cover CRSD input/output, focused image CPU/Metal, local reference
  tools, image comparison, GOTCHA conversion, CI lanes, token contracts,
  transport opacity, and Metal truth-in-labeling.
- Documentation coverage has been consolidated into `README.md` and
  `plan/BASELINE.md`, with tests checking exact truth-in-labeling phrases.

### Inferred

- The repo has broad unit and guardrail coverage, especially around
  truth-in-labeling and deterministic fixture behavior.
- Some tests are string-guardrail heavy. These are useful for documentation
  drift, but they are brittle by construction.

### Unknown

- Full test-suite status is unknown because no tests were run in this
  inspection.
- Coverage quality for all plugin metadata and all runtime error paths was not
  exhaustively inspected.

## 11. Current Git/Workspace State

### Observed

- `git status --short` showed:
  - deleted archived `plan/archive/2026-06-baseline/agents/GRAPHX_AGENT_ROLES.md`;
  - untracked active `plan/agents/`;
  - existing prior documentation/archive changes from the baseline
    consolidation.
- This report creates `plan/reviews/GRAPHX_INSPECTOR_REPORT.md`.

### Inferred

- The active agent roles were moved from archive into `plan/agents/`, matching
  the user's requested path, but that move is not yet represented as a staged
  git rename.

## 12. Summary Findings

| Area | Classification | Finding |
|---|---|---|
| Active baseline | Observed | `plan/BASELINE.md` and `README.md` are active sources of truth. |
| Agent roles | Observed | `plan/agents/GRAPHX_AGENT_ROLES.md` exists and is active. |
| Core runtime | Observed | Typed GraphX nodes, ports, executor, JSON loader, plugin/provider system, and repeated-port helpers exist. |
| Token model | Observed | Accelerator-ready edges use `graph::gpu::accel::ControlToken<...>` in DSP/FHSS/SAR contracts. |
| FHSS graph | Observed | Canonical channelized FHSS fixture exists with 64-port channelizer and per-channel detectors. |
| FHSS limitations | Observed | Production RF, Doppler/noise, overlap-aware separation, and FHSS GPU execution remain unsupported. |
| DSP spectrum | Observed | CPU direct DFT and Metal direct DFT lanes exist with truth-in-labeling guardrails. |
| SAR | Observed | CRSD/GOTCHA/focused-image lanes exist with CPU, Metal, and local-only reference tooling. |
| GPU | Observed | Native Metal and backend-neutral accelerator contracts exist; unsupported/experimental paths are labeled. |
| C++26 | Observed | Project requires C++26 and uses modern type/error facilities. |
| Complexity | Inferred | Main complexity hotspots are plugin/facade runtime, repeated ports, SAR config sprawl, FHSS dual topology, and GPU transport metadata discipline. |
| Test status | Unknown | Full test-suite status was not checked during this inspection. |

End of current-state inspection.
