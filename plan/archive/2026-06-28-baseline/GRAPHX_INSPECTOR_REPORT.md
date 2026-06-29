# GraphX Inspector Report

Date: 2026-06-28

Role source: `plan/agents/GRAPHX_AGENT_ROLES.md`

Scope: Current repository inspection only. This report describes the checked-out
tree, including pre-existing uncommitted changes. No redesign or implementation
work was performed.

## Inspection Context

### Observed

- Branch: `main`; inspected commit: `c998cb1b` (`FHSS Dashboard Step 8+fixes`).
- The worktree already contained modifications to:
  `examples/DSP/src/fhss_demo.cpp`,
  `examples/DSP/test/test_dsp_fhss_dashboard_step3.cpp`,
  `libgraph/include/graph/dashboard/GraphRuntimeSession.hpp`, and
  `libgraph/src/dashboard/GraphRuntimeSession.cpp`.
- The active planning baseline is `plan/BASELINE.md`; the active user guide is
  `README.md`. Archived material was not treated as active scope.

### Unknown

- Whether the four uncommitted files are intended for the next commit.

## 1. Current Architecture Summary

### Observed

- GraphX is a C++26 workspace composed of:
  - `libgraph`: typed graph runtime, lifecycle, queues, executor, policies,
    dynamic edges, JSON construction, plugin ABI/facades, providers, resolver,
    metrics, and an optional embedded dashboard;
  - `libgpu`: backend-neutral accelerator contracts and CUDA, SYCL, and Metal
    capability/node surfaces;
  - `libdsp`: spectrum and deterministic FHSS contracts, algorithms, and real
    GraphX nodes;
  - `examples/SAR`: SAR nodes, CRSD/GOTCHA I/O, focused-image paths, plugins,
    fixtures, local tools, and tests;
  - `libsensor`: sensor support.
- `GraphExecutorBuilder` is the normal integration entry point. It consumes a
  JSON graph path and plugin directories, bootstraps providers, builds the graph,
  installs policies, and returns a `GraphExecutor`.
- JSON/plugin execution and direct typed-node testing coexist. The former is the
  user-facing integration path; the latter supplies algorithm and contract
  coverage.
- The optional FHSS dashboard adds static assets, HTTP APIs, configuration
  editing, lifecycle commands, metrics/diagnostic snapshots, scenario stepping,
  event replay, visualization payloads, and artifact export. It is disabled by
  default with `GRAPHX_BUILD_WEB_DASHBOARD=OFF`.
- Current uncommitted dashboard work injects start/stop handlers into
  `GraphRuntimeSession` and connects dashboard start to a real
  `GraphExecutorBuilder` execution thread.

### Inferred

- The repository is a general graph-runtime workspace with DSP/FHSS and SAR as
  substantial proving domains, not a domain-specific runtime.
- The dashboard is an application-facing layer over core graph metrics and
  lifecycle services, although its implementation currently resides partly in
  `libgraph`.

### Unknown

- Whole-repository behavior under installed-package consumption was not tested.

## 2. Current Type and Packet Model

### Observed

- `graph::gpu::accel::ControlToken<SidecarT>` is the common accelerator-ready
  envelope. It carries the domain sidecar, token id, buffer lease, host/device
  views, transfer/kernel tickets, and explicit presence flags.
- `ControlTokenType`, `ControlTokenFor`, and graph port concepts provide
  compile-time token-contract checks.
- DSP uses explicit IQ and magnitude packets. Accelerator DSP edges keep domain
  data in the sidecar and transport state in token fields.
- FHSS packet definitions centralize complex IQ evidence, sample-rate and
  global/channel sample-time mapping, RF metadata versus IQ offset frequency,
  channel/decimation/group-delay metadata, pulse evidence, branch metrics,
  symbol decisions, decoded words, messages, and diagnostics.
- FHSS host evidence uses shared immutable complex-sample storage plus ranges;
  magnitude-only output is not the canonical decoder evidence.
- SAR uses `ControlToken<SarSidecar>`. SAR identity, routing, aperture, tile,
  backend, transfer, kernel, and timing information live in the sidecar;
  `host_ptr` and `ready_event` are tested as transport-only fields.

### Inferred

- The common envelope separates transport mechanics from domain identity while
  allowing CPU-only domain lanes to be type-compatible with future accelerator
  stages.

### Unknown

- Sidecar preservation was not exhaustively traced through every exceptional or
  cancellation path.

## 3. Current Node and Port Model

### Observed

- The runtime exposes typed source, interior, sink, split, merge, fixed fan-in,
  and fixed fan-out bases, plus named port specifications and runtime port
  facades.
- `RoutedInputFn`, `RoutedOutputFn`, and `RoutedTransferFn` route virtual port
  calls to typed CRTP hooks.
- `NamedFixedFanInOutNode` generates compile-time port tables, routed functions,
  per-output queues, lifecycle/metrics forwarding, and direct typed transfer
  dispatch. A transfer may return `std::nullopt` to emit no output.
- `ChannelizerNode` uses this base with one input and exactly 64 separate output
  ports. `FHSSPulseMergeNode` uses it with 65 inputs and two outputs.
- The canonical FHSS code has no aggregate 64-channel output packet and no
  active correlator-bank node/config/plugin.
- Public SAR `...Node` types are GraphX nodes; SAR algorithm/reference helpers
  are separately named records or functions.

### Inferred

- Repeated-port helpers have removed much of the FHSS per-port implementation
  boilerplate, but the explicit 64-lane topology remains mechanically large.

### Unknown

- Fairness, latency, and memory behavior under sustained simultaneous traffic on
  every FHSS port were not measured.

## 4. Current Token and Data Flow

### Observed

- CPU spectrum flow is `SineSignalNode -> CpuSpectrumDftNode ->
  SpectrumSinkNode`.
- Metal spectrum flow is `SineSignalNode -> DspIqH2DNode ->
  MetalSpectrumDftNode -> DspMagnitudeD2HNode -> SpectrumSinkNode`. Both CPU and
  Metal spectrum algorithms are labeled direct DFT, not FFT.
- Canonical FHSS flow is:
  `FHSSSyntheticIqSourceNode -> FHSSDownconverterNode -> ChannelizerNode ->
  PerChannelPulseDetectorNode[64] -> FHSSPulseMergeNode ->
  FHSSPulseCandidateNode -> CPSMBranchMetricNode -> CPSMViterbiDecoderNode ->
  FHSSPulseWordDecoderNode -> FHSSPreambleDetectorNode ->
  FHSSMessageAssemblerNode -> FHSSMessageSinkNode`.
- The FHSS JSON explicitly contains 64 channelizer edges and 64 detector
  instances. Each detector consumes one channel; merge normalizes evidence into
  shared global sample time.
- The deterministic channelizer mixes each configured offset and may decimate,
  but does not implement a channel filter bank.
- CPSM decoding is a CPU Viterbi/MLSE fixture path using complex evidence and
  branch metrics.
- SAR CPU focused-image flow uses ordered CRSD-set input, aperture assembly,
  focused-image transform, and artifact/diagnostic sinks. The sole canonical
  Metal focused-image config remains experimental/incomplete.

### Inferred

- FHSS acquisition is known-schedule deterministic fixture processing rather
  than a continuously acquiring receiver.
- Missing channel filtering is a semantic limit on separation claims, not merely
  a performance limitation.

### Unknown

- Sustained-stream behavior and end-to-end backpressure across long domain runs
  were not characterized.

## 5. Current Plugin, Provider, and Resolver Flow

### Observed

- `NodeProviderBootstrap` discovers plugin directories and loads libraries.
- `RegisteredNodeProvider` constructs registered concrete nodes.
- `ResolvingNodeProvider` maps intent node types to available concrete types
  using requested backend, fallback policy, availability, and default or
  JSON-provided resolution contracts.
- Resolver diagnostics include intent, concrete type, selected backend,
  fallback reason, and declared token type names.
- `GraphConfigParser` validates resolver declarations and `accel-token` edge
  contracts; `JsonDynamicGraphLoader` creates nodes and dynamic edges through
  provider/facade interfaces.
- DSP, FHSS, GPU, SAR, and test node plugins are built. Integration examples and
  tests use `GraphExecutorBuilder` rather than local executor substitutes.

### Inferred

- Backend substitution is deliberately policy/config driven, while domain
  resolver mappings can remain outside `libgraph`.

