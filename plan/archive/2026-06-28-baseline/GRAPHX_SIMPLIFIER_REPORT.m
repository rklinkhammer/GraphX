# GRAPHX SIMPLIFIER REPORT

## 1. Target type model

- Core GraphX types are the stable runtime model:
  - typed source, interior, and sink nodes
  - typed ports
  - typed graph config
  - repository-native executor and plugin/provider loading

- Accelerator-ready GraphX edges use:
  - `graph::gpu::accel::ControlToken<PacketT>`
  - domain packet payloads as sidecars
  - GPU/accelerator transport state kept separate from domain identity

- Raw domain packets may exist as internal algorithm/kernel types, but should not be exposed as public GraphX node port types when the edge is intended to be accelerator-ready.

- GPU transport types remain backend-neutral and domain-free:
  - `BackendKind`
  - `DataType`
  - `TensorLayout`
  - `DeviceBufferView`
  - `HostPinnedBufferView`
  - `BufferLease`
  - `TransferTicket`
  - `KernelTicket`
  - `ControlToken<SidecarT>`

- DSP types stay small and explicit:
  - complex IQ packet/evidence types for signal-bearing data
  - magnitude/spectrum packet types for spectrum display or metrics
  - no magnitude-only packet is a canonical decoder input

- FHSS canonical packet contracts are:
  - synthetic IQ output
  - downconverted IQ
  - channelized IQ
  - per-channel pulse evidence
  - detected pulse evidence
  - pulse candidate evidence
  - CPSM branch metrics
  - CPSM symbol decisions
  - decoded pulse words
  - assembled messages
  - diagnostics

- FHSS packet rules:
  - RF metadata frequency is distinct from IQ offset frequency.
  - global sample time is preserved across every packet boundary.
  - complex evidence is preserved through word recovery.
  - truth metadata may be carried for comparison, but decoder decisions must not depend on truth metadata.
  - receiver channel count equals configured frequency count.
  - for the 64-frequency FHSS fixture, there is one logical channel per frequency index.

- SAR type model:
  - `SarAccelControlToken = graph::gpu::accel::ControlToken<SarSidecar>`
  - SAR identity lives in `SarSidecar`, not in transport fields.
  - CRSD/GOTCHA metadata remains SAR-domain data and must not leak into GraphX core contracts.

- Diagnostics model:
  - every canonical lane emits deterministic metrics and diagnostics.
  - diagnostics use typed packet fields where possible, not ad hoc strings.
  - string guardrails remain useful only for truth-in-labeling checks.

- C++26 target usage:
  - use strong enums for domain states
  - use constexpr constants for protocol constants
  - use `std::span` for non-owning views
  - use `std::expected` or repository-consistent equivalents for validation
  - use concepts/traits for token and port contract checks where they reduce boilerplate

## 2. Target node model

- Public `...Node` classes are real GraphX nodes only.

- Public pseudo-nodes, helper nodes, compatibility nodes, and test-only node facades are not part of the target architecture.

- Generic GraphX node bases remain in GraphX core:
  - named source node
  - named interior node
  - named sink node
  - fixed fan-in/fan-out node support
  - routed input/output/transfer helpers

- Domain nodes use generic GraphX bases instead of reimplementing repeated-port boilerplate.

- User-facing and demo graph execution uses:
  - graph JSON/config
  - plugin/provider resolution
  - `GraphExecutorBuilder`
  - repository-native executor APIs

- DSP target nodes:
  - signal source nodes
  - CPU DFT/spectrum transform nodes
  - DSP IQ host-to-device bridge nodes
  - Metal direct DFT spectrum nodes
  - DSP magnitude device-to-host bridge nodes
  - spectrum sink nodes

- DSP truth-in-labeling:
  - current Metal spectrum path is a direct DFT lane unless a real FFT is implemented and verified.

- FHSS canonical target graph:

```text
FHSSSyntheticIqSourceNode
  -> FHSSDownconverterNode
  -> ChannelizerNode
      -> PerChannelPulseDetectorNode[0]
      -> PerChannelPulseDetectorNode[1]
      -> ...
      -> PerChannelPulseDetectorNode[63]
  -> FHSSPulseMergeNode
  -> FHSSPulseCandidateNode
  -> CPSMBranchMetricNode
  -> CPSMViterbiDecoderNode
  -> FHSSPulseWordDecoderNode
  -> FHSSPreambleDetectorNode
  -> FHSSMessageAssemblerNode
  -> FHSSMessageSinkNode
```

- FHSS channelizer target:
  - exactly 64 GraphX output ports for the 64-entry fixture
  - output port `N` maps to frequency index `N` and channel id `N`
  - ports 0 and 63 exist as receiver guard/metadata channels
  - indices 0 and 63 remain invalid for transmitted preamble/body pulses

- FHSS pulse merge target:
  - one canonical `FHSSPulseMergeNode`
  - derives from shared repeated-port/routed GraphX infrastructure
  - no duplicate `FHSSPulseMergeInteriorNode` as a public node

- FHSS correlator-bank topology:
  - not canonical
  - delete if backward compatibility is not required
  - if retained temporarily, it must be labeled reference-only and local-only

- SAR target nodes:
  - CRSD/GOTCHA ingest nodes
  - aperture assembly/adapter nodes
  - CPU focused-image transform nodes
  - Metal focused-image transform nodes only while explicitly labeled experimental until complete
  - SAR artifact/diagnostic sink nodes

- SAR reference tooling:
  - local validation helpers remain outside GraphX core
  - external package assumptions do not become GraphX runtime contracts

- GPU target nodes:
  - transfer nodes
  - memory nodes
  - sync nodes
  - kernel launch nodes
  - reduce/transform nodes where supported
  - unsupported backend features are explicit unsupported statuses, not partial production claims

## 3. Deletion list

- Delete public FHSS pseudo-node APIs that are not real GraphX nodes.

- Delete `FHSSPulseMergeInteriorNode` as a public node once `FHSSPulseMergeNode` is the canonical routed/fixed fan-in implementation.

- Delete direct tests of old FHSS pseudo-node APIs after equivalent GraphX node tests exist.

- Delete the FHSS correlator-bank topology from the active canonical path:
  - `FHSSCorrelatorBankDetectorNode`
  - correlator-bank graph config
  - correlator-bank plugin registration
  - correlator-bank tests that exist only for compatibility

- Delete aggregate/single-edge channelizer output contracts if present or reintroduced:
  - channelized stream packets
  - vector/list sidecar fanout packets
  - any channelizer output model that hides the one-port-per-frequency invariant

- Delete stale or duplicate SAR graph configs that are not part of the selected canonical/local-reference set.

- Delete active documentation files that duplicate `README.md`, `plan/BASELINE.md`, active agent roles, or current review reports.

- Delete old active docs after archiving only the historical material that still has reference value.

- Delete editor artifacts, including observed swap files under third-party or vendored include trees.

- Delete placeholder-only runtime surfaces when they are not used by a supported path and do not have a concrete near-term contract:
  - placeholder edge registration functions
  - placeholder plugin inspector paths
  - placeholder static adapter paths
  - placeholder built-in command branches
  - template-only placeholder node plugin functions

- Delete tests whose only purpose is preserving deleted compatibility paths.

- Delete documentation claims that imply:
  - production RF support
  - production channelizer separation
  - FHSS overlap-aware separation
  - Doppler/noise/multipath support
  - GPU/Metal acceleration for lanes that are CPU-only
  - FFT behavior where only DFT exists
  - external waveform compatibility where only deterministic fixtures exist

## 4. Replacement list

- Replace ad hoc repeated-port code with:
  - `NamedFixedFanInOutNode`
  - `RoutedInputFn`
  - `RoutedOutputFn`
  - `RoutedTransferFn`
  - generated type-list helpers where a fixed arity such as 64 ports is required

- Replace duplicate FHSS merge implementations with one routed GraphX `FHSSPulseMergeNode`.

- Replace raw packet GraphX port types with `graph::gpu::accel::ControlToken<PacketT>` where accelerator readiness is required.

- Replace FHSS correlator-bank canonical examples with the channelized graph.

- Replace any single-edge channelizer aggregate with 64 explicit output ports.

