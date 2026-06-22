# FHSSPulseMergeNode As A NamedInteriorNode Sketch

This note sketches what `FHSSPulseMergeNode` would look like if it were derived from `NamedInteriorNode` and defined down to the `Transfer` layer.

This is not a claim that the current `NamedInteriorNode` execution semantics are sufficient for every pulse merge behavior. The merge node has stateful many-input association behavior, and there may be additional executor, lifecycle, scheduling, or metadata semantics not visible from the node header alone.

One important `TransferFunction` behavior is confirmed: when `Transfer(...)` returns `std::nullopt`, the executor simply does not enqueue an output for that transfer call. This is valid for stateful accumulation and has no ill effect by itself.

Additional working assumptions for this sketch:

```text
unused input/output port pairs do not need Transfer overloads as long as the graph never links those pairs
each graph link has its own thread moving data toward Consume/Transfer
fairness is therefore controlled by thread scheduling policy, and can be strengthened later with SCHED_FIFO if needed
the merge batch definition is a node configuration choice, not a hard-coded property of NamedInteriorNode
```

## Intended GraphX Shape

The external graph contract remains:

```text
inputs:
  port 0       FHSSDetectedPulseToken
  ports 1..64 FHSSPerChannelPulseEvidenceToken

outputs:
  port 0       FHSSPulseCandidateToken for correlator-bank detected pulses
  port 1       FHSSPulseCandidateToken for per-channel pulse evidence
```

The important part is that this is still a real GraphX node with token-wrapped edge data:

```cpp
using DetectedPulseInputToken = FHSSDetectedPulseToken;
using PerChannelInputToken = FHSSPerChannelPulseEvidenceToken;
using OutputToken = FHSSPulseCandidateToken;
```

where each token is expected to remain a `graph::gpu::accel::ControlToken<...>` carrying the PR7A/PR11 FHSS packet sidecar.

## Conceptual Type Construction

The input type list still needs one detected-pulse port and 64 per-channel ports.

```cpp
namespace dsp::fhss {

namespace detail {

template <typename T, std::size_t N>
struct RepeatType;

template <typename T>
struct RepeatType<T, 0> {
    using type = graph::TypeList<>;
};

template <typename T, std::size_t N>
struct RepeatType {
    using tail = typename RepeatType<T, N - 1>::type;
    using type = typename graph::PrependTypeList<T, tail>::type;
};

} // namespace detail

class FHSSPulseMergeNode final
    : public graph::NamedInteriorNode<
          typename graph::PrependTypeList<
              FHSSDetectedPulseToken,
              typename detail::RepeatType<FHSSPerChannelPulseEvidenceToken, 64>::type>::type,
          graph::TypeList<FHSSPulseCandidateToken, FHSSPulseCandidateToken>,
          FHSSPulseMergeNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    static constexpr std::size_t kDetectedPulseInputPort = 0;
    static constexpr std::size_t kPerChannelInputCount = 64;
    static constexpr std::size_t kPerChannelFirstInputPort = 1;
    static constexpr std::size_t kPerChannelLastInputPort = 64;

    static constexpr std::size_t kDetectedPulseOutputPort = 0;
    static constexpr std::size_t kPerChannelOutputPort = 1;

    using Base = graph::NamedInteriorNode<
        typename graph::PrependTypeList<
            FHSSDetectedPulseToken,
            typename detail::RepeatType<FHSSPerChannelPulseEvidenceToken, 64>::type>::type,
        graph::TypeList<FHSSPulseCandidateToken, FHSSPulseCandidateToken>,
        FHSSPulseMergeNode>;

    using DetectedPulseInputToken = FHSSDetectedPulseToken;
    using PerChannelInputToken = FHSSPerChannelPulseEvidenceToken;
    using OutputToken = FHSSPulseCandidateToken;

    explicit FHSSPulseMergeNode(std::string name = "FHSSPulseMergeNode")
        : Base(std::move(name)) {}

    // IConfigurable / IParameterized are omitted here except for noting
    // that they should keep the existing FHSSPulseMergeConfig and parameter
    // behavior.
};

} // namespace dsp::fhss
```

The exact helper names above may need to match repository conventions. The important point is that the type list is explicit at compile time and contains 65 input port types and 2 output port types.

## Transfer Surface

