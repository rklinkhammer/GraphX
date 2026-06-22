# GraphX Node Inventory Report

Source request: GraphX Node Generalization Plan attachment.

Scope: repository inspection only. Ports are treated as fixed external contracts. This report inventories production and example GraphX node implementations plus test-only node families where they expose reusable mechanics.

Classification labels:

- Observed: directly visible in the current repository.
- Inferred: derived from inheritance, port types, config, tests, or implementation shape.
- Unknown: not established by this inspection.

## Inventory Summary

- Observed: GraphX already has generic base mechanics for source, sink, interior, simple same-token split, and same-token merge nodes.
- Observed: Domain packages implement additional mechanics in concrete nodes: SAR sidecar transforms, DSP GPU transfer/DFT nodes, FHSS high-port channelizer/merge nodes, and GPU backend nodes.
- Inferred: Most immediate generalization opportunities are in repeated mechanics, not in public node contracts.

## Node Inventory

| Node | File | Category | Ports | Token / payload type | State | Execution | Routing | Failure | Domain coupling |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `graph::NamedSourceNode` | `libgraph/include/graph/NamedNodes.hpp` | source base | 0 inputs, N outputs | template `Outputs...` | stateless base | lifecycle/queue | 0->N | base queue semantics | generic |
| `graph::NamedSinkNode` | `libgraph/include/graph/NamedNodes.hpp` | sink base | N inputs, 0 outputs | template `Inputs...` | stateless base | lifecycle/queue | N->0 | base queue semantics | generic |
| `graph::NamedInteriorNode` | `libgraph/include/graph/NamedNodes.hpp` | transform base | N inputs, M outputs | template type lists | stateless base | lifecycle/queue | N->M | base optional output | generic |
| `graph::SplitNodeN<T,N>` | `libgraph/include/graph/SplitNode.hpp` | split/fan-out | 1 input, N same-type outputs | `T` | per-output queues | synchronous consume, queued produce | 1->N | enqueue failure returns false | generic |
| `graph::MergeNode<N,T,O>` | `libgraph/include/graph/Nodes.hpp` | merge/fan-in | N same-type inputs, 1 output | input `T`, output `O` | unified input queue, worker thread | async worker | N->1 | nullopt suppresses output | generic |
| `graph::CompletionAggregatorNode` | `libgraph/include/graph/CompletionAggregatorNode.hpp` | barrier sink | 5 completion inputs, 0 outputs | `CompletionSignal` | buffered completion set | synchronous consume | N->0 | config errors, callback status | generic/control |
| `dsp::SineSignalNode<N>` | `libdsp/include/dsp/SineSignalNode.hpp` | source | output 0 IQ token, output 1 completion | `ControlToken<Message>` + `CompletionSignal` | generator state | timed producer | 0->2 | generator exhaustion emits notification | DSP |
| `dsp::CpuSpectrumDftNode<T,N>` | `libdsp/include/dsp/CpuSpectrumDftNode.hpp` | transform | 1 input, 1 output | `ControlToken<Message>` -> `ControlToken<MagnitudePacket<T,N>>` | accumulating window | synchronous transfer | 1->1 | nullopt while accumulating / invalid | DSP |
| `dsp::SpectrumSinkNode<T,N>` | `libdsp/include/dsp/SpectrumSinkNode.hpp` | sink | 1 input | `ControlToken<MagnitudePacket<T,N>>` | history buffer, metrics | synchronous consume | 1->0 | consume false on invalid | DSP |
| `dsp::DspIqH2DNode<N>` | `libdsp/include/dsp/DspIqH2DNode.hpp` | GPU transfer adapter | 1 input, 1 output | `ControlToken<Message>` -> same | last views/tickets | capability-mediated transfer | 1->1 | nullopt on missing packet/capability | DSP/GPU |
| `dsp::MetalSpectrumDftNode<N>` | `libdsp/include/dsp/MetalSpectrumDftNode.hpp` | GPU kernel transform | 1 input, 1 output | `ControlToken<Message>` -> `ControlToken<MagnitudePacket<float,N>>` | kernel descriptor, tickets | Metal kernel | 1->1 | nullopt on invalid device evidence | DSP/GPU |
| `dsp::DspMagnitudeD2HNode<N>` | `libdsp/include/dsp/DspMagnitudeD2HNode.hpp` | GPU transfer adapter | 1 input, 1 output | `ControlToken<MagnitudePacket<float,N>>` -> same | last host view/packet | capability-mediated transfer | 1->1 | nullopt on transfer failure | DSP/GPU |
| `FHSSSyntheticIqSourceNode` | `libdsp/include/dsp/fhss/FHSSSyntheticIqSourceNode.hpp` | source | 0 inputs, 1 output | `ControlToken<FHSSSyntheticIqOutputPacket>` | fixture schedule | synchronous source production | 0->1 | config validation/reject | FHSS |
| `FHSSDownconverterNode` | `libdsp/include/dsp/fhss/FHSSDownconverterNode.hpp` | transform | 1 input, 1 output | `FHSSSyntheticIqToken` -> `FHSSDownconvertedIqToken` | config only | CPU passthrough/translation | 1->1 | false/nullopt on invalid evidence/config | FHSS |
| `ChannelizerNode` | `libdsp/include/dsp/fhss/ChannelizerNode.hpp` | split/fan-out | 1 input, 64 outputs | `FHSSDownconvertedIqToken` -> 64 `FHSSChannelizedIqToken` | 64 output queues | synchronous consume, queued produce | 1->64 | consume false on invalid config/evidence | FHSS |
| `PerChannelPulseDetectorNode` | `libdsp/include/dsp/fhss/PerChannelPulseDetectorNode.hpp` | transform | 1 input, 1 output | `FHSSChannelizedIqToken` -> `FHSSPerChannelPulseEvidenceToken` | config only | CPU detector | 1->1 | emits empty/evidence or rejects invalid | FHSS |
| `FHSSCorrelatorBankDetectorNode` | `libdsp/include/dsp/fhss/FHSSCorrelatorBankDetectorNode.hpp` | transform/detector | 1 input, 1 output | `FHSSSyntheticIqToken` -> `FHSSDetectedPulseToken` | config only | CPU detector | 1->1 | nullopt on invalid evidence/config | FHSS |
| `FHSSPulseMergeNode` | `libdsp/include/dsp/fhss/FHSSPulseMergeNode.hpp` | merge/fan-in | 65 inputs, 2 outputs | detected/per-channel tokens -> candidate tokens | mutex, pending per-channel detections, output queues | synchronous consume, queued produce | N->2 | nullopt/false on inconsistent evidence | FHSS |
| `FHSSPulseCandidateNode` | `libdsp/include/dsp/fhss/FHSSPulseCandidateNode.hpp` | pass-through transform | 1 input, 1 output | `FHSSPulseCandidateToken` -> same | stateless | synchronous transfer | 1->1 | pass-through | FHSS |
| `CPSMBranchMetricNode` | `libdsp/include/dsp/fhss/CPSMBranchMetricNode.hpp` | transform | 1 input, 1 output | candidate token -> branch metric token | stateless | CPU metric computation | 1->1 | invalid evidence skipped/nullopt | FHSS |
| `CPSMViterbiDecoderNode` | `libdsp/include/dsp/fhss/CPSMViterbiDecoderNode.hpp` | transform | 1 input, 1 output | branch metric token -> symbol decision token | stateless | CPU Viterbi/MLSE | 1->1 | status fields for invalid/low confidence | FHSS |
| `FHSSPulseWordDecoderNode` | `libdsp/include/dsp/fhss/FHSSPulseWordDecoderNode.hpp` | transform | 1 input, 1 output | symbol decision token -> decoded words token | stateless | CPU word assembly | 1->1 | status fields | FHSS |
| `FHSSPreambleDetectorNode` | `libdsp/include/dsp/fhss/FHSSPreambleDetectorNode.hpp` | transform | 1 input, 1 output | decoded words token -> assembled message token | config only | CPU preamble lock | 1->1 | status/diagnostics | FHSS |
| `FHSSMessageAssemblerNode` | `libdsp/include/dsp/fhss/FHSSMessageAssemblerNode.hpp` | transform | 1 input, 1 output | assembled message token -> same | config only | CPU assembly/truth compare | 1->1 | status/diagnostics | FHSS |
| `FHSSMessageSinkNode` | `libdsp/include/dsp/fhss/FHSSMessageSinkNode.hpp` | sink | 1 input | `FHSSAssembledMessageToken` | last message/diagnostics | synchronous consume | 1->0 | false on invalid input unknown | FHSS |
| `SyntheticApertureIqSourceNode` | `examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp` | source | 0 inputs, 1 output | `SarAccelControlToken` | synthetic stream counters | producer | 0->1 | config validation / EOS | SAR |
| `GotchaReplaySourceNode` | `examples/SAR/include/sar/GotchaReplaySourceNode.hpp` | source | 0 inputs, 1 output | `SarAccelControlToken` | replay index | producer | 0->1 | config/data validation | SAR |
| `OrderedCrsdSetInputSourceNode` | `examples/SAR/include/sar/OrderedCrsdSetInputSourceNode.hpp` | source | 0 inputs, 1 output | `SarAccelControlToken` | CRSD read state | producer | 0->1 | read/config failure | SAR/CRSD |
| `RangeWindowNode` | `examples/SAR/include/sar/RangeWindowNode.hpp` | transform | 1 input, 1 output | `SarAccelControlToken` -> same | config only | synchronous transfer | 1->1 | pass-through on disabled/EOS | SAR |
| `RangeCompressionNode` | `examples/SAR/include/sar/RangeCompressionNode.hpp` | transform | 1 input, 1 output | `SarAccelControlToken` -> same | config only | synchronous transfer | 1->1 | pass-through/validation | SAR |
| `AzimuthTileSplitNode` | `examples/SAR/include/sar/AzimuthTileSplitNode.hpp` | split-like transform | 1 input, 1 output | `SarAccelControlToken` -> same | config only | synchronous transfer | 1->1 logical tiling | nullopt unknown; builds data/EOS tile | SAR |
| `SarPulseFanoutNode` | `examples/SAR/include/sar/SarPulseFanoutNode.hpp` | split/fan-out | 1 input, 4 outputs | `SarAccelControlToken` | per-output queues from base | synchronous consume, queued produce | 1->4 | base enqueue failure | SAR |
| `H2DAsyncAccelNode` | `examples/SAR/include/sar/H2DAsyncAccelNode.hpp` | GPU transfer adapter | 1 input, 1 output | `SarAccelControlToken` -> same | transfer sequence, last metadata | simulated/native transfer boundary | 1->1 | nullopt on invalid host view | SAR/GPU |
| `SarBackprojectionTransformAccelNode` | `examples/SAR/include/sar/SarBackprojectionTransformAccelNode.hpp` | GPU/domain transform | 1 input, 1 output | `SarAccelControlToken` -> same | kernel binding/config | native or simulated kernel path | 1->1 | nullopt on invalid device view | SAR/GPU |
| `D2HAsyncAccelNode` | `examples/SAR/include/sar/D2HAsyncAccelNode.hpp` | GPU transfer adapter | 1 input, 1 output | `SarAccelControlToken` -> same | transfer sequence | simulated/native transfer boundary | 1->1 | nullopt on invalid device view | SAR/GPU |
| `ImageTileMergeNode` | `examples/SAR/include/sar/ImageTileMergeNode.hpp` | merge/aggregator transform | 1 input, 1 output | `SarAccelControlToken` -> same | seen/eos tile sets, counters, timing totals | synchronous accumulating transfer | N logical -> 1 physical | suppress until complete / duplicate counters | SAR |
| `SarDiagnosticsSinkNode` | `examples/SAR/include/sar/SarDiagnosticsSinkNode.hpp` | sink | 1 input | `SarAccelControlToken` | last diagnostics/counters | synchronous consume | 1->0 | returns bool | SAR |
| `SarMaterializedImageSinkNode` | `examples/SAR/include/sar/SarMaterializedImageSinkNode.hpp` | sink-like transform | 1 input, 1 output | `SarAccelControlToken` -> same | last capture metadata | synchronous pass-through/write | 1->1 | false/nullopt on invalid capture | SAR |
| `SarVisualizationSinkNode` | `examples/SAR/include/sar/SarVisualizationSinkNode.hpp` | sink-like transform | 1 input, 1 output | `SarAccelControlToken` -> same | artifact paths/config | synchronous pass-through/write | 1->1 | false/nullopt on write/invalid | SAR |
| `CrsdApertureAssemblyAdapterNode` | `examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp` | adapter/accumulator | 1 input, 1 output | `SarAccelControlToken` -> `SarPhaseHistoryControlMessage` | segment accumulation | synchronous assembly on EOS | 1 stream -> 1 frame | nullopt until frame complete | SAR/CRSD |
| `CrsdFocusedImageTransformNode` | `examples/SAR/include/sar/CrsdFocusedImageTransformNode.hpp` | transform | 1 input, 1 output | `SarPhaseHistoryControlMessage` -> `FocusedImageResult` | config only | CPU backprojection | 1->1 | nullopt on invalid frame | SAR/CRSD |
| `CrsdFocusedImageTransformMetalNode` | `examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp` | GPU/domain transform | 1 input, 1 output | `SarPhaseHistoryControlMessage` -> `FocusedImageResult` | capabilities/config/diagnostic | Metal placeholder + CPU seed/fallback | 1->1 | fallback/nullopt depending config | SAR/CRSD/GPU |
| `CrsdFocusedImageSinkNode` | `examples/SAR/include/sar/CrsdFocusedImageSinkNode.hpp` | sink-like transform | 1 input, 1 output | `FocusedImageResult` -> same | output artifact state | synchronous write/pass-through | 1->1 | false/nullopt on persist failure | SAR/CRSD |
| `HostIngressPinnedSourceNodeMetal` | `libgpu/include/gpu/metal/nodes/HostIngressPinnedSourceNodeMetal.hpp` | GPU source | 0 inputs, 1 output | `HostPinnedBufferView` | memory pool/config | capability-mediated allocation | 0->1 | nullopt on allocation failure | GPU/Metal |
| `H2DAsyncNodeMetal` | `libgpu/include/gpu/metal/nodes/H2DAsyncNodeMetal.hpp` | GPU transfer | 1 input, 1 output | `HostPinnedBufferView` -> `DeviceBufferView` | queue/lease/ticket | capability-mediated async copy | 1->1 | nullopt on invalid view/capability | GPU/Metal |
| `D2HAsyncNodeMetal` | `libgpu/include/gpu/metal/nodes/D2HAsyncNodeMetal.hpp` | GPU transfer | 1 input, 1 output | `DeviceBufferView` -> `HostPinnedBufferView` | queue/lease/ticket | capability-mediated async copy | 1->1 | nullopt on invalid view/capability | GPU/Metal |
| `PeerCopyNodeMetal` | `libgpu/include/gpu/metal/nodes/PeerCopyNodeMetal.hpp` | GPU transfer | 1 input, 1 output | `DeviceBufferView` -> `DeviceBufferView` | queue/ticket | capability-mediated device copy | 1->1 | nullopt on invalid view/capability | GPU/Metal |
| `DeviceShardNodeMetal` | `libgpu/include/gpu/metal/nodes/DeviceShardNodeMetal.hpp` | GPU split/shard | 1 input, N-like output contract by config | `DeviceBufferView` | shard config | capability-mediated memory partition/copy | 1->N logical | nullopt on invalid shard | GPU/Metal |
| `LeaseReleaseNodeMetal` | `libgpu/include/gpu/metal/nodes/LeaseReleaseNodeMetal.hpp` | GPU sink | 1 input | `BufferLease` | memory pool | release side effect | 1->0 | false on release failure | GPU/Metal |
| `QueueSyncNodeMetal` | `libgpu/include/gpu/metal/nodes/QueueSyncNodeMetal.hpp` | GPU sync transform | 1 input, 1 output | `DeviceBufferView` -> same | context/queue/event | sync/event creation | 1->1 | nullopt on missing capability | GPU/Metal |
| `DeviceKernelNodeMetal` | `libgpu/include/gpu/metal/nodes/DeviceKernelNodeMetal.hpp` | GPU kernel primitive | 1 input, 1 output | `DeviceBufferView` -> `DeviceBufferView` | descriptor/kernel ticket | Metal kernel dispatch | 1->1 | nullopt on invalid descriptor/capability | GPU/Metal |
| `DeviceTransformNodeMetal` | `libgpu/include/gpu/metal/nodes/DeviceTransformNodeMetal.hpp` | GPU kernel primitive | 1 input, 1 output | `DeviceBufferView` -> `DeviceBufferView` | transform config | Metal kernel dispatch | 1->1 | nullopt on invalid input/capability | GPU/Metal |
| `DeviceReduceNodeMetal` | `libgpu/include/gpu/metal/nodes/DeviceReduceNodeMetal.hpp` | GPU reduce | 1 input, 1 output | `DeviceBufferView` -> `DeviceBufferView` | reduce config | Metal kernel dispatch | N logical -> 1 | nullopt on invalid input/capability | GPU/Metal |
| `CollectiveReduceNodeMetal` | `libgpu/include/gpu/metal/nodes/CollectiveReduceNodeMetal.hpp` | GPU collective | N logical inputs, 1 output | device views | collective capability | currently unsupported | N->1 | unsupported/failure | GPU/Metal |
| `HostEgressSinkNodeMetal` | `libgpu/include/gpu/metal/nodes/HostEgressSinkNodeMetal.hpp` | GPU sink | 1 input | `HostPinnedBufferView` | last host view | synchronous consume | 1->0 | false on invalid input | GPU/Metal |
| CUDA node family | `libgpu/include/gpu/cuda/nodes/*.hpp` | GPU transfer/source/sink | same shapes as generic H2D/D2H/ingress/egress/release | accel views/leases | backend capability state | backend-mediated | 0->1, 1->1, 1->0 | nullopt/false on invalid backend | GPU/CUDA |
| SYCL node family | `libgpu/include/gpu/sycl/nodes/*.hpp` | GPU transfer/source/sink | same shapes as generic H2D/D2H/ingress/egress/release | accel views/leases | backend capability state | backend-mediated | 0->1, 1->1, 1->0 | nullopt/false on invalid backend | GPU/SYCL |
| Test node family | `libgraph/test/include/test/*.hpp`, `libgraph/test/plugins/*.cpp` | test-only source/sink/interior/split/merge | varied | mostly `graph::message::Message`, scalar test types | counters/buffers/failure switches | synchronous/producer/merge worker | all basic shapes | intentional failures for tests | generic/test |

