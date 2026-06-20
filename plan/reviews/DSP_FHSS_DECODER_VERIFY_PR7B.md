# DSP FHSS Decoder PR7B Verifier Report

## 1. Executive Verdict

PASS_WITH_FOLLOWUP

PR7B satisfies the core architecture correction: FHSS `...Node` names now identify real GraphX nodes, all node ports are `graph::gpu::accel::ControlToken<...>` sidecars carrying PR7A packets, raw PR7A packets are not exposed as node port types, and the test suite passes. Two follow-ups remain: the renamed algorithm kernels are still public headers rather than private/internal kernels, and detailed PR1-PR7 behavior is still mostly proven by kernel tests while the GraphX node tests cover representative boundary/lane behavior.

## 2. Acceptance Criteria Matrix

| Criterion | Result | Evidence | Notes |
|---|---|---|---|
| Every FHSS `...Node` named component in the target graph is a real GraphX node | PASS | `libdsp/include/dsp/fhss/FHSSGraphXNodes.hpp` defines `FHSSSyntheticIqSourceNode`, `FHSSCorrelatorBankDetectorNode`, `FHSSPulseMergeNode`, `FHSSPulseCandidateNode`, `CPSMBranchMetricNode`, `CPSMViterbiDecoderNode`, `FHSSPulseWordDecoderNode`, `FHSSPreambleDetectorNode`, `FHSSMessageAssemblerNode`, and `FHSSMessageSinkNode` as `NamedSourceNode`, `NamedInteriorNode`, or `NamedSinkNode`. | Search found no remaining public FHSS `...Node` class outside `FHSSGraphXNodes.hpp`. |
| GraphX nodes use PR7A edge packet/contract types for inputs and outputs | PASS | `FHSSGraphXToken<PacketT>` aliases wrap `FHSSSyntheticIqOutputPacket`, `FHSSDetectedPulseEvidencePacket`, `FHSSPulseCandidateEvidencePacket`, `FHSSCpsmBranchMetricPacket`, `FHSSCpsmSymbolDecisionPacket`, `FHSSDecodedPulseWordPacket`, `FHSSDecodedPulseWordsPacket`, `FHSSAssembledMessagePacket`, and `FHSSDiagnosticsPacket`. | The plural decoded-pulse-words packet was added to support batched message-layer edges. |
| Every FHSS GraphX node input/output edge data type is `graph::gpu::accel::ControlToken<...>` | PASS | `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars` statically checks all node input/output port types. | Matches the `DspIqH2DNode` / `CpuSpectrumDftNode` token pattern at the port type level. |
| Raw PR7A FHSS packet types are not exposed directly as GraphX node port types | PASS | Static assertion in `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars`; `FHSSGraphXNodes.hpp` port lists use token aliases only. | Raw packet types appear inside helper functions/tests as sidecar values, not node port types. |
| Type-contract tests prove PR7A packets are token sidecars/payloads preserving semantic metadata independently from future accelerator transport state | PASS | `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars`, `CpuLaneDecodesFirstPulseThroughGraphXNodeApi`, and `PreambleAssemblerAndSinkOperateOnTokenWrappedDecodedWords`. | Tests prove token sidecar type mapping plus metadata survival through representative node transfers. |
| Old public pre-GraphX pseudo-node headers/classes are deleted or renamed into private non-node algorithm kernels | PASS_WITH_FOLLOWUP | Old `...Node` class names are gone from helper headers; they were renamed to `...Kernel`. | The kernels are non-node classes, but they remain public in `libdsp/include/dsp/fhss`. PR7C should decide whether to move them private/internal or add guardrails allowing public algorithm kernels explicitly. |
| No compatibility shim preserves the old pseudo-node API | PASS | Searches found no `FHSS...Node::...` or `CPSM...Node::...` static pseudo-node calls. | Old class names no longer expose the previous static helper API. |
| FHSS tests exercise GraphX node APIs and packet contracts, not direct old helper `Node` calls | PASS_WITH_FOLLOWUP | New `test_fhss_graphx_nodes.cpp` exercises `Produce`, `Transfer`, and `Consume`; old tests now call `...Kernel`, not `...Node`. | No direct old `Node` calls remain, but several tests still include helper headers and exercise kernels directly. |
| PR1-PR7 behavior remains covered through GraphX node tests | PASS_WITH_FOLLOWUP | Representative GraphX node lane tests decode one pulse and assemble a token-wrapped decoded message; full PR1-PR7 behavior remains covered by kernel tests. | Detailed behavior coverage has not all moved to GraphX-node tests. This is acceptable for merge if PR7C/next cleanup broadens GraphX coverage or explicitly keeps kernel tests as lower-level unit coverage. |
| Plugin/provider registration tests exist for nodes exposed through plugins | PASS | Implementer report states no FHSS nodes were exposed through the plugin path; inspection found no FHSS plugin/provider wiring. | No plugin registration test required for PR7B as implemented. |
| No graph JSON, real channelizer, Metal/GPU, Doppler/noise behavior, overlap-aware separation, or production RF claim was added | PASS | Scope search found only guardrail comments and pre-existing fixture flags/docs. | No graph executor wiring or backend execution was added. |

