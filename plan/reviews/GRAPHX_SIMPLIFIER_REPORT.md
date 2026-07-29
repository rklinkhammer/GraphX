# GraphX Simplifier Report

## 1. Target Type Model

### Core types to keep

- Keep typed ports and compile-time input/output compatibility.
- Keep `std::expected`-based construction and runtime errors.
- Keep strong enums and structured diagnostics for lifecycle, resolver,
  transfer, kernel, and domain status.
- Keep one accelerator envelope:
  `graph::gpu::accel::ControlToken<PacketT>`.
- Keep backend-neutral buffer views, leases, transfer tickets, and kernel
  tickets in `libgpu`.

### Canonical packet rule

Every edge has exactly one semantic packet type. A domain packet contains only
domain meaning. It must not contain executor state, backend handles, device
pointers, ready events, or plugin/facade state.

An accelerator-ready edge carries:

```text
ControlToken<DomainPacket>
  domain_packet   semantic identity and metadata
  token_id        graph transport correlation
  buffer lease    owned transport storage
  host/device view
  transfer ticket
  kernel ticket
```

A CPU-only edge may carry `DomainPacket` directly. A canonical graph must not
offer both bare-packet and token-wrapped versions of the same edge. The selected
contract is declared in the graph schema and enforced at compile time and load
time.

### Ownership and evidence

- Packet payloads use explicit value ownership or immutable shared storage with
  bounded views. Raw pointers never carry identity or ownership.
- Large IQ/image arrays live in owned storage referenced by a packet or token
  lease; metadata remains small and copyable.
- End-of-stream, watermark, cancellation, and failure are explicit typed control
  states. Sentinel pointers, empty vectors, and missing tickets do not stand in
  for control flow.
- Sample position, sample rate, time, frequency, channel id, frequency-table
  index, decimation, and filter delay use distinct strong types or named fields
  with declared units.
- RF center/reference frequency is distinct from sampled baseband/IF offset.
- Complex evidence is distinct from magnitude products. Magnitude packets can
  feed visualization and metrics, never an FHSS phase-sensitive decoder.

### Domain packet families to keep

- DSP: IQ block, magnitude spectrum, spectrum metrics.
- FHSS: source schedule, channel IQ, detected pulse, merged pulse evidence,
  pulse candidate, CPSM branch metrics, symbol decision, decoded word,
  preamble result, assembled message, decoder diagnostics, and impairment
  status.
- SAR: CRSD segment/product metadata, aperture, focused image, tile/artifact
  metadata, and comparison diagnostics.

Each stage owns one packet family with the minimum fields needed by that stage.
Validation-only truth belongs in fixture/test records and does not travel on
production-facing graph edges.

### Naming changes

- Rename `FHSSGraphXPackets` to `FHSSPackets`; GraphX transport is not a domain
  semantic.
- Split `FHSSGraphXNodeUtils` into narrowly named packet aliases, port types,
  and fixture utilities; delete the catch-all name.
- Use `Packet`, `Token`, `View`, `Lease`, `Ticket`, `Status`, and `Diagnostics`
  consistently. Do not call transport metadata a packet sidecar unless it is
  the `DomainPacket` parameter of `ControlToken`.

## 2. Target Node Model

### Core node model

GraphX core has one public family of real typed graph nodes:

```text
TypedSourceNode<Out>
TypedTransformNode<In, Out>
TypedSinkNode<In>
TypedFixedFanNode<Inputs..., Outputs...>
```

All public `...Node` classes derive from one of these shapes. The node bases own
lifecycle, port exposure, metrics, queues, and typed dispatch. Domain nodes own
only configuration validation and domain transformation.

`TypedFixedFanNode` is the sole repeated-port mechanism. It incorporates the
useful behavior now spread across named fixed-fan bases and routed input,
output, and transfer helpers. Port tables are generated from compile-time
descriptors; domain nodes do not implement per-port virtual boilerplate.

### Runtime and construction

