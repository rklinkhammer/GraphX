# DSP FHSS Decoder PR7D Verifier Report

PR7D: Split FHSS GraphX Nodes Into Per-Node Files And Plugins

## Verdict

PASS.

PR7D satisfies the required split from the unified FHSS GraphX node header into per-node headers/sources, keeps the PR7A/PR7B token sidecar contracts intact, registers every FHSS GraphX node through the plugin/provider path, and preserves the PR7C guardrails.

## Required Checks

| Check | Result | Evidence |
| --- | --- | --- |
| Each FHSS GraphX node has its own header and source file. | PASS | Verified headers under `libdsp/include/dsp/fhss` and matching sources under `libdsp/src/dsp` for all ten PR7D nodes. `FHSSGraphXGuardrailTest.EachFhssGraphXNodeHasOwnHeaderAndSource` also passes. |
| Unified FHSS GraphX node-definition header/source was deleted and not preserved as a compatibility shim. | PASS | `libdsp/include/dsp/fhss/FHSSGraphXNodes.hpp` and `libdsp/src/dsp/FHSSGraphXNodes.cpp` are absent. `FHSSGraphXGuardrailTest.UnifiedFhssGraphXNodeDefinitionHeaderWasDeleted` passes. |
| Shared FHSS GraphX node utility files define no `...Node` classes. | PASS | `FHSSGraphXNodeUtils.hpp/.cpp` contain token aliases and metadata conversion helpers only. Regex inspection and `FHSSGraphXGuardrailTest.SharedFhssGraphXUtilityDefinesNoNodeClasses` pass. |
| Every FHSS GraphX node remains a real GraphX node and every edge remains `graph::gpu::accel::ControlToken<...>` carrying PR7A packet sidecars. | PASS | Per-node headers inherit `graph::NamedSourceNode`, `graph::NamedInteriorNode`, or `graph::NamedSinkNode`. `FHSSGraphXNodeTest.EveryNodePortUsesAccelControlTokenSidecars` statically validates token-wrapped port types and sidecar packet types. |
| Every FHSS GraphX node is registered with the plugin/provider system. | PASS | `libdsp/plugins/CMakeLists.txt` defines one `add_graphx_plugin` target per FHSS node. Each plugin source uses `PluginGlue` and `NodePluginInstance` for its node type. |
| Dynamic-loading tests prove every FHSS GraphX node can be resolved through the plugin system. | PASS | `FHSSGraphXNodeTest.EveryNodeIsRegisteredAndDynamicallyLoadable` loads all ten plugin libraries, checks registry availability, and creates each node through `RegisteredNodeProvider`. |
| Existing FHSS GraphX node tests include per-node headers, not the deleted unified node-definition header. | PASS | `test_fhss_graphx_nodes.cpp` includes the ten per-node headers. No test includes `dsp/fhss/FHSSGraphXNodes.hpp`; guardrail enforces this. |
| PR7C guardrails still pass. | PASS | `FHSSGraphXGuardrailTest.*` passed. The guardrail suite now covers deleted unified header, per-node file layout, GraphX base inheritance, utility-file boundaries, and deleted pseudo-node API prevention. |
| No graph JSON end-to-end executor wiring, real channelizer, Metal/GPU execution, Doppler/noise behavior, overlap-aware separation, or production RF claim was added. | PASS | Review found PR7D changes limited to FHSS GraphX node file layout, plugins, CMake, tests, and reports. No FHSS graph JSON or GPU/channelizer behavior was added. Existing mentions of noise/Doppler/Metal are pre-existing scope guardrails or unrelated DSP/GPU tests. |

## Validation Commands

```text
cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit
```

Result: pass, no work to do.

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*:FHSSGraphXPacketContractTest.*'
```

Result: 16 tests passed.

```text
./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*'
```

Result: 79 tests passed.

## Notes

- No blocking issues found.
- The working tree also contains prior PR7C/report artifacts; this verification only evaluated PR7D scope.
