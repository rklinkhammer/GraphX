# GraphX Inspector Report

Inspector role source: `plan/agents/GRAPHX_AGENT_ROLES.md`

Scope: current repository inspection only. No redesign. No implementation.

Repository state at inspection time:

- Observed: The working tree already had `plan/agents/GRAPHX_SAR_AGENT_ROLES.md` deleted, `plan/prompt examples/sequence.md` modified, `plan/reviews/SAR_INSPECTOR_REPORT.md` modified, and `plan/agents/GRAPHX_AGENT_ROLES.md` untracked before this report write.
- Observed: This inspector pass adds `plan/reviews/GRAPHX_INSPECTOR_REPORT.md`.
- Inferred: Findings describe checked-out repository files only and do not rely on external datasets, local GPU hardware, or network state.
- Unknown: No test suite was executed during this inspector-only pass.

## 1. Current Type Model

- Observed: Generic accelerator transport types live in `libgpu/include/gpu/accel/types/AccelTypes.hpp`, including `DeviceBufferView`, `HostPinnedBufferView`, `BufferLease`, `TransferTicket`, `KernelTicket`, and `ControlToken<SidecarT>`.
- Observed: `ControlToken<SidecarT>` carries a domain sidecar plus opaque accelerator transport metadata.
- Observed: SAR defines `SarAccelControlToken = graph::gpu::accel::ControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: SAR sidecar fields carry SAR identity, frame markers, backend state, queue IDs, byte counters, merge counters, and stage timing.
- Observed: SAR comments and tests state that `host_ptr` and `ready_event` are transport metadata, not SAR identity.
- Observed: DSP spectrum nodes use tokenized sidecars with mixed payload shapes: `ControlToken<graph::message::Message>` for IQ input and `ControlToken<MagnitudePacket<...>>` for spectrum output.
- Observed: FHSS defines data-only protocol/config/truth metadata in `libdsp/include/dsp/fhss/FHSSProtocol.hpp`.
- Observed: FHSS GraphX packet contracts live in `libdsp/include/dsp/fhss/FHSSGraphXPackets.hpp`.
- Observed: FHSS GraphX token aliases are `graph::gpu::accel::ControlToken<PacketT>` specializations, defined in `FHSSGraphXNodeUtils.hpp`.
- Observed: CRSD/focused-image SAR types use `SarPhaseHistoryControlMessage` and `FocusedImageResult`; both embed or preserve a `SarAccelControlToken` but are not themselves `ControlToken<...>` graph edge types.
- Inferred: GraphX currently has a shared accelerator token primitive, but domain packages use it differently: SAR uses a domain sidecar token, DSP spectrum uses message/packet sidecars, and FHSS uses one typed packet sidecar per edge.
- Unknown: Whether the final intended `AccelControlToken<DataType>` model is a single uniform sidecar family or the existing per-domain `ControlToken<PacketT>` pattern.

## 2. Current Node Model

- Observed: Core graph nodes use `NamedSourceNode`, `NamedInteriorNode`, `NamedSinkNode`, `SourceNode`, and `SinkNode` base templates.
- Observed: Generic Metal nodes exist under `libgpu/include/gpu/metal/nodes`, including H2D, D2H, peer copy, shard, lease release, queue sync, host ingress/egress, kernel, transform, reduce, and collective reduce nodes.
- Observed: `docs/sar/metal_node_truth_in_labeling.md` classifies generic Metal transfer/memory/sync/kernel primitive nodes separately from unsupported or experimental domain nodes.
- Observed: SAR stripmap uses `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncAccelNode -> SarBackprojectionTransformAccelNode -> D2HAsyncAccelNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: SAR H2D/D2H/backprojection accel nodes are SAR-specific GraphX nodes with `SarAccelControlToken` ports.
- Observed: `SarBackprojectionTransformAccelNode` can use `DeviceKernelNodeMetal` internally when native capabilities are bound.
- Observed: CRSD focused-image nodes consume and emit typed CRSD/focused-image messages rather than all-token edge contracts after aperture assembly.
- Observed: DSP spectrum graphs include `SineSignalNode`, `CpuSpectrumDftNode`, `DspIqH2DNode`, `MetalSpectrumDftNode`, `DspMagnitudeD2HNode`, and `SpectrumSinkNode`.
- Observed: `MetalSpectrumDftNode` is documented/tested as a Metal direct DFT node, not as a GPU FFT.
- Observed: FHSS real GraphX nodes exist in per-node headers/sources for source, downconverter, channelizer, per-channel detector, correlator-bank detector, merge, candidate, CPSM branch metric, Viterbi, word decoder, preamble detector, message assembler, and message sink.
- Observed: FHSS guardrails assert the old unified FHSS GraphX node header/source are deleted and each FHSS node has its own header/source pair.
- Observed: `ChannelizerNode` is modeled as one sink plus a generated 64-output source base, with one output token type per configured frequency.
- Inferred: The repository currently has both generic GPU nodes and domain-specific GPU-aware nodes; generic primitives are not yet the only graph-visible GPU execution model.
- Unknown: Whether SAR-specific accel nodes are intended to remain or be replaced by generic GPU nodes in a later cleanup.

