# GraphX Inspector Report

Date: 2026-06-23

Role source: `plan/agents/GRAPHX_AGENT_ROLES.md`

Scope: Current repository inspection only. No redesign or implementation work
was performed.

## 1. Current Architecture Summary

### Observed

- GraphX is a C++26 workspace with four main implementation areas:
  - `libgraph/`: typed graph runtime, lifecycle, ports, queues, executor,
    policies, JSON graph construction, node facade/ABI, plugins, and resolver;
  - `libgpu/`: backend-neutral accelerator contracts plus CUDA/SYCL/Metal node
    and capability implementations;
  - `libdsp/`: DSP spectrum and deterministic FHSS nodes/contracts;
  - `examples/SAR/`: SAR nodes, CRSD/GOTCHA I/O, focused-image lanes, tools,
    plugins, fixtures, and tests.
- `CMakeLists.txt` requires C++26, Ninja by default, position-independent code,
  and optional CUDA, SYCL, Metal, and native Metal support.
- `GraphExecutorBuilder` is the normal user-facing graph construction path. It
  validates JSON/plugin inputs, bootstraps providers, builds through
  `GraphBuilder`, installs execution policies, and returns `GraphExecutor`.
- The active user guide is `README.md`; the active architecture baseline is
  `plan/BASELINE.md`.
- The worktree was clean before this report was written.

### Inferred

- The repository is a multi-domain graph-runtime workspace rather than a
  SAR-only project.
- Dynamic JSON/plugin execution is the principal integration architecture;
  direct node tests remain important for algorithm and contract coverage.

### Unknown

- The complete repository test suite was not run during this inspection.

## 2. Current Type And Packet Model

### Observed

- `graph::gpu::accel::ControlToken<SidecarT>` is the common accelerator-ready
  envelope. It contains:
  - domain sidecar;
  - token id;
  - buffer lease and host/device views;
  - transfer and kernel tickets;
  - explicit presence flags.
- `ControlTokenType` and `ControlTokenFor` concepts exist in `libgpu`, with
  node-port concepts in `libgraph/include/graph/AccelTokenContracts.hpp`.
- FHSS edge contracts are centralized in `FHSSGraphXPackets.hpp` and token
  aliases in `FHSSGraphXNodeUtils.hpp`.
- FHSS contracts preserve:
  - complex IQ evidence;
  - global/channel sample timing and sample-rate mapping;
  - RF metadata and IQ offset frequency;
  - channel/decimation/group-delay metadata;
  - pulse confidence, CFO placeholders, branch metrics, symbol decisions,
    decoded words, assembled messages, and diagnostics.
- FHSS complex CPU evidence uses shared immutable
  `std::vector<std::complex<double>>` storage plus offset/count ranges.
- SAR uses `ControlToken<SarSidecar>`. `SarSidecar` carries SAR identity,
  routing, tile, aperture, backend, transfer, kernel, and timing fields.
- SAR tests explicitly treat `host_ptr` and `ready_event` as transport-only,
  not domain identity.
- DSP spectrum edges use token-wrapped message or magnitude sidecars.

### Inferred

- The token model is structurally shared across GPU, DSP, FHSS, and SAR, while
  each domain retains its own semantic sidecar.
- FHSS packet contracts are accelerator-ready by type but currently carry host
  complex samples for the CPU fixture lane.

### Unknown

- Full sidecar preservation across every node and every error path was not
  exhaustively audited.

## 3. Current Node And Port Model

### Observed

- GraphX exposes named source, interior, sink, split, merge, and fixed
  fan-in/fan-out node bases.
- `RoutedInputFn`, `RoutedOutputFn`, and `RoutedTransferFn` route typed virtual
  port calls into CRTP template hooks.
- `NamedFixedFanInOutNode` provides:
  - compile-time input/output type lists;
  - generated port tables;
  - routed consume/produce functions;
  - per-output queues;
  - lifecycle and metrics forwarding;
  - direct typed transfer routing through `TransferInputToOutput`.