- Replace source-random FHSS message generation with explicit configured message schedules.

- Replace scattered FHSS frequency derivation logic with one frequency-map/config validation path:
  - RF metadata table
  - IQ center/reference frequency
  - derived IQ offset
  - Nyquist and guard validation

- Replace scattered SAR configs with a small named set:
  - one CI-safe CPU fixture
  - one explicitly experimental Metal fixture
  - one local-only GOTCHA/CRSD validation path
  - one benchmark path if still useful

- Replace doc sprawl with:
  - top-level `README.md` for build/run/test/demo usage
  - `plan/BASELINE.md` for active architecture state
  - active role definitions in `plan/agents`
  - current reports in `plan/reviews`
  - archived history outside the active doc path

- Replace string-only guardrails with structural tests where practical:
  - type-contract tests
  - graph-config tests
  - plugin/provider registration tests
  - executor tests
  - diagnostics schema tests

- Replace external package coupling with artifact comparison boundaries:
  - external SAR/DSP packages may generate references
  - GraphX core must not depend on those packages at runtime

## 5. Architecture invariants

- C++26 is the project language baseline.

- Complexity is a defect.

- Backward compatibility is not preserved unless explicitly required.

- There is one canonical path per capability.

- Reference paths may exist only when explicitly labeled reference-only, local-only, or experimental.

- Public `...Node` classes are real GraphX nodes.

- No public pseudo-node compatibility shims.

- User-facing graph execution uses repository-native GraphX APIs and `GraphExecutorBuilder`.

- User/demo graph nodes are dynamically loadable through the plugin/provider path unless explicitly internal.

- Accelerator-ready GraphX edges use `graph::gpu::accel::ControlToken<PacketT>`.

- Domain identity is stored in sidecar packet fields, not in accelerator transport handles.

- `host_ptr`, `ready_event`, and similar fields are transport state only.

- GraphX core remains domain-neutral.

- SAR, DSP, SDR, FHSS, and GPU semantics stay in their domain libraries.

- FHSS decoder input preserves complex IQ evidence; magnitude-only DFT/FFT output is not canonical decoder evidence.

- FHSS RF metadata frequency and IQ offset frequency remain separate fields.

- FHSS channelized receiver configuration has one logical GraphX output port per configured frequency.

- The 64-frequency FHSS fixture uses exactly 64 channelizer output ports.

- FHSS transmitted pulses may use selectable frequency indices 1 through 62 only.

- FHSS receiver guard/metadata channels 0 and 63 may exist but are not valid transmitted pulse indices.

- FHSS overlap-aware separation remains unsupported until explicitly implemented and tested.

- Doppler, noise, CFO drift, multipath, and production RF claims remain unsupported until explicitly implemented and tested.

- SAR external datasets and references remain local/reference inputs, not required GraphX runtime dependencies.

- GPU backend support is truth-labeled by actual implementation status.

- Diagnostics and metrics are deterministic and testable.

- Documentation must describe current behavior, not desired future behavior as if it exists.

## 6. Open questions that block planning

- Should the FHSS correlator-bank topology be deleted immediately, or retained briefly as a reference-only comparison path?

- Which SAR graph configs are canonical, which are local-only references, and which should be deleted?

- Should CUDA and SYCL simulated/stub paths remain active examples, or should the active GPU baseline be Metal-only until other backends are real?

- Should the current `ChannelizerNode` implementation stay specialized, or should GraphX core gain a generalized fixed fan-out source base for nodes with large output arity?

- Which placeholder runtime/plugin surfaces are intentional extension points, and which are dead code to delete?

- What is the minimum CI test set for the new baseline across GraphX core, DSP, FHSS, SAR, and GPU?

- Which documentation directory is allowed to remain active besides `README.md`, `plan/BASELINE.md`, `plan/agents`, and `plan/reviews`?

- Should active review reports remain in `plan/reviews`, or should only the latest baseline report remain active after simplification?

- What is the exact policy for vendored or third-party artifacts that contain local editor/build residue?

- What production claims, if any, should be allowed before real RF/channelizer validation and SAR external-data validation are complete?