## 3. Scope Assessment

PR7B stayed inside the planned runtime-node conversion scope. It added one GraphX node header, one GraphX node test file, renamed pseudo-node helper classes to non-node kernels, and updated tests to remove old pseudo-node class references. It did not add graph JSON executor wiring, plugin runtime wiring, a real channelizer, GPU execution, Doppler/noise behavior, overlap-aware separation, or production RF claims.

## 4. Architecture Assessment

The FHSS node path now follows the requested accel-token-ready pattern:

```text
GraphX node
  -> graph::gpu::accel::ControlToken<PR7A_PACKET>
  -> GraphX node
```

The nodes are CPU-only today. Accelerator transport fields remain separate from FHSS semantic sidecar metadata, which is consistent with the existing `ControlToken` architecture. SAR accel-token architecture is not modified by this PR.

## 5. Legacy Cruft Assessment

Deleted/removed:

- Old public FHSS pseudo-node class names are removed from helper headers.
- Old direct `...Node::StaticMethod` API calls are removed from tests.

Remaining:

- Public non-node `...Kernel` classes remain in `libdsp/include/dsp/fhss`. They are not GraphX nodes and do not preserve the old class names, but they are still public algorithm surfaces.

Blockers:

- None.

Follow-up:

- PR7C should either move kernels into private/internal implementation headers or explicitly define public algorithm kernels as allowed lower-level API with guardrails preventing `...Node` misuse.

## 6. Test Assessment

Meaningful tests:

- Compile-time token-port checks for every FHSS GraphX node.
- Sidecar type checks proving PR7A packet contracts are carried by `ControlToken`.
- GraphX source/interior/sink API tests using `Produce`, `Transfer`, and `Consume`.
- Representative CPU lane test from synthetic IQ through detector, merge, branch metrics, Viterbi, and pulse-word decode.
- Message-layer token test for preamble detector, assembler, and sink.
- Existing PR1-PR7 deterministic behavior tests still pass through renamed kernels.

Missing/non-blocking:

- More PR1-PR7 edge cases should be exercised through GraphX node tests, not only kernel tests, especially detector rejection paths, merge duplicate/collision policy, Viterbi error handling, low-confidence word decode, missing preamble, payload rejection, and overlap rejection.

Obsolete tests:

- No tests call old pseudo-node class names.

Shallow tests:

- The GraphX node tests are boundary-focused and representative rather than exhaustive.

## 7. Resolver/Substitution Assessment

Not applicable for PR7B as implemented. No FHSS nodes were exposed through plugins, and graph JSON/plugin/provider runtime wiring was explicitly out of scope.

## 8. External Baseline Assessment

Not applicable. No external baseline integration was added.

## 9. Blocking Issues

None.

## 10. Follow-Up Issues

1. Move or classify public `...Kernel` helpers.

   The old pseudo-node names were removed, but the algorithm kernels remain public in `libdsp/include/dsp/fhss`. PR7C should move them private/internal or add an explicit guardrail that public `...Kernel` algorithm surfaces are allowed while public non-GraphX `...Node` surfaces are forbidden.

2. Broaden GraphX-node behavior coverage.

   Current detailed PR1-PR7 behavior remains mostly in kernel tests. Add GraphX-node tests for the key rejection/error paths so the GraphX contract becomes the primary behavioral test surface.

## 11. Minimal Fix Recommendation

No fix is required before merge. The smallest follow-up is to extend PR7C with guardrail tests that ban public FHSS pseudo-node classes and either privatize `...Kernel` helpers or document them as allowed algorithm internals, then add a handful of GraphX-node tests for the highest-risk error paths.

## Build And Test Evidence

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Passed: no work to do.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*'`
  - Passed: 9 tests.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXNodeTest.*'`
  - Passed: 72 tests.
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`
  - Passed: 1/1 test, 80.55 sec.