At the `Transfer` layer, the node has two logical paths:

```text
detected-pulse input port 0
    -> output port 0

per-channel input ports 1..64
    -> stateful accumulation
    -> output port 1 only when the configured merge batch is complete
```

The detected-pulse path can be a direct transform:

```cpp
std::optional<OutputToken> Transfer(
    const DetectedPulseInputToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) override {

    const auto& sidecar = input.sidecar;

    if (sidecar.detected_pulses.size() != sidecar.pulse_evidence.size()) {
        return std::nullopt;
    }

    std::vector<FHSSLocalPulseDetection> detections;
    detections.reserve(sidecar.detected_pulses.size());

    for (std::size_t i = 0; i < sidecar.detected_pulses.size(); ++i) {
        FHSSLocalPulseDetection detection;
        detection.metadata = sidecar.detected_pulses[i];
        detection.evidence = sidecar.pulse_evidence[i];
        detections.push_back(std::move(detection));
    }

    return BuildOutput(input.token_id, detections);
}
```

The per-channel path should not scan across frequencies. Each input port corresponds to one channelized frequency stream, and all received channel packets must carry their own `frequency_index` and `channel_id`.

```cpp
template <std::size_t Port>
std::optional<OutputToken> TransferPerChannel(
    const PerChannelInputToken& input) {

    static_assert(Port >= kPerChannelFirstInputPort);
    static_assert(Port <= kPerChannelLastInputPort);

    return AccumulatePerChannel(input);
}
```

Because C++ virtual dispatch cannot use a templated virtual function directly, the real class would still need concrete `Transfer` overloads for the repeated ports:

```cpp
#define FHSS_PULSE_MERGE_TRANSFER_PER_CHANNEL(PORT)                         \
    std::optional<OutputToken> Transfer(                                    \
        const PerChannelInputToken& input,                                  \
        std::integral_constant<std::size_t, PORT>,                          \
        std::integral_constant<std::size_t, 1>) override {                   \
        return TransferPerChannel<PORT>(input);                             \
    }

FHSS_PULSE_MERGE_TRANSFER_PER_CHANNEL(1)
FHSS_PULSE_MERGE_TRANSFER_PER_CHANNEL(2)
// ...
FHSS_PULSE_MERGE_TRANSFER_PER_CHANNEL(64)

#undef FHSS_PULSE_MERGE_TRANSFER_PER_CHANNEL
```

Unused input/output pairs should not need `Transfer` overloads if those links are not instantiated by the graph. Under that model, only the actually connected port pairs are part of the callable surface:

```text
input 0       -> output 0
inputs 1..64 -> output 1
```

If a future graph accidentally links an unsupported pair, that should be treated as a graph configuration error rather than as a node behavior path.

## Stateful Per-Channel Accumulation

The per-channel path is the part that makes this node awkward as a simple `NamedInteriorNode`. A transfer from one channel packet may not produce an output immediately.

```cpp
std::optional<OutputToken> AccumulatePerChannel(
    const PerChannelInputToken& input) {

    std::lock_guard<std::mutex> lock(per_channel_mutex_);

    const auto& sidecar = input.sidecar;

    if (sidecar.detected_pulses.size() != sidecar.pulse_evidence.size()) {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < sidecar.detected_pulses.size(); ++i) {
        FHSSLocalPulseDetection detection;
        detection.metadata = sidecar.detected_pulses[i];
        detection.evidence = sidecar.pulse_evidence[i];
        pending_per_channel_detections_.push_back(std::move(detection));
    }

    ++pending_per_channel_packet_count_;

    if (!ConfiguredBatchIsComplete()) {
        return std::nullopt;
    }

    auto output = BuildOutput(input.token_id, pending_per_channel_detections_);

    pending_per_channel_detections_.clear();
    pending_per_channel_packet_count_ = 0;

    return output;
}
```

This relies on the existing `TransferFunction` behavior where `std::nullopt` means "no output queued for this transfer call." That makes the accumulator shape viable: per-channel inputs can be consumed one at a time, and the node only emits once the configured batch policy says the accumulated evidence is ready.

The batch policy should be explicit configuration. Examples:

```cpp
enum class FHSSPulseMergeBatchMode {
    kPacketCount,
    kAllConfiguredChannels,
    kTimeWindowSamples,
    kSlotWindow,
};

struct FHSSPulseMergeConfig {
    FHSSPulseMergeBatchMode batch_mode = FHSSPulseMergeBatchMode::kAllConfiguredChannels;
    std::size_t expected_packet_count = 64;
    std::size_t expected_channel_count = 64;
    uint64_t batch_window_samples = 0;
    uint64_t pulse_period_samples = 6500;
};
```

For the channel-per-frequency invariant, the default PR shape should be:

```text
batch_mode = kAllConfiguredChannels
expected_channel_count = configured_frequency_count
```

For unit tests or reduced fixtures, `expected_channel_count` can be reduced without changing the node type.

## Output Construction

Both paths should share the same merge kernel and output packet construction:

```cpp
std::optional<OutputToken> BuildOutput(
    uint64_t token_id,
    const std::vector<FHSSLocalPulseDetection>& detections) const {

    auto merged = FHSSPulseMergeKernel::Merge(detections, config_);

    FHSSPulseCandidateEvidencePacket packet;
    packet.ordered_candidates = std::move(merged.candidates);
    packet.globally_ordered = true;
    packet.unsupported_overlap_rejected = merged.unsupported_overlap_rejected;
    packet.truth_metadata_required_for_decision = false;
    packet.diagnostics = std::move(merged.diagnostics);

    OutputToken output;
    output.token_id = token_id;
    output.sidecar = std::move(packet);

    return output;
}
```

The exact token construction question is only about API mechanics, not about the node contract. The node output type remains `FHSSPulseCandidateToken`. The implementation must use whatever repository-consistent `ControlToken` construction path exists.

For example, the real code might need one of these patterns instead of direct field assignment:

```cpp
auto output = OutputToken::FromSidecar(std::move(packet));
```

or:

```cpp
OutputToken output;
output.SetSidecar(std::move(packet));
output.SetTokenId(token_id);
```

or a message-wrapper pattern if that is how FHSS packets are carried. The sketch uses direct assignment only to show which semantic fields are produced by the merge.

## Port Metadata

The node should still expose stable port metadata:

```text
input 0       DetectedPulseInput
input 1       PerChannelInput0
input 2       PerChannelInput1
...
input 64      PerChannelInput63

output 0      DetectedPulseCandidates
output 1      PerChannelCandidates
```

If `NamedInteriorNode` auto-generates names only from type lists, the class would need repository-consistent metadata overrides so graph JSON and plugin inspection can still identify the 64 per-frequency inputs.

## Confirmed Assumptions

The design above now assumes:

1. Unused destination/port pairs do not need functions if those graph links are never instantiated.
2. Routing is determined by the graph links: input port 0 links to output port 0, and input ports 1..64 link to output port 1.
3. Each link has a thread moving data to `Consume`/`Transfer`; fairness is based on thread scheduling and can be strengthened with `SCHED_FIFO` if required.
4. `std::nullopt` from `Transfer` is a valid "no output queued" result.
5. The definition of a complete merge batch is a configuration parameter.

## Remaining Implementation Choices

The remaining items are implementation choices rather than architectural blockers:

1. Which repository-consistent `ControlToken` construction API should be used to attach the `FHSSPulseCandidateEvidencePacket` sidecar.
2. Which merge batch modes are required for the first `NamedInteriorNode` version.
3. Whether reduced-fixture tests should configure a smaller batch or use private kernel tests for single-packet behavior.

## Practical Assessment

Deriving `FHSSPulseMergeNode` from `NamedInteriorNode` is possible at the type-list level, but it does not make the node much simpler unless GraphX has a first-class pattern for repeated input ports and stateful many-input association.

The resulting node would still need:

```text
65 compile-time input ports
2 compile-time output ports
64 repeated per-channel Transfer overloads
stateful accumulation across per-channel arrivals
duplicate/collision/overlap policy in the merge kernel
stable metadata for every repeated port
token-wrapped PR7A/PR11 packet contracts
```

So the cleanest version is probably not a plain one-input/one-output `NamedInteriorNode`. It is a specialized `NamedInteriorNode` with explicit repeated-port support, or a small generalized GraphX base that understands fixed fan-in/fan-out token nodes without requiring each FHSS node to manually re-create the same boilerplate.