### Unknown

- ABI compatibility across independently compiled plugin toolchains and release
  versions was not exercised.

## 6. SDR, DSP, FHSS, and SAR Capability Status

### DSP — Observed

- Deterministic sine generation, CPU direct DFT, Metal direct DFT, H2D/D2H
  nodes, magnitude sink, summaries, and informational CPU-versus-Metal timing
  reports exist.
- No canonical optimized FFT implementation was observed.

### SDR/FHSS — Observed

- The fixture is labeled 500 Msps, 5 Mbps, 100 samples/symbol, 32
  symbols/pulse, 3,200 pulse samples, 3,300 gap samples, and 6,500
  samples/period.
- It has 64 RF metadata frequencies, reserved indices 0 and 63, explicit
  transmit schedules, a 16-pulse preamble, four active transmit frequencies,
  and at most 256 pulses/message.
- Downconversion supports validated passthrough or declared translation; one
  logical output port exists per configured frequency.
- Deterministic pulse detection, merging, CPSM metrics, Viterbi/MLSE, word
  decode, preamble detection, message assembly, SigMF debug capture, and
  diagnostics exist.
- The 1 GHz values are RF metadata. The fixture does not model the complete
  table as one simultaneous alias-free 500 Msps direct-RF capture.
- Doppler, noise, multipath, overlap-aware separation, real RF capture,
  production channelization, occupied-bandwidth/filter closure, and FHSS GPU
  execution are not implemented. Overlap is unsupported.

### SDR/FHSS — Inferred

- CFO and impairment fields are primarily status/contract surfaces rather than
  demonstrated impairment-correction behavior.

### SAR — Observed

- Synthetic stripmap, binary and sidecar-assisted CRSD reading, ordered-set
  ingest, GOTCHA conversion/replay, aperture assembly, CPU focused-image
  formation, an experimental Metal focused-image lane, diagnostics, artifact
  comparison, CI-safe tiny fixtures, and local-only reference tools exist.
- Exactly 13 active SAR JSON configs are present.
- SarPy is optional and local-only; GOTCHA real-data and full-aperture tests are
  explicitly environment gated.
- The GraphX-versus-baseline and substitution tools distinguish CI-safe fixture
  comparison from opt-in local experiments.

### SAR — Unknown

- Production motion compensation, autofocus, calibration, and broad real-data
  focused-image acceptance were not demonstrated.

## 7. GPU and Accelerator Readiness Status

### Observed

- Backend-neutral types validate layouts, views, leases, transfer/kernel
  tickets, collectives, and shards.
- CUDA and SYCL node/capability surfaces exist. Metal has fallback and native
  capability implementations plus transfer, memory, sync, kernel, shard,
  reduce, peer-copy, and lifecycle nodes.
- Compile-time tests cover representative GPU, DSP, FHSS, and SAR token/port
  contracts.
- Native Metal DSP/SAR checks skipped in this environment because no active
  Metal device was exposed. SAR still passed all 281 runnable tests.
- FHSS edges are accelerator-ready by contract, but the active FHSS algorithms
  are CPU implementations.

### Inferred

- Accelerator contract readiness is materially broader than native backend
  execution coverage.

### Unknown

- CUDA execution, real SYCL device execution, native Metal correctness on this
  host, multi-GPU behavior, and backend performance are unknown.

## 8. C++26 Usage Observations

### Observed

- C++26 is enforced globally and repeated in major library CMake files.
- The tree uses concepts, `std::expected`, `std::span`, ranges,
  `std::remove_cvref_t`, `consteval`, compile-time type lists, type traits,
  strong enums, structured errors, and compile-time contract assertions.
- Newer token, repeated-port, resolver, and validation code uses language-level
  constraints to make invalid type combinations fail at compile time.
- The optional module pilot is disabled by default.
- Some public headers retain generic or duplicated generated Doxygen wording,
  and central template headers remain large.

### Inferred

- Strong compile-time modeling improves contract precision but increases
  template diagnostic depth and rebuild breadth when central headers change.

### Unknown

- Template-instantiation time and binary-size costs were not profiled.

## 9. Complexity Hotspots and Obsolete Abstractions

### Architecture Findings — Observed