- A transfer returning `std::nullopt` is valid and produces no queued output.
- `ChannelizerNode` now derives from `NamedFixedFanInOutNode` with one input and
  exactly 64 token-wrapped outputs.
- `FHSSPulseMergeNode` now derives from the same base with 65 inputs
  (one detected-pulse input plus 64 per-channel inputs) and two outputs.
- The duplicate `FHSSPulseMergeInteriorNode` and correlator-bank detector
  surface are absent from current public code/plugins/config.
- FHSS public nodes are split into individual headers/sources and registered
  as plugins.
- SAR public `...Node` classes are real GraphX nodes, with domain algorithm
  records and kernels using non-node names.

### Inferred

- Repeated-port boilerplate is substantially lower for the channelizer and
  pulse merge than in the earlier custom implementations.
- `FixedFanInOutNodeBase::Transfer` is a direct typed helper. Runtime input and
  output scheduling for fixed fan-in/out nodes still occurs through routed
  consume/produce threads and node-owned output queues.

### Unknown

- Fairness and latency under simultaneous activity on all 64 FHSS detector
  inputs were not measured.

## 4. Current Token And Data Flow

### Observed

- CPU DSP spectrum:
  `SineSignalNode -> CpuSpectrumDftNode -> SpectrumSinkNode`.
- Metal DSP direct DFT:
  `SineSignalNode -> DspIqH2DNode -> MetalSpectrumDftNode ->
  DspMagnitudeD2HNode -> SpectrumSinkNode`.
- The Metal spectrum implementation is explicitly a direct DFT, not an FFT.
- Canonical FHSS:
  `FHSSSyntheticIqSourceNode -> FHSSDownconverterNode -> ChannelizerNode ->
  PerChannelPulseDetectorNode[64] -> FHSSPulseMergeNode ->
  FHSSPulseCandidateNode -> CPSMBranchMetricNode ->
  CPSMViterbiDecoderNode -> FHSSPulseWordDecoderNode ->
  FHSSPreambleDetectorNode -> FHSSMessageAssemblerNode ->
  FHSSMessageSinkNode`.
- The canonical FHSS JSON contains 64 distinct channelizer edges and 64
  detector instances.
- FHSS source output is one token containing the configured deterministic
  message schedule, IQ, and validation-only truth.
- The channelizer mixes each output to its configured offset and optionally
  decimates. It does not implement a channel filter.
- Per-channel detection scans known pulse-period slots and emits complex pulse
  evidence. It does not search across frequencies.
- CPSM decoding uses a four-state, `h = 1/2`, rectangular full-response CPU
  Viterbi/MLSE fixture model.
- SAR CPU focused-image flow uses ordered CRSD input, aperture assembly, CPU
  focused-image transform, and optional sink/artifact paths.
- The named canonical SAR GPU config uses an experimental/incomplete Metal
  focused-image transform.

### Inferred

- The FHSS lane is deterministic known-slot acquisition, not a streaming
  receiver acquisition implementation.
- The current channelizer provides frequency-parallel fixture evidence but not
  production channel separation because no analysis filter bank is present.

### Unknown

- Runtime scaling and memory pressure for long FHSS schedules or sustained
  streaming IQ were not measured.

## 5. Current Plugin, Provider, And Resolver Flow

### Observed

- `NodeProviderBootstrap` discovers and loads plugin directories.
- `RegisteredNodeProvider` creates concrete plugin nodes.
- `ResolvingNodeProvider` maps intent types to available backend-specific
  concrete types using:
  - requested backend;
  - fallback policy;
  - default or JSON-provided resolution contracts;
  - availability checks.
- Resolver diagnostics record intent, concrete type, backend, fallback reason,
  and declared token type names.
- `GraphConfigParser` recognizes `resolver_diagnostics` and the
  `accel-token` edge contract.
- DSP, FHSS, GPU, SAR, and test nodes have plugin targets.
- Examples and integration tests use `GraphExecutorBuilder` with JSON and
  plugin directories rather than local executor substitutes.

### Inferred

- Backend selection is intentionally policy/config driven.
- Domain-specific resolution can remain outside `libgraph` through JSON
  resolver mappings, as demonstrated by SAR.

### Unknown

- ABI compatibility across independently built plugin toolchains was not
  tested in this inspection.

## 6. SDR, DSP, FHSS, And SAR Capability Status

### DSP

#### Observed

- CPU direct DFT, Metal direct DFT, H2D/D2H, spectrum sink, deterministic
  diagnostics, and CPU-vs-Metal informational reporting exist.
- Magnitude spectrum is a DSP output contract, not the FHSS decoder input.

#### Unknown

- No optimized FFT implementation was observed in the canonical spectrum lane.

### SDR/FHSS

#### Observed

- Fixture constants remain 500 Msps, 5 Mbps, 100 samples/symbol, 32 symbols
  per pulse, 3200 pulse samples, 3300 gap samples, and 6500 samples/period.
- The RF metadata table has 64 entries; transmit indices 0 and 63 are reserved.
- Source messages explicitly specify transmit time, pulse frequency index,
  value, and preamble/body role.
- Downconversion supports validated passthrough or declared frequency
  translation.
- One logical channel/output port exists per frequency.
- Hop-only preamble, four-frequency active set, CPSM decode, word decode, and
  message assembly are implemented for deterministic fixtures.
- Overlap, Doppler, noise, multipath, production channelization, real RF
  capture, and FHSS GPU execution remain unsupported.
- The current 500 Msps fixture does not represent all 64 RF centers as one
  alias-free direct-RF capture.

#### Inferred

- CFO fields and impairment diagnostics are mostly contract/status surfaces,
  not impairment-correcting receiver behavior.

### SAR

#### Observed

- Synthetic stripmap, ordered CRSD-set ingest, GOTCHA conversion/replay tools,
  CPU focused-image formation, experimental Metal focused-image execution,
  diagnostics, artifact comparison, and local-only baseline tools exist.
- Exactly 13 SAR JSON configs are present after config consolidation.
- SarPy is selected for the opt-in local baseline runner; default CI does not
  require it.
- The GraphX-vs-baseline harness has a deterministic CI-safe tiny-fixture mode.
- Native Metal focused-image status is explicitly
  `experimental_incomplete_cpu_seed_plus_placeholder_kernel`.
- Metal collective reduce is explicitly runtime unsupported.

#### Unknown

- Production-quality SAR motion compensation, autofocus, radiometric
  calibration, and real-data acceptance were not demonstrated.

## 7. GPU And Accelerator Readiness Status

### Observed

- Backend-neutral validation exists for layouts, views, leases, transfer
  tickets, kernel tickets, collectives, and shards.
- CUDA and SYCL graph-node surfaces exist; Metal has both fallback capabilities
  and a native implementation.
- GPU capability bootstrap and GraphX policy binding are tested.
- Representative DSP, FHSS, SAR, and GPU port contracts have compile-time
  `ControlToken` tests.
- Stub/backend-neutral GPU tests passed: 35 of 35.
- Native Metal runtime tests ran in this environment with 2 passing
  validation tests and 12 tests skipped because no active Metal GPU device was
  available.

### Inferred

- Accelerator contract readiness is stronger than native-backend execution
  coverage in the current environment.
- Token-ready FHSS edges do not imply a GPU FHSS implementation.

### Unknown

- Native Metal transfer/kernel correctness and performance could not be
  exercised on this host.
- CUDA and real SYCL device execution were not exercised.

## 8. C++26 Usage Observations

### Observed

- C++26 is enforced globally.
- The code uses `std::expected`, concepts, `std::remove_cvref_t`, `consteval`,
  compile-time type lists, `std::span`, ranges, and extensive type traits.
- Compile-time tests enforce token and port contracts.
- Strong enums and structured status/error types are common in newer code.
- Some public headers retain duplicated or generic generated Doxygen blocks,
  including descriptions that incorrectly call registry/provider types graph
  nodes.
- Several large legacy headers remain template-heavy and monolithic.