## 3. Current Token/Data Flow

- Observed: SAR stripmap preserves `SarAccelControlToken` through the full definitive pipeline.
- Observed: SAR source and transfer nodes populate host/device views, leases, tickets, and sidecar fields; SAR identity remains in `SarSidecar`.
- Observed: `host_ptr` is set by SAR source/adapter/transfer paths and by GPU capability implementations as transport state.
- Observed: `ready_event` is set by GPU transfer/kernel capability paths and by SAR/DSP device-backed paths as transport completion state.
- Observed: DSP CPU spectrum flow is `SineSignalNode<256> -> CpuSpectrumDftNode<256> -> SpectrumSinkNode<256>`, using token-wrapped message/magnitude packets.
- Observed: DSP Metal DFT flow uses `DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256>` around the Metal DFT stage.
- Observed: FHSS reference graph is source -> correlator-bank detector -> merge -> candidate -> CPSM metrics -> Viterbi -> word decode -> preamble -> assembler -> sink.
- Observed: FHSS canonical channelized graph is source -> downconverter -> 64-output channelizer -> 64 per-channel pulse detectors -> pulse merge -> downstream decoder/assembler/sink.
- Observed: FHSS decoded decisions carry flags stating truth metadata is not required for decisions.
- Observed: FHSS diagnostics include truth mismatch count, unsupported overlap/impairment rejection, synchronization assumption, and decoded pulse metadata.
- Inferred: GraphX token/data flow is moving toward explicit token contracts, but payload sidecars still vary substantially by domain and stage.
- Unknown: Whether all graph configs marked with accel-token fully enforce token-only edges at runtime.

## 4. Resolver Substitution Flow

- Observed: `GraphExecutorBuilder` builds from JSON configs, plugin directories, executor timeout, policies, and provider bootstrap.
- Observed: `NodeProviderBootstrap` loads plugin providers from one or more plugin directories.
- Observed: `GraphBuilder` loads JSON resolver mappings into `NodeResolutionRegistry`.
- Observed: `NodeResolutionRegistry::CreateDefault()` registers generic GPU intents such as `H2DAsyncNode`, `D2HAsyncNode`, `DeviceTransformNode`, `DeviceKernelNode`, `DeviceReduceNode`, and `QueueSyncNode`.
- Observed: `ResolvingNodeProvider` resolves intents by backend preference and fallback policy.
- Observed: Auto backend preference order is `metal`, `sycl`, `stub`, `cuda`.
- Observed: Strict backend policy does not append fallback backends; `allow_fallback` appends remaining backends.
- Observed: SAR configs add local mappings for SAR-specific accel node intents, with `SarAccelControlToken` input/output token strings.
- Observed: FHSS GraphX executor tests load FHSS nodes through `GraphExecutorBuilder` and plugin provider paths.
- Observed: DSP tests check JSON graph runtime and truth-in-labeling behavior for CPU and Metal DFT lanes.
- Inferred: Resolver substitution currently supports both default generic GPU contracts and domain-local resolver mappings.
- Unknown: Whether resolver token strings are enforced as actual C++ port type compatibility or mostly as diagnostics/config metadata.