- Keep `GraphExecutorBuilder` as the only public graph execution entry point.
- Keep JSON loading, plugin discovery, provider registration, capability
  resolution, policy installation, and executor construction behind the
  builder.
- Keep one provider interface. A registry creates concrete nodes; an optional
  resolver maps intent to a registered concrete type. Resolution emits a
  typed deterministic diagnostic.
- The plugin facade is an ABI boundary only. It does not define a second node
  model or second lifecycle.
- Dynamic edges and statically typed edges share the same port contract and
  lifecycle. Dynamic loading erases types only at the plugin/config boundary.
- Lifecycle completion is explicit. A fan-in node completes only after the
  required inputs have produced end-of-stream/watermark state or a declared
  failure/cancellation policy has fired.

### Domain placement and canonical lanes

- `libgraph`: node shapes, ports, queues, graph lifecycle, executor, policies,
  plugin/provider boundary, resolver, generic metrics, and generic diagnostics.
- `libgpu`: `ControlToken`, transport storage/views/tickets, backend capability
  interfaces, and real transfer/kernel/sync/memory nodes.
- `libdsp`: signal generation, direct DFT, complex IQ, magnitude, FHSS packets,
  FHSS fixture algorithms, and FHSS nodes.
- `examples/SAR` or a future dedicated SAR domain library: CRSD/GOTCHA domain
  packets and nodes, aperture assembly, image formation, artifacts, and
  comparisons. No SAR type enters `libgraph`.
- `examples/DSP/dashboard`: FHSS dashboard server composition, configuration
  editing, scenario stepping, event replay, visualization, and artifact export.
  Only generic graph snapshot/metrics interfaces remain in `libgraph`.
- `tools/` and example-local tools: SarPy, GOTCHA conversion/reference runners,
  SigMF capture, benchmarks, and local comparison harnesses.

### Domain node names

- Rename `ChannelizerNode` to `FHSSFixtureFrequencyChannelizerNode` until it has
  a defined channel filter and measured separation behavior.
- Keep `CpuSpectrumDftNode` and `MetalSpectrumDftNode`; their names correctly
  describe direct DFT algorithms.
- Rename dashboard classes that remain FHSS-specific with an `FHSSDashboard`
  prefix rather than presenting them as generic graph runtime services.
- CPU SAR focused-image formation is the sole canonical SAR image-formation
  lane. A backend-specific node may become canonical only after it performs the
  real algorithm and passes parity tests.

## 3. Deletion List

### Core runtime

- Delete `StaticNodeAdapter` and its compatibility tests/documentation once the
  remaining call sites are moved to real typed nodes or the plugin facade. Do
  not retain an adapter shim.
- Delete redundant node base variants, lifecycle forwarding, port visitors, and
  transfer-routing layers superseded by the four canonical node shapes.
- Delete duplicate facade/plugin reflection registries. There must be one node
  registry and one ABI facade implementation.
- Delete deprecated boolean/throwing construction paths when an equivalent
  `std::expected` path exists; keep one error model.
- Delete the experimental module-pilot build path unless it has an active,
  separately stated product requirement.
- Delete repeated per-library C++ standard assertions after the top-level build
  contract is authoritative.
- Delete duplicate static-library link entries.

### GPU and accelerator

- Delete backend node implementations that only advertise a capability but do
  not execute it. Unsupported operations remain explicit capability results,
  not placeholder nodes.
- Delete synthesized pointer/event identity helpers. Tests may use typed fake
  views and tickets, never pointer-shaped semantic sentinels.
- Delete the experimental/incomplete SAR Metal focused-image node, plugin, and
  canonical-looking config. Reintroduce a Metal image-formation node only with
  a real algorithm, kernel-ticket diagnostics, and CPU parity evidence.
- Delete any FFT-labeled surface backed by direct DFT. Keep the accurately named
  DFT nodes.

### DSP/FHSS

- Delete validation truth from runtime FHSS edge packets.
- Delete any magnitude-only branch that can be selected as canonical FHSS
  decoder input.
- Delete implicit completion based on expected counts or timing. Completion is
  driven by typed end-of-stream/watermark state from every required input.
- Delete hand-written per-channel node/port code. The 64 distinct logical edges
  remain, but their declarations are generated deterministically from one
  frequency-table definition.
- Delete stale PR-number/future-node wording from active packet and node docs.

### Dashboard, examples, tests, and documents

- Delete FHSS-specific controllers, schemas, and configuration mutation logic
  from `libgraph`; move the retained feature to the DSP example application.
- Delete mock dashboard lifecycle behavior once the dashboard invokes the real
  `GraphExecutorBuilder` path.
- Delete string-only architecture tests that duplicate compile-time or runtime
  contract tests. Keep string guardrails only for claims that cannot be encoded
  structurally.
- Delete the monolithic cross-domain test ownership model. Domain tests do not
  belong in the core runtime test executable.
- Delete the stale malformed report
  `plan/reviews/GRAPHX_SIMPLIFIER_REPORT.m` after this report becomes the active
  simplifier output.
- Delete archived concepts from active indexes; do not delete the archive
  itself when it is retained solely for traceability.

## 4. Replacement List

- Replace the overlapping node-base/routed-function hierarchy with the four
  canonical typed node shapes and one compile-time port descriptor mechanism.
- Replace `Nodes.hpp` as an umbrella implementation header with focused headers
  for source, transform, sink, fixed fan, lifecycle, and port descriptors.
- Replace mixed responsibilities in `GraphManager.hpp` with focused internal
  components for graph ownership, lifecycle coordination, edge ownership, and
  metrics snapshots. `GraphExecutor` remains the public lifecycle owner.
- Replace the multiple registry/reflection surfaces with one concrete-node
  registry consumed by both direct and plugin providers.
- Replace plugin-specific node behavior with a thin facade over the same typed
  node interface used in-process.
- Replace ad hoc resolver strings with strong backend, fallback, capability,
  and resolution-result types serialized deterministically to JSON.
- Replace hand-maintained 64-lane FHSS JSON repetition with a deterministic
  authoring/generation tool that emits ordinary expanded GraphX JSON. Runtime
  execution still consumes only the normal JSON loader; generated output must
  preserve 64 separately inspectable ports, detectors, and edges.
- Replace `ChannelizerNode` with
  `FHSSFixtureFrequencyChannelizerNode`, retaining current mix/decimate behavior
  and truthful fixture labeling. A future production channelizer is a different
  node with explicit filter, bandwidth, delay, alias, and separation contracts.
- Replace implicit FHSS merge completion with per-input EOS/watermark tracking
  and deterministic completeness diagnostics. This contract must prevent the
  current 36-versus-72 pulse truncation regardless of scheduling order.
- Replace broad FHSS packet utility headers with stage-specific packets and
  port aliases.
- Replace the generic-looking dashboard runtime/session/configuration classes
  with FHSS-dashboard application services plus a small generic read-only graph
  snapshot interface in `libgraph`.
- Replace dashboard test doubles for start/stop with tests around the real
  builder/executor lifecycle; keep pure service tests only for configuration
  validation and serialization.
- Replace the monolithic `libgraph` test target with core runtime tests and a
  separate `libdsp`/FHSS target. Replace the monolithic SAR target with focused
  CRSD I/O, SAR nodes, runtime integration, and local-only test targets.
- Replace `NativeMetalCapabilities.cpp` with cohesive implementation units for
  device discovery, memory/transfer, kernel dispatch, synchronization, and
  diagnostics while retaining one capability interface.
- Replace `sar_benchmark.cpp` with a small runner plus separate dataset,
  measurement, trace, and reporting components. Benchmark code remains an
  example/tool and never defines runtime contracts.
- Replace placeholder backend success with explicit `unsupported` results that
  name the backend, capability, node, and reason.
- Replace local external-package assumptions with subprocess/file artifact
  contracts in local-only tools. GraphX core and canonical CI remain free of
  SarPy, GOTCHA, MATLAB, and external dataset dependencies.

## 5. Architecture Invariants

