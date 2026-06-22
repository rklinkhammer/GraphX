# GraphX Node Generalization Candidate Report

Source request: GraphX Node Generalization Plan attachment.

Principle: generalize mechanics, not meaning. Existing node class names, port names, port counts, token contracts, JSON behavior, and observable execution semantics are treated as fixed.

## High-Confidence Refactors

### 1. Token-Preserving 1->1 Transform Mechanics

Observed candidates:

- `RangeWindowNode`
- `RangeCompressionNode`
- `AzimuthTileSplitNode`
- `FHSSPulseCandidateNode`
- `FHSSDownconverterNode`
- `CPSMBranchMetricNode`
- `CPSMViterbiDecoderNode`
- `FHSSPulseWordDecoderNode`
- `FHSSPreambleDetectorNode`
- `FHSSMessageAssemblerNode`
- `DspMagnitudeD2HNode`
- `DspIqH2DNode`

Observed duplicated mechanics:

- Inherit `NamedInteriorNode<TypeList<Input>, TypeList<Output>, Node>`.
- Parse JSON config and expose parameters.
- Validate an input token or sidecar.
- Build an output token preserving token id and selected transport fields.
- Return `std::optional<OutputToken>`.

Inferred candidate abstraction:

```cpp
template <typename Derived, typename InputToken, typename OutputToken, typename Policy>
class TransformNodeBase;
```

Why high confidence:

- Ports stay unchanged because concrete wrappers keep their current `InputToken` and `OutputToken`.
- The base can own only mechanics: `Transfer` shell, diagnostics hooks, optional token-copy helpers, stable parameter helper.
- Domain logic remains in `Policy`.

Risk:

- Some transforms intentionally suppress output until accumulation or lock; policy must support `std::nullopt`.

### 2. Config/Parameter Boilerplate Helpers

Observed candidates:

- SAR nodes with `Fields()` arrays and repeated `GetParameters`, `GetParameterDescription`, `GetParameterNames`.
- FHSS nodes with stable JSON view helpers.
- GPU nodes with queue/device/backend parameter handling.

Inferred candidate abstraction:

- `ConfigurableNodeParameters<Config, FieldsProvider>`
- `StableJsonParameterMixin`
- `GpuQueueConfigMixin`

Why high confidence:

- Does not affect port contracts.
- Can be introduced as helper functions/mixins before changing inheritance.

Risk:

- Existing docs/tests may depend on exact JSON key order or descriptions.

### 3. GPU Capability Binding And Queue Resolution Mechanics

Observed candidates:

- `H2DAsyncNodeMetal`, `D2HAsyncNodeMetal`, `PeerCopyNodeMetal`, `QueueSyncNodeMetal`
- `DspIqH2DNode`, `DspMagnitudeD2HNode`, `MetalSpectrumDftNode`
- `H2DAsyncAccelNode`, `D2HAsyncAccelNode`, `SarBackprojectionTransformAccelNode`

Observed duplicated mechanics:

- Bind context/shared queue/memory/transfer/kernel capabilities.
- Resolve queue id.
- Track ownership of created queues.
- Validate accel views, leases, transfer tickets, kernel tickets.
- Accept queue/backend compatibility config.

Inferred candidate abstraction:

```cpp
class GpuQueueBindingMixin;
class GpuTransferValidation;
class GpuCapabilityBindingSet;
```

Why high confidence:

- Backend mechanics are repeated while ports remain concrete.
- Can start with helper extraction rather than base-class replacement.

Risk:

- Metal/CUDA/SYCL capability APIs differ enough that only validation/config helpers may be safely shared initially.

## Medium-Confidence Refactors

### 4. Split/Fan-Out Mechanics

Observed candidates:

- `graph::SplitNodeN<T,N>`
- `SarPulseFanoutNode`
- `ChannelizerNode`
- `DeviceShardNodeMetal`

Observed duplicated mechanics:

- One input token/view.
- Multiple output ports.
- Output queues.
- Repeated output port metadata.
- Repeated `Produce(port)` forwarding to a per-port queue.

Inferred candidate abstraction:

```cpp
template <typename Derived, typename InputToken, typename OutputToken, size_t N, typename SplitPolicy>
class SplitNodeBase;
```

Use cases:

- `SarPulseFanoutNode`: duplicate same token to fixed four named ports.
- `ChannelizerNode`: build per-frequency packets and emit to exactly 64 ports.
- GPU sharding: output view slices/leases.

Why medium confidence:

- Mechanics are similar, but output policy differs strongly: duplicate, channelize, shard, route.
- `graph::SplitNodeN` supports simple same-token fan-out only and caps convenience aliases at 8; `ChannelizerNode` needs 64 generated outputs.

Risk:

- Need compile-time port generation without changing port metadata, names, or JSON source-port numbers.

### 5. Merge/Fan-In Mechanics

Observed candidates:

- `graph::MergeNode<N,T,O>`
- `FHSSPulseMergeNode`
- `ImageTileMergeNode`
- `CompletionAggregatorNode`
- `DeviceReduceNodeMetal`
- `CollectiveReduceNodeMetal`

Observed duplicated mechanics:

- Consume on multiple conceptual inputs.
- Buffer or unify partial input.
- Detect completion.
- Emit output only when policy says complete.
- Track duplicates/missing/late input.

Inferred candidate abstraction:

```cpp
template <typename Derived, typename OutputToken, typename MergePolicy, typename PortBinding>
class MergeNodeBase2;
```

Why medium confidence:

- `graph::MergeNode` already covers same-type N-input worker-thread mechanics.
- Domain merges need richer policies: slot grouping, tile completion, duplicate handling, partial suppression, two output ports.

Risk:

- A generic merge base can easily become too abstract if it tries to model every merge/reduce/join case.

### 6. High-Port Repeated Port Binding

Observed candidates:

- `ChannelizerNode`
- `FHSSPulseMergeNode`
- `CompletionAggregatorNode`
- test `MergeNode` / advanced split nodes

Observed duplicated mechanics:

- Generate repeated type lists.
- Implement repeated `Consume` or `Produce` overrides.
- Construct repeated port metadata.

Inferred candidate abstraction:

```cpp
template <typename Token, size_t N>
using RepeatedOutputPorts = ...;

template <typename Token, size_t N>
using RepeatedInputPorts = ...;
```

Why medium confidence:

- This targets mechanics only and preserves concrete names.
- It directly addresses 64-port FHSS node verbosity.

Risk:

- C++ template diagnostics may become harder unless kept small and documented.

## Nodes That Should Remain Specialized

- `CrsdFocusedImageTransformNode`: domain algorithm semantics and image geometry should remain visible.
- `CrsdFocusedImageTransformMetalNode`: experimental incomplete Metal/domain behavior is truth-in-labeling sensitive.
- `SarBackprojectionTransformAccelNode`: SAR algorithm plus native kernel bridge is too domain-specific for an early generic base.
- `MetalSpectrumDftNode`: direct DFT truth-in-labeling and kernel descriptor behavior should remain explicit.
- `FHSSSyntheticIqSourceNode`: message schedule and waveform fixture semantics are domain-specific.
- `SineSignalNode`: already uses `DataProducerWithNotification`; generator semantics should stay explicit.
- `CompletionAggregatorNode`: fixed 5 completion inputs are generic but currently callback/control-specific.

## Split/Merge Findings

- Observed: `graph::SplitNodeN` is a reusable simple fan-out base for same-token duplication, and `SarPulseFanoutNode` already uses it.
- Observed: `ChannelizerNode` is not a simple duplicate split. It creates per-channel packet metadata and enforces one output port per frequency.
- Observed: `FHSSPulseMergeNode` is not a simple same-type merge. It has one correlator-bank input, 64 per-channel inputs, and two candidate outputs.
- Observed: `ImageTileMergeNode` is physically 1->1 but logically N->1 through tile metadata and internal sets.
- Inferred: The right split/merge abstraction must allow policy-defined payload construction and completion, not only port count.

## N/M Node Patterns

- 1->64: `ChannelizerNode`.
- 65->2: `FHSSPulseMergeNode`.
- 1->4: `SarPulseFanoutNode`.
- 5->0: `CompletionAggregatorNode`.
- Logical N->1 on one edge: `ImageTileMergeNode`.
- Logical N->1 device reduction: `DeviceReduceNodeMetal`, `CollectiveReduceNodeMetal`.

Inferred pattern:

- Port binding and queue lifecycle can be generalized.
- Matching, grouping, duplicate handling, and payload semantics should remain policy-specific.

## Risks

- Over-generalization could make graph nodes harder to inspect and debug.
- Port metadata order and names are externally visible through JSON/plugin tooling and tests.
- Some tests may instantiate nodes directly and assume concrete methods.
- GPU nodes have backend-specific capability lifecycles that should not be flattened prematurely.
- Existing truth-in-labeling docs/tests make GPU algorithm claims sensitive to naming and classification changes.

## Recommended Acceptance Criteria For Any Future Refactor

- All existing graph JSON files load unchanged.
- All plugin names and node class names remain unchanged.
- Port count, port index, port name, and token type tests are added before refactor.
- Existing executor tests pass for SAR, DSP, and FHSS examples.
- No new compatibility shim preserves an obsolete public API; wrappers are real nodes preserving current contracts.
- Reports document whether each refactor removes duplicated mechanics without hiding domain meaning.