## 5. Violations Of Accel-Token Architecture

- Observed: FHSS and DSP/FHSS node ports use `graph::gpu::accel::ControlToken<...>` for the inspected GraphX nodes.
- Observed: SAR stripmap GPU path uses `SarAccelControlToken`.
- Observed: CRSD focused-image transform and sink expose `SarPhaseHistoryControlMessage` and `FocusedImageResult` as graph port types, not `ControlToken<...>`.
- Observed: Several SAR/CRSD configs declare top-level `edge_contract: "accel-token"` while CRSD image-formation stages include non-token typed edges.
- Observed: SAR H2D/D2H nodes are domain-specific accel-token transfer nodes rather than direct graph-visible generic GPU transfer nodes.
- Observed: `SarBackprojectionTransformAccelNode` wraps generic Metal kernel execution internally rather than exposing the generic kernel primitive as a graph stage in the definitive SAR topology.
- Observed: `CollectiveReduceNodeMetal` exists but is documented/tested as unsupported.
- Observed: `CrsdFocusedImageTransformMetalNode` exists but is documented/tested as fallback plus experimental incomplete.
- Inferred: The largest strict-architecture violations are the CRSD typed message/result graph edges and domain-specific SAR GPU wrappers.
- Unknown: Whether FHSS `ControlToken<FHSS...Packet>` satisfies the role's `AccelControlToken<DataType>` target or is only adjacent to it.

## 6. Obsolete Abstractions

- Observed: FHSS pre-GraphX unified node scaffolding is guardrailed as deleted.
- Observed: FHSS guardrails reject aggregate channelizer stream packet contracts as canonical output types.
- Observed: Legacy SAR payload names appear in negative tests and guardrails rather than active stripmap edges.
- Observed: `CollectiveReduceNodeMetal` is active in inventory but unsupported.
- Observed: `CrsdFocusedImageTransformMetalNode` is active but explicitly experimental incomplete.
- Observed: `NodeFacadeAdapterWrapper` is used by helper functions/tests to recover concrete sinks from graph managers after execution.
- Observed: `SarBackendKind`, resolver backend strings, and node-level backend fields coexist.
- Inferred: The repository has partially removed old SAR/FHSS pseudo-node and legacy payload abstractions, but still has transitional domain-specific GPU abstractions.
- Unknown: Whether `NodeFacadeAdapterWrapper` sink resolution is intended as a permanent testing/runtime utility or a transitional accessor.

## 7. Complexity Hotspots

- Observed: The repository supports core graph runtime, generic GPU/Metal runtime, DSP spectrum, FHSS, SAR stripmap, CRSD/GOTCHA ingestion, focused-image formation, and external baseline tooling in one workspace.
- Observed: There are multiple token sidecar styles: `ControlToken<Message>`, `ControlToken<MagnitudePacket>`, `ControlToken<SarSidecar>`, and many `ControlToken<FHSS...Packet>` aliases.
- Observed: SAR has overlapping backend controls across top-level resolver config, resolver mappings, node-level backend fields, and node-specific execution backend fields.
- Observed: FHSS canonical channelized JSON is large: 75 nodes and 137 edges, with 64 repeated detector nodes.
- Observed: `ChannelizerNode` has template-generated 64 output port types, while JSON represents those as explicit source-port connections to detector instances.
- Observed: `examples/SAR/src/sar_benchmark.cpp` is a broad benchmark/reporting harness that includes graph execution, baseline parity, telemetry, cost attribution, and trace output.
- Observed: `examples/DSP/src/main.cpp` contains CPU/Metal comparison, JSON reporting, strict optional speed gate, and executor timing reporting.
- Observed: `SarRuntimeHelpers.hpp` defines one SAR `ElapsedUs` helper; no duplicate `elapsedUs` function was found in active SAR/DSP/GPU/Graph code.
- Observed: `CrsdFocusedImageTransformMetal.cpp` contains a local microsecond duration calculation separate from `ElapsedUs`.
- Inferred: The highest current complexity areas are backend truth-in-labeling, sidecar/token unification, and graph shape complexity for 64-port FHSS channelization.
- Unknown: Whether the repeated explicit FHSS detector topology will remain manageable for larger configured frequency tables.