1. `GraphExecutorBuilder` and the repository JSON/plugin/provider path are the
   only canonical graph construction and execution route.
2. A public `...Node` is a real GraphX node derived from one canonical typed
   node shape. Private kernels and algorithms do not use `Node` names.
3. Every edge has one declared packet type. No canonical graph maintains bare
   and token-wrapped variants of the same edge.
4. Accelerator-ready edges use `ControlToken<DomainPacket>`. Domain identity is
   entirely in `DomainPacket`; pointers, views, events, leases, and tickets are
   transport only.
5. Token sidecars and required diagnostics survive every transfer, kernel,
   split, merge, and failure boundary unless a node explicitly produces a new
   semantic packet type.
6. Core GraphX contains only domain-neutral graph concerns. DSP, FHSS, SAR,
   dashboard scenario behavior, and external package assumptions remain outside
   core.
7. `libgpu` contains backend-neutral accelerator contracts and nodes that
   perform real transfer/kernel/sync/memory work. Unsupported behavior is an
   explicit result, never simulated success.
8. There is one registry, one provider interface, one resolver flow, and one
   plugin facade boundary. Dynamic loading does not create a second node model.
9. Fixed fan-in/fan-out nodes use one generated typed-port mechanism. FHSS keeps
   one distinct output edge per configured frequency and no aggregate channel
   stream packet.
10. Fan-in completion accounts for every required input through typed EOS,
    watermark, failure, or cancellation state. Scheduling order cannot silently
    truncate data.
11. The canonical FHSS decoder consumes complex IQ evidence, CPSM branch
    metrics, and Viterbi/MLSE decisions. Magnitude products are observational
    only.
12. RF metadata is never represented as sampled RF unless the sample-rate,
    bandwidth, downconversion, and alias model support that claim.
13. Current FHSS mix/decimate behavior is fixture channelization. Production
    channelization requires explicit filter response, delay, bandwidth, alias,
    and separation evidence.
14. CPU focused-image formation is the single canonical SAR lane until a real
    accelerator implementation passes deterministic CPU parity. Experimental
    placeholders are not canonical alternatives.
15. Deterministic CI fixtures and typed correctness tests precede performance,
    external-data, native-GPU, RF, or production claims.
16. Local-only tools are opt-in, artifact-oriented, and external to runtime
    contracts. Missing packages or datasets produce deterministic skips.
17. Truth-in-labeling is structural where possible and documented where not:
    direct DFT is not FFT, metadata RF is not sampled RF, fixture behavior is
    not production behavior, and accelerator-ready types are not proof of GPU
    execution.
18. No compatibility shim, duplicate canonical path, or deprecated abstraction
    survives solely because an old test or archived document names it.

## 6. Open Questions That Block Planning

1. What causes the reproducible FHSS 36-versus-72 pulse result: premature merge
   completion, source schedule construction, detector loss, or message-sink
   termination? The failing boundary must be identified before sequencing the
   FHSS node/port simplification.
2. Does `StaticNodeAdapter` provide any active capability that the typed node
   interface plus plugin facade cannot express? If yes, that capability must be
   named explicitly; otherwise deletion is unblocked.
3. Is the embedded dashboard intended to become a reusable GraphX product, or
   is it an FHSS development application? Its ownership determines whether a
   small generic server/session API belongs in `libgraph` or the entire feature
   belongs under `examples/DSP`.
4. Which accelerator backends have an active support commitment and available
   correctness hardware: Metal and/or CUDA? Backend surfaces without an
   owner, hardware lane, and executable capability contract should be deleted
   rather than planned for indefinite preservation.
5. Is the C++ module pilot an active deliverable with a compiler/test matrix? If
   not, its build path should be deleted before other core-header work.
6. Must plugin ABI compatibility be supported across independently built
   releases? Backward source compatibility is not required, but the intended ABI
   boundary determines how aggressively facade/reflection code can collapse.
7. Should SAR remain example-owned, or is a supported SAR domain library an
   intended product boundary? This determines the destination of retained SAR
   packets/nodes when the monolithic example target is split.
