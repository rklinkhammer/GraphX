# DSP FHSS Decoder PR8 Verifier Report

PR8: End-To-End Graph JSON, Executor Test, And Minimal Diagnostics

## Verdict

PASS.

PR8 satisfies the required end-to-end GraphX JSON/runtime scope. The deterministic FHSS CPSM fixture graph loads dynamically through the PR7D plugin/provider path, runs to completion through `GraphExecutorBuilder`, preserves complex IQ evidence through word recovery, locks on the hop-only preamble, assembles the message, compares decoded pulses against truth, and emits the required minimum diagnostics.

## Required Checks

- PASS: FHSS graph config loads nodes through the PR7D plugin/provider path.
  - Verified `FHSSGraphXExecutorTest.JsonTopologyRunsThroughGraphExecutorBuilderAndEmitsDiagnostics` uses `NodeProviderBootstrap`, `WithPluginDirectory`, and `GraphExecutorBuilder`.
- PASS: FHSS graph config uses only real GraphX FHSS nodes and PR7A edge packet/contract types.
  - Verified config node types are the ten PR7D FHSS GraphX nodes.
  - Verified node-port contract tests still assert `graph::gpu::accel::ControlToken<...>` sidecars.
- PASS: No deleted pre-GraphX pseudo-node helper or deleted unified FHSS node-definition header is referenced by config, plugins, tests, or runtime wiring.
  - Verified PR7C/PR7D guardrail tests still pass.
  - No `FHSSGraphXNodes.hpp` reference was found in the PR8 config/runtime test path.
- PASS: Executor runs the full deterministic fixture to completion.
  - Verified `executor->Execute()` succeeds and `executor->IsCompletionSignaled()` is true.
- PASS: Synthetic IQ flows through detector, merge, CPSM branch metrics, Viterbi, pulse-word decode, preamble lock, message assembly, and truth comparison.
  - Verified JSON edge chain covers the complete graph from source to sink.
  - Verified executor test compares final decoded output against deterministic fixture truth.
- PASS: Decoded pulses match truth for start, duration, frequency index, and value.
  - Verified executor test checks every decoded pulse against generated fixture truth.
- PASS: Assembled message locks on hop-only preamble and validates the four-frequency active set.
  - Verified diagnostics report `preamble_lock = true` and active set `{1, 7, 12, 62}`.
- PASS: Diagnostics contain all required minimum fields from the roadmap.
  - Verified diagnostics include `pulse_count`, `rejected_count`, `global_start_sample`, `frequency_index`, `confidence`, `viterbi_path_metric`, `decoded_value`, `preamble_lock`, `truth_mismatch_count`, RF metadata frequency, IQ offset frequency, sample-time mapping, synchronization assumption, unsupported-overlap rejection, and unsupported-impairment rejection.
- PASS: Graph is CPU-only and does not use Metal/GPU nodes.
  - Verified PR8 config uses only FHSS CPU-lane node types and no Metal/GPU graph nodes.
- PASS: Complex IQ evidence is preserved through word recovery.
  - Verified branch metric and Viterbi nodes consume `FHSSGraphXComplexEvidence` host complex samples, and packet contract tests cover complex evidence ownership/range.
- PASS: No real RF capture, external dataset, real channelizer topology, Doppler/noise behavior, production RF claim, or canonical PDW diagnostic path was added.
  - Verified config keeps noise/Doppler/multipath disabled and uses the correlator-bank path, not a real channelizer or external data path.

## Validation Run

- Build:
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_libgraph_unit`
  - Result: no work to do, target current.
- Focused PR8/GraphX tests:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='FHSSGraphXExecutorTest.*:FHSSGraphXNodeTest.*:FHSSGraphXPacketContractTest.*:FHSSGraphXGuardrailTest.*'`
  - Result: 18 passed.
- Broad FHSS/CPSM regression:
  - `./build-ninja/ninja-debug-metal-native/libgraph/test/test_libgraph_unit --gtest_filter='*FHSS*:*CPSM*'`
  - Result: 81 passed.

## Findings

No blocking findings.

## Residual Risk

PR8 intentionally remains a deterministic CPU fixture. Pulse-start acquisition, real channelizer topology, noisy/Doppler impairments, Metal/GPU execution, production RF claims, and optional PDW-style diagnostics remain future work and were not introduced by this PR.
