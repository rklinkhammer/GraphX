# libaccelgraph Phase 0 Design Note

## Greenfield Rationale

`libaccelgraph` starts as a new package rather than an in-place migration of
`libgpu` so we can define one coherent accelerator graph API without carrying
legacy naming, bootstrap coupling, or compatibility debt. This phase only adds
the scaffold and package integration.

## libgraph Mechanisms Used

Phase 0 code is intentionally small, but the scaffold is aligned to `libgraph`
runtime contracts that will be used in later phases:

- provider boundary patterns from `libgraph/include/graph/NodeProvider.hpp`;
- capability container pattern from `libgraph/include/graph/CapabilityBus.hpp`;
- graph/plugin bootstrap conventions from
  `libgraph/include/graph/NodeProviderBootstrap.hpp`.

The current smoke surface exercises these foundations without introducing
accelerator runtime behavior yet.

## libgpu Ideas Kept as Reference-Only

The following `libgpu` areas informed terminology and future work sequencing,
but are not depended on by `libaccelgraph` in Phase 0:

- transfer-node flow concepts from
  `libgpu/include/gpu/cuda/nodes/H2DAsyncNode.hpp` and
  `libgpu/include/gpu/cuda/nodes/D2HAsyncNode.hpp`;
- capability split patterns from
  `libgpu/include/gpu/cuda/capabilities/ICudaCapabilities.hpp` and
  `libgpu/include/gpu/metal/capabilities/NativeMetalCapabilities.hpp`;
- backend bootstrap toggles from
  `libgpu/include/gpu/bootstrap/GpuCapabilityBootstrap.hpp`.

## Why No Compatibility Layer

No adapters, wrappers, aliases, shims, or dual old/new APIs are introduced.
This avoids freezing old assumptions into the new package and keeps migration
cost visible and explicit in later phases.

## Intended DSP Spectrum Performance Graph

The intended accelerator validation topology for later phases is the DSP
spectrum pipeline shape currently represented in `libdsp`:

- CPU spectrum runtime test path in
  `libdsp/test/unit/test_dsp_spectrum_graph_runtime.cpp`;
- CPU spectrum operator implementation in
  `libdsp/src/dsp/CpuSpectrumDftNode.cpp`;
- Metal reference operator behavior in
  `libdsp/src/dsp/MetalSpectrumDftNode.cpp`;
- frequency-domain packet contract in
  `libdsp/include/dsp/MagnitudePacket.hpp`.

These files inform future parity and performance checks (transfer-inclusive
timing, peak-bin correctness, and backend strictness) but are not modified in
this phase.

## Multi-Host Validation Model

- macOS lane: CPU-only must build and test with Metal optionally enabled in
  later phases when native prerequisites are available.
- Jetson lane: CPU-only must build and test with CUDA optionally enabled in
  later phases when toolchain/device prerequisites are available.
- Shared requirement: core `libaccelgraph` contracts remain backend-neutral and
  verifiable in CPU-only mode on both hosts before native backend claims.

## Phase 2A Transfer-Node Compliance Notes

Phase 2A converted all five transfer nodes in `libaccelgraph` to GraphX
node-base wrappers:

- `HostIngressNode` derives from `graph::NamedSourceNode`;
- `HostToDeviceNode` and `DeviceToHostNode` derive from
  `graph::NamedInteriorNode`;
- `HostEgressNode` and `ReleaseLeaseNode` derive from
  `graph::NamedSinkNode`.

No justified direct-`INode` exceptions were needed for transfer nodes in this
phase.

## Phase 3 Native Metal Provider Notes

Phase 3 adds `MetalAcceleratorProvider` directly in `libaccelgraph` against the
new `IAcceleratorProvider` and `IAcceleratorSession` contracts. The
implementation keeps device, queue, buffer, command buffer, shared event, and
completion tracking internal to the provider/session implementation.

Diagnostic categories are intentionally explicit:

- `Metal support not compiled (ACCELGRAPH_ENABLE_METAL=OFF).`
- `Metal runtime unavailable.`
- `Metal runtime unavailable: no compatible device.`
- `Metal session creation failure.`
- `Metal transfer failure.`

No `IMetal*Capability` inheritance or wrappers are introduced, and transfer
graph node types remain backend-neutral.

## Phase 3A Public Test-Surface Correction Notes

Behavior validation for libaccelgraph transfer paths is now constrained to the
public GraphExecutor surface:

- JSON graph configuration;
- plugin discovery/loading;
- GraphExecutor lifecycle execution;
- sink/output verification.

Direct provider/session/backend runtime behavior tests were removed from
`libaccelgraph/test/unit` and replaced with GraphExecutor-driven tests using
CPU and Metal transfer topology JSON files.

For later phases (including CUDA), do not reintroduce direct provider/session
behavior harnesses in unit tests. Direct tests are allowed only for pure
value/config/schema/compile contracts that do not execute accelerator behavior.