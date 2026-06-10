# SAR Verifier PR1.1 Report

Role: VERIFIER
Date: 2026-06-09
Scope:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- Implemented PR1 diff

## Verdict

Overall result for requested verification checklist: FAIL.

Notes:
- PR1 roadmap acceptance criteria (type foundation only) is satisfied.
- The stricter verification checklist in this review is not satisfied due to blocking runtime-path issues listed below.

## Blocking Issues

1. Encoded host_ptr identity remains.
- Token is still bit-packed and written into pointer channels in examples/SAR/src/AzimuthTileSplitNode.cpp.
- D2H still reconstructs host pointer from ready_event in examples/SAR/src/D2HAsyncAccelNode.cpp.

2. Encoded ready_event identity remains.
- H2D still copies encoded host_ptr identity into ready_event in examples/SAR/src/H2DAsyncAccelNode.cpp.
- Backprojection still forwards ready_event as identity in examples/SAR/src/SarBackprojectionTransformAccelNode.cpp.

3. Global sidecar store remains primary correlation path.
- Global map and mutation API remain in examples/SAR/src/SarAccelTokenSidecarStore.cpp and examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp.
- Split/H2D/kernel/D2H/merge still write/read sidecar store in:
  - examples/SAR/src/AzimuthTileSplitNode.cpp
  - examples/SAR/src/H2DAsyncAccelNode.cpp
  - examples/SAR/src/SarBackprojectionTransformAccelNode.cpp
  - examples/SAR/src/D2HAsyncAccelNode.cpp
  - examples/SAR/src/ImageTileMergeNode.cpp

4. SAR sidecar is defined explicitly but not carried explicitly through runtime node interfaces.
- Canonical types exist in examples/SAR/include/sar/SarMessages.hpp.
- Runtime signatures still use HostPinnedBufferView/DeviceBufferView in:
  - examples/SAR/include/sar/H2DAsyncAccelNode.hpp
  - examples/SAR/include/sar/D2HAsyncAccelNode.hpp
  - examples/SAR/include/sar/SarBackprojectionTransformAccelNode.hpp

## Non-Blocking Issues

1. Generic GPU nodes remain SAR-unaware.
- No SAR include/namespace coupling found in libgpu sources/headers.
- libgraph SAR references are parser guardrails (expected), e.g. legacy payload contract checks.

2. Sidecar-preservation test coverage exists but is tied to current encoded/store path.
- Structural token/sidecar tests were added in examples/SAR/test/test_sar_token_contract.cpp.
- Existing flow tests in examples/SAR/test/test_sar_accel_nodes.cpp validate metadata continuity under current token encoding + sidecar store behavior.

3. Deleted tests review.
- No tests were deleted in implemented PR1 diff, so there are no deletions to classify as obsolete.

## Credibility of Build/Test Results

1. Build status: credible.
- Build_CMakeTools completed successfully (result code 0).

2. Test status: credible for current branch behavior.
- RunCtest_CMakeTools reported 5/5 passing targets, including sar_example_unit.
- This confirms compile/test health, not completion of PR2/PR3 cleanup requirements.

## Suggested Fixes

1. Keep PR1 scope strict and document expectations.
- Treat PR1 as type-foundation only.
- Verify removal of encoded identity and sidecar-store primary path in PR2.

2. PR2 implementation fixes.
- Remove host_ptr/ready_event identity transport.
- Remove sidecar global store files and all runtime references.
- Add negative tests asserting identity is not reconstructed from pointer/event channels.

3. PR3 implementation fixes.
- Migrate runtime node interfaces to explicit SarAccelControlToken input/output.
- Add end-to-end sidecar continuity tests on explicit token contract.

## Final Pass/Fail Summary

- No encoded host_ptr identity remains: FAIL
- No encoded ready_event identity remains: FAIL
- No global sidecar store remains as primary path: FAIL
- SAR sidecar is carried explicitly: FAIL (defined only; not runtime-carried yet)
- Generic GPU nodes remain SAR-unaware: PASS
- Tests cover sidecar preservation: PARTIAL (via current path, not explicit token-carried path)
- Deleted tests were obsolete: N/A (none deleted)
- Build and test results are credible: PASS
