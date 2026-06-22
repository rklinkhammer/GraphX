# SAR CRSD To Focused Image VERIFIER Report - PR3

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR3 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Verdict: **FAIL**

PR3 introduces the adapter/model scaffolding and passes its focused unit suite, but multiple required verifier checks are not fully satisfied, primarily around contract completeness and negative-proof test coverage.

## Verification Commands Executed

- Build (already green in this workspace before verifier pass):
  - `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`
- Verifier-focused adapter tests:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*'`

Observed result:
- `6` tests run, `6` passed.

## Required Checks Matrix

1. CrsdApertureAssemblyAdapterNode emits assembled full-aperture SAR phase-history messages/tokens.
- Status: **PASS**
- Evidence:
  - examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp emits one `SarPhaseHistoryControlMessage` on EOS via `BuildApertureMessage`.
  - examples/SAR/test/test_crsd_aperture_assembly_adapter_node.cpp validates EOS assembly (`AssemblesFullApertureFrameOnEndOfStream`).

2. Required geometry and sampling fields are present and validated.
- Status: **PARTIAL / FAIL**
- Evidence:
  - Present in model: `rcv_time_s`, `platform_position_m`, `platform_velocity_mps`, `samples_per_vector`, `carrier_hz`, `sample_rate_hz` in examples/SAR/include/sar/SarPhaseHistoryModel.hpp.
  - Validation implemented only for sample/frequency consistency at configure time in examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp (`ValidateAssemblyConsistency`).
  - No explicit configure/runtime validation for geometry-field presence/continuity.

3. Total output vector count equals sum of segment vectors.
- Status: **PARTIAL / FAIL**
- Evidence:
  - Adapter sets `frame.total_vector_count = read_result_.value.total_vector_count` and copies vectors, but does not assert equality against computed sum of emitted segment vectors.
  - Existing tests assert expected fixture totals, but no invariant test explicitly checks `sum(segment.vector_count) == frame.total_vector_count` for adapter output.

4. Sample/channel/frequency consistency is enforced across segments.
- Status: **FAIL**
- Evidence:
  - Enforced: samples and frequency (`samples_per_vector`, `carrier_hz`, `sample_rate_hz`) in `ValidateAssemblyConsistency`.
  - Missing: channel consistency contract/field validation for segments (no channel identifier in examples/SAR/include/sar/io/CrsdReader.hpp or examples/SAR/include/sar/SarPhaseHistoryModel.hpp).

5. Tests cover segment ordering, full-aperture accounting, metadata/PVP mapping, EOS/control-marker propagation, ownership/layout, checksums, vector/channel/sample ordering, and SarAccelControlToken preservation.
- Status: **PARTIAL / FAIL**
- Evidence:
  - Covered:
    - ordering/diagnostics: out-of-order/missing/duplicate/unexpected segment tests.
    - EOS/control marker: asserted in `AssemblesFullApertureFrameOnEndOfStream`.
    - layout basics: rank/shape assertions.
    - checksum boundary field linkage: split boundary input hash equals ordered set hash.
  - Not sufficiently covered:
    - metadata/PVP mapping assertions are not deep (vector geometry/timing mapping not explicitly asserted field-by-field in adapter tests).
    - ownership/sample format invariants are defined in model but not asserted in tests.
    - vector/channel/sample ordering coverage is incomplete (no channel-order tests; no explicit per-vector sample-order proof).
    - SarAccelControlToken preservation coverage is incomplete (test checks marker; no comprehensive preservation assertion across sidecar/token fields).

6. Tests fail if payload data is dropped, sidecars are used as physics inputs, or each segment is treated as a separate final image.
- Status: **PARTIAL / FAIL**
- Evidence:
  - Sidecar physics misuse guard is partially covered only as optional pulse-range cross-check behavior.
  - No explicit negative test proving payload-drop failure (e.g., corrupted/empty vector payload accepted vs rejected at adapter boundary).
  - Per-segment final-image behavior is implicitly avoided by adapter design (no output on data tokens, one output on EOS), but there is no explicit guardrail test asserting rejection/failure of per-segment-finalization semantics.

7. Split/merge partition metadata contract is defined for PR4.
- Status: **PARTIAL / FAIL**
- Evidence:
  - Model includes split-boundary hashes (`split_boundary_input_hash`, `split_boundary_output_hash`) in examples/SAR/include/sar/SarPhaseHistoryModel.hpp.
  - Missing explicit partition metadata contract elements (e.g., partition id/count/range ordering keys) expected to drive deterministic PR4 split/merge semantics.

8. No focused-image transform, Metal lane, sink, SarPy reference, local real-data workflow, or MATLAB dependency was added.
- Status: **PASS**
- Evidence:
  - Changed files are scoped to adapter/model/reader extension/plugin/test wiring and report.
  - No new focused-image transform node, no new sink node, no new Metal lane wiring, no new SarPy/MATLAB dependency introduced by PR3 changes.

## File-Level Evidence (PR3 Changes)

- examples/SAR/include/sar/SarPhaseHistoryModel.hpp
- examples/SAR/include/sar/CrsdApertureAssemblyAdapterNode.hpp
- examples/SAR/src/CrsdApertureAssemblyAdapterNode.cpp
- examples/SAR/plugins/crsd_aperture_assembly_adapter_node_plugin.cpp
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/test/test_crsd_aperture_assembly_adapter_node.cpp
- examples/SAR/include/sar/io/CrsdReader.hpp
- examples/SAR/src/io/CrsdReader.cpp
- examples/SAR/test/CMakeLists.txt

## Summary

PR3 is directionally correct and functionally compiles/tests for the introduced adapter path, but it does **not** fully satisfy all required verifier checks as written. Specifically, channel consistency contract, split/merge partition metadata contract depth, and multiple negative-proof test requirements remain incomplete.