### Inferred

- Modern language facilities are used most effectively in newer token,
  repeated-port, config, and validation code.
- Compile-time abstraction depth contributes to longer diagnostics and broad
  rebuilds when central headers change.

### Unknown

- No compile-time or template-instantiation performance profile was collected.

## 9. Complexity Hotspots And Obsolete Abstractions

### Architecture Findings

#### Observed

- `Nodes.hpp` is 2364 lines, `GraphManager.hpp` 1803,
  `NodePluginTemplate.hpp` 1764, `ThreadPool.hpp` 1665, and
  `NodeFacade.hpp` 1154. These remain high-coupling core surfaces.
- `NativeMetalCapabilities.cpp` is 2199 lines.
- `sar_benchmark.cpp` is 1510 lines.
- The SAR unit target is one large executable containing most SAR sources and
  tests. A change to shared headers triggers a broad rebuild.
- FHSS tests live primarily in `libgraph/test`, not a `libdsp/test` directory,
  which blurs test ownership.
- The canonical FHSS JSON repeats all 64 channel ids, frequency indices,
  detectors, and edges explicitly.
- Build output reports duplicate static libraries while linking many SAR
  plugins and executables.
- The current build reports unused-variable/parameter warnings in focused-image
  source and tests.

#### Inferred

- The largest maintenance risk is concentrated in core facade/plugin/runtime
  headers and the SAR monolithic test/build target.
- Explicit 64-lane JSON is architecturally clear but mechanically large.

### Implementation Defects

#### Observed

- `examples/SAR/tools/sar_local_runner.py` and `examples/SAR/README.md` still
  reference deleted `examples/SAR/config/sar_gotcha_external_manual.json`.
- `ImageComparatorContractTest.RealScenarioArtifactsProduceDeterministicFailReportWithReasons`
  fails with `FileNotFoundError` for that deleted config.
- `examples/SAR/README.md` also lists deleted Metal stripmap configs as active,
  conflicting with the consolidated top-level documentation.
- `examples/SAR/test/CMakeLists.txt` defines
  `SAR_PROJECTILE_JSON_CONFIG_PATH` twice.
- `FHSSGraphXPackets.hpp` still describes its contracts as PR7A contracts for
  “future runtime nodes,” although those runtime nodes now exist.

#### Inferred

- Documentation consolidation is incomplete because an older domain README
  remains operationally discoverable and stale.
- The broken SAR local-runner path can cause broader SAR test lanes to fail
  even though the newer local baseline runner and comparison harness pass.

### Unknown

- Other stale paths may remain in less frequently exercised local-only tools.

## 10. Test And Documentation Coverage Gaps

### Observed

- Current focused verification results:
  - GraphX/FHSS/repeated-port/executor timing: 38 of 38 passed.
  - GPU stub/backend-neutral tests: 35 of 35 passed.
  - SAR token, transport, baseline, local baseline, and comparison selection:
    21 passed and 1 failed due to the deleted manual config.
  - Native Metal runtime: 2 passed, 12 skipped because no device was available.
- FHSS tests cover graph shape, plugin loading, truth matching, repeated-port
  contracts, complex evidence, pulse merge, and decoder behavior.
- SAR tests cover deterministic fixtures, CRSD I/O, token identity, config
  guardrails, local-only gates, and comparison contracts.
- Default local-only real GOTCHA CTest is disabled and explicitly labeled.

### Inferred

- Current CI confidence is strong for deterministic fixture behavior and type
  contracts, but weaker for native accelerator execution, sustained streaming,
  RF impairments, overlap, and real-data SAR acceptance.
- String/path guardrail tests detect many architecture regressions but can lag
  behind code changes or preserve stale wording.

### Unknown

- Whole-repository CTest status is unknown because the complete suite was not
  run.
- Native GPU performance, multi-GPU behavior, and long-duration scheduler
  behavior are unknown.
- FHSS behavior under asynchronous multi-message arrival, real acquisition,
  interference, and channel-filter leakage is unknown.
