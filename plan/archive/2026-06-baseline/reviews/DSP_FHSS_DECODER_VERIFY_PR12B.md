# DSP FHSS Decoder PR12B Verifier Report

PR: PR12B - Correct Channelizer Graph Shape To 64 Output Ports
Role: VERIFIER
Date: 2026-06-21
Verdict: PASS

## Summary

PR12B corrects the PR12 channelizer topology. `ChannelizerNode` no longer uses
an aggregate channel stream sidecar as its output contract. It now exposes 64
GraphX output ports, and each output port carries
`graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>`.

The implementation remains within scope: no per-channel detector, graph JSON
end-to-end wiring, real RF capture, production channelizer claim, Metal/GPU
execution, Doppler/noise behavior, or overlap-aware separation was added.

## Required Checks

| Check | Result | Evidence |
| --- | --- | --- |
| `ChannelizerNode` exposes exactly 64 GraphX output ports. | PASS | `ChannelizerNode::NOutputs` and `kOutputPortCount` are `FHSSProtocolConstants::kFrequencyCount`; runtime test checks `GetOutputPortCount() == 64`. |
| Every `ChannelizerNode` output port type is `graph::gpu::accel::ControlToken<FHSSChannelizedIqPacket>`. | PASS | `FHSSChannelizerOutputList` repeats `FHSSChannelizedIqToken` for the 64-entry frequency table; representative static assertions cover ports 0, 1, 62, and 63. |
| Output port `N` maps to frequency index `N` and channel id `N`. | PASS | `Consume()` builds output queue entry `port` from frequency-map entry `port`; runtime tests prove ports 0, 1, 24, 62, and 63 map to matching frequency index/channel id. |
| Ports 0 and 63 exist as receiver guard/metadata channels while indices 0 and 63 remain invalid transmitted preamble/body frequencies. | PASS | Runtime tests prove output ports 0 and 63 are guard/metadata channels; validation tests reject 0 and 63 in transmitted active/pulse frequency config. |
| No `FHSSChannelizedIqStreamPacket`, `FHSSChannelizedIqStreamToken`, vector/list stream sidecar, fanout payload, or aggregate single-edge channelizer output is canonical or used as a `ChannelizerNode` GraphX output port type. | PASS | Aggregate packet/token were removed from packet and utility headers; guardrail test asserts they do not reappear. |
| Tests prove representative ports 0, 1, 62, and 63 are token-wrapped `FHSSChannelizedIqPacket` outputs. | PASS | `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars` statically checks representative port token and sidecar types. |
| Tests prove the corrected `ChannelizerNode` remains a real GraphX node and remains plugin/provider loadable. | PASS | Guardrails accept the corrected sink/source GraphX shape, and `FHSSGraphXNodeTest.EveryNodeIsRegisteredAndDynamicallyLoadable` includes `ChannelizerNode`. |
| PR12 downconverter passthrough and declared frequency-translation behavior remains covered. | PASS | Node tests still cover passthrough sample/timing preservation, declared 8 MHz translation, and invalid frame mismatch rejection. |
| PR13 can instantiate one `PerChannelPulseDetectorNode` per channelizer output port when PR13 is implemented. | PASS | PR13 is not implemented in PR12B, as required. The corrected `ChannelizerNode` exposes 64 token-wrapped channelized IQ output ports, which is the required graph shape for one downstream detector per port. |
| No per-channel pulse detector implementation, graph JSON end-to-end executor wiring, real RF capture, production channelizer claim, Metal/GPU, Doppler/noise behavior, or overlap-aware separation was added. | PASS | Scope grep found only pre-existing docs/tests/non-FHSS GPU files and negative/guardrail references; no PR12B implementation of those future items was added. |

## Verification Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: PASS, target already up to date.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXNodeTest.*' --gtest_brief=1
```

Result: PASS, 9 tests passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXPacketContractTest.*' --gtest_brief=1
```

Result: PASS, 10 tests passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXGuardrailTest.*' --gtest_brief=1
```

Result: PASS, 12 tests passed.

```bash
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*' --gtest_brief=1
```

Result: PASS, 100 tests passed.

```bash
git diff --check
```

Result: PASS, no whitespace errors reported.

## Findings

No blocking findings.

## Final Assessment

PR12B is verified. The channelizer graph shape now satisfies the invariant:
64 configured FHSS frequencies produce 64 GraphX output ports, with output port
index equal to frequency index and channel id.