- High-coupling files remain large: `Nodes.hpp` (2,364 lines),
  `GraphManager.hpp` (1,803), `NodePluginTemplate.hpp` (1,764),
  `ThreadPool.hpp` (1,665), and `NodeFacade.hpp` (1,154).
- `NativeMetalCapabilities.cpp` is 2,199 lines; `sar_benchmark.cpp` is 1,510.
- The SAR unit suite is one large executable with 291 tests and many domain
  sources; shared-header changes trigger broad rebuilds.
- FHSS/core tests are concentrated in the 1,134-test `libgraph` executable,
  which blurs `libgraph` versus `libdsp` ownership and makes focused runtime
  verification expensive.
- The canonical FHSS JSON explicitly repeats 64 ports, detectors, and edges.
- Dashboard behavior is split among generic-looking `libgraph/dashboard`
  services and FHSS-specific controllers/schemas.
- Linking reports duplicate `libgraph.a`/`libgpu.a` inputs.
- `StaticNodeAdapter`, the facade/ABI layer, dynamic edges, typed nodes, and
  fixed-fan routing all coexist as current abstractions. No source evidence
  established that `StaticNodeAdapter` is dead.
- The old FHSS correlator-bank surface and aggregate channel output are absent;
  they are obsolete only as historical/archived concepts, not active
  compatibility paths.

### Architecture Findings — Inferred

- The main maintenance concentration is the core node/facade/plugin/runtime
  headers and the monolithic core/SAR test targets.
- The explicit 64-lane graph favors inspectable topology over compact
  configuration.

### Implementation Defects — Observed

- `FHSSGraphXExecutorTest.ChannelizedJsonTopologyRunsThroughGraphExecutorBuilderAndMatchesTruth`
  reproducibly fails: diagnostics report 36 pulses and 36 decoded pulses while
  fixture truth contains 72. This is a current canonical GraphExecutor/FHSS
  correctness failure, not a labeling or archived-document issue.
- The earlier deleted-SAR-config path failure is no longer present:
  `ImageComparatorContractTest.RealScenarioArtifactsProduceDeterministicFailReportWithReasons`
  now passes, and active references describe the removed configs as deleted.

### Unknown

- The exact cause of the FHSS half-count failure was not diagnosed, per the
  inspection-only scope.
- Other low-frequency local-only stale paths may exist.

## 10. Test and Documentation Coverage Gaps

### Observed

- Focused build of `test_dsp_example_unit` succeeded.
- `test_libgraph_unit`: 1,126 passed, 7 skipped, 1 failed, 1 disabled. The sole
  failure is the reproducible FHSS 36-versus-72 pulse mismatch.
- `test_sar_example_unit`: 281 passed and 10 explicitly gated/skipped.
- `test_dsp_example_unit`: 25 non-socket tests passed; 34 dashboard HTTP tests
  failed because the managed sandbox rejected local socket binding. Those
  results do not establish a repository defect.
- Native Metal tests encountered by the suites skipped clearly when no device
  was available. Real GOTCHA, local CRSD, and optional SarPy CRSD paths also
  skipped behind documented environment gates.
- Core and domain tests cover typed ports, token identity, plugin loading,
  resolver diagnostics, JSON graph execution, deterministic DSP/FHSS fixtures,
  SAR CRSD I/O, artifacts, local-only gates, and truth-in-labeling guardrails.
- `README.md` documents the active baseline, build presets, runtime examples,
  truth-in-labeling, local-only gates, and dashboard invocation. Current active
  searches found no stale operational references to the deleted SAR configs.

### Inferred

- Deterministic fixture and compile-time contract coverage is broad, but native
  accelerator execution, real RF impairments, overlap, sustained streaming,
  and real-data SAR acceptance remain weakly exercised or intentionally absent.
- Many documentation/architecture tests are string guardrails; they protect
  labels and file sets but cannot establish algorithmic correctness.

### Unknown

- Dashboard HTTP correctness could not be verified in the managed sandbox.
- Full CTest status, native GPU performance, long-duration scheduler behavior,
  FHSS multi-message concurrency, RF acquisition behavior, and production SAR
  accuracy remain unknown.

Stop: current-state analysis complete. No redesign or implementation is
included.
