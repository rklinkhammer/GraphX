# DSP FHSS Decoder PR7C Implementer Report

## PR

PR7C: Remove Pre-GraphX FHSS Node Scaffolding And Guard Against Regression

## Scope Implemented

- Added source-level guardrail tests preventing public FHSS pseudo-node scaffolding from returning.
- Verified that public FHSS `...Node` classes exist only in `FHSSGraphXNodes.hpp`.
- Verified that every public FHSS `...Node` class in `FHSSGraphXNodes.hpp` inherits a repository GraphX node base:
  - `graph::NamedSourceNode`
  - `graph::NamedInteriorNode`
  - `graph::NamedSinkNode`
- Verified that FHSS tests do not call deleted pre-GraphX pseudo-node static APIs.
- Verified that FHSS tests do not include deleted pre-GraphX `*Node*.hpp` headers.
- Kept existing GraphX node tests passing.

## Files Added

- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- `plan/reviews/DSP_FHSS_DECODER_IMPL_PR7C.md`

## Files Deleted

- None. PR7B had already removed the old public pseudo-node class names and direct old pseudo-node API calls. PR7C adds regression guardrails around that state.

## Guardrails Added

- `FHSSGraphXGuardrailTest.PublicFhssNodeClassesExistOnlyInGraphXNodeHeader`
  - Scans `libdsp/include/dsp/fhss/*.hpp`.
  - Fails if any FHSS public `...Node` class appears outside `FHSSGraphXNodes.hpp`.
- `FHSSGraphXGuardrailTest.FhssNodeClassesInheritGraphXNodeBases`
  - Scans `FHSSGraphXNodes.hpp`.
  - Fails if a public FHSS `...Node` class does not inherit a GraphX node base.
- `FHSSGraphXGuardrailTest.FhssTestsDoNotCallDeletedPseudoNodeApis`
  - Scans `libgraph/test/unit/test_fhss_*.cpp`.
  - Fails if tests call deleted pseudo-node static APIs such as `...Node::Detect`, `...Node::Merge`, `...Node::Decode`, `...Node::Assemble`, `...Node::Diagnostics`, `...Node::ScoreBranch`, or `...Node::Compute`.
  - Fails if tests include deleted pre-GraphX `*Node*.hpp` headers.

## Guardrails

- Did not preserve backward compatibility for old pseudo-node APIs.
- Did not add graph JSON end-to-end executor wiring.
- Did not add a real channelizer.
- Did not add Metal/GPU execution.
- Did not add Doppler/noise behavior.
- Did not add overlap-aware separation.
- Did not make production RF claims.

## Validation

- `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Passed.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSGraphXGuardrailTest.*:FHSSGraphXNodeTest.*'`
  - Passed: 6 tests.
- `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=FHSSProtocolTest.*:FHSSSyntheticIqGeneratorTest.*:FHSSPulseMergeTest.*:FHSSCorrelatorBankDetectorTest.*:FHSSCpsmDecoderTest.*:FHSSPulseWordDecoderTest.*:FHSSMessageAssemblyTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXNodeTest.*:FHSSGraphXGuardrailTest.*'`
  - Passed: 75 tests.
- `ctest --test-dir build-ninja/ninja-debug-metal-native -R '^libgraph_unit$' --output-on-failure`
  - Passed: 1/1 test, 81.85 sec.

## Notes For PR8

PR8 should wire only real FHSS GraphX nodes and PR7A/PR7B tokenized edge contracts through graph JSON/plugin/provider execution. The PR7C guardrails intentionally do not add graph JSON or plugin wiring.