## Shape Classification

| Shape | Observed nodes |
| --- | --- |
| 0->1 | `SyntheticApertureIqSourceNode`, `GotchaReplaySourceNode`, `OrderedCrsdSetInputSourceNode`, `FHSSSyntheticIqSourceNode`, GPU host ingress, test source nodes |
| 0->2 | `SineSignalNode<N>` via `DataProducerWithNotification` |
| 1->0 | `SarDiagnosticsSinkNode`, `FHSSMessageSinkNode`, `SpectrumSinkNode`, GPU host egress/release, test sink nodes |
| 1->1 | Most transform/adapter/GPU transfer nodes: range window, range compression, H2D/D2H adapters, DFT, CPSM/FHSS decode stages, CRSD transforms |
| 1->N | `graph::SplitNodeN`, `SarPulseFanoutNode`, `ChannelizerNode`, GPU sharding logic |
| N->1 | `graph::MergeNode`, `CompletionAggregatorNode`, `ImageTileMergeNode` logical merge, `DeviceReduceNodeMetal`, collective reduce |
| N->M | `FHSSPulseMergeNode` as 65 inputs and 2 outputs; channelized FHSS graph topology as a staged fan-out/fan-in subsystem |

## Initial Findings

- Observed: The repository already has mechanics for simple same-token split and same-token merge, but domain-specific high-port and policy-heavy nodes do not reuse a shared policy base.
- Observed: `ChannelizerNode` and `FHSSPulseMergeNode` contain nearly parallel mechanics: generated type lists, explicit lifecycle delegation, metadata generation, output queues, and macro-expanded port methods.
- Observed: GPU backend nodes repeat queue/capability/config/validation patterns across Metal, CUDA, SYCL, DSP GPU adapters, and SAR GPU adapters.
- Inferred: Transform nodes have the broadest opportunity for a small base that preserves public ports while centralizing config/diagnostic/token-copy mechanics.
- Inferred: Split/merge generalization should be shape-specific and policy-driven; a universal node abstraction would likely hide domain intent.