## 8. Blockers For `AccelControlToken<DataType>`

- Observed: Generic `ControlToken<SidecarT>` exists and is widely used.
- Observed: The repository does not expose one single domain-neutral `AccelControlToken<DataType>` alias used by all DSP/SAR/FHSS graph nodes.
- Observed: DSP spectrum uses `ControlToken<Message>` as an input carrier and `ControlToken<MagnitudePacket<...>>` as an output carrier.
- Observed: FHSS uses one `ControlToken<FHSS...Packet>` type per edge contract.
- Observed: SAR uses `ControlToken<SarSidecar>` for stripmap but typed CRSD message/result edges for focused-image formation.
- Observed: Generic libgpu nodes operate on host/device view sidecars rather than SAR/FHSS semantic sidecars.
- Observed: Resolver token type strings document contracts but do not by themselves eliminate domain-specific wrappers.
- Inferred: A single `AccelControlToken<DataType>` architecture is blocked by sidecar variance, CRSD non-token edges, and domain-specific GPU wrapper nodes.
- Inferred: A generic GPU-node-only graph is blocked by missing graph-visible adapters that preserve domain sidecar semantics across generic transfer/kernel primitives.
- Unknown: Whether the desired `DataType` should be an enum, packet type, sidecar type, or a `graph::message::Message` payload convention.

## 9. Existing External Comparison/Baseline Hooks

- Observed: SAR external comparison tooling includes SarPy scripts, gotcha-back adapter, image comparator, local runner, scenario files, and local-only validation docs.
- Observed: `.github/workflows/sarpy-integration.yml` is opt-in.
- Observed: SAR docs state SarPy is optional/local-only and not a GraphX runtime dependency.
- Observed: Tests cover SarPy harness behavior, gotcha-back adapter contracts, local runner contracts, external baseline policy, and image comparison lanes.
- Observed: DSP performance comparison tooling exists for CPU versus Metal direct DFT, including a JSON schema and optional strict local gate.
- Observed: DSP truth-in-labeling tests prevent the Metal DFT lane from being documented as a GPU FFT and prevent general GPU speedup claims.
- Observed: SAR benchmark code compares graph diagnostics with an internal baseline pipeline and emits trace/report fields.
- Inferred: External packages are currently used for comparison, validation, conversion, and local workflow boundaries, not as GraphX core runtime dependencies.
- Unknown: Whether SarPy-assisted CRSD writing remains the long-term standards-targeted writer path.

## Current-State Summary

- Observed: GraphX has a functioning core JSON/plugin/executor path, generic GPU/Metal primitives, DSP CPU/Metal demo lanes, SAR accel-token stripmap lane, and FHSS CPU fixture lanes.
- Observed: Examples are covered by tests: SAR main executable, DSP spectrum executable, FHSS GraphExecutorBuilder paths, GraphX resolver/provider tests, and truth-in-labeling guardrails.
- Observed: Examples report performance metrics unevenly: DSP has executor timing and CPU/Metal report schema; SAR has richer benchmark reporting but the simple SAR example executable reports only basic runtime/diagnostics.
- Observed: Metal is the first backend in resolver auto preference and is covered by truth-in-labeling inventory/tests.
- Inferred: The repository is mid-cleanup: many accel-token guardrails now exist, especially for FHSS and SAR stripmap, but the architecture is not yet a single uniform `AccelControlToken<DataType>` model across all domains.
- Inferred: The main architectural ambiguity is whether domain-specific token sidecars and typed CRSD product edges are accepted current architecture or remaining cleanup targets.
- Unknown: No recommendation is made here because this inspector report is limited to current-state analysis.
