# SAR PR1 Verifier Report 2

Role: VERIFIER
Date: 2026-06-09
Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- Implemented PR1 diff

## Pass/Fail

- Overall for the requested check list: FAIL.
- PR1 roadmap acceptance criteria only (type foundation + test + build): PASS.

## Blocking Issues

1. Encoded host_ptr identity remains in active SAR runtime flow.
- Token packing and pointer-channel encoding still present in examples/SAR/src/AzimuthTileSplitNode.cpp.
- D2H still reconstructs pointer identity from ready_event in examples/SAR/src/D2HAsyncAccelNode.cpp.

2. Encoded ready_event identity remains in active SAR runtime flow.
- H2D still derives ready_event from host_ptr in examples/SAR/src/H2DAsyncAccelNode.cpp.
- Backprojection still propagates ready_event identity in examples/SAR/src/SarBackprojectionTransformAccelNode.cpp.

3. Global sidecar store remains primary correlation path.
- Runtime nodes still include and use sidecar store API in:
  - examples/SAR/src/AzimuthTileSplitNode.cpp
  - examples/SAR/src/H2DAsyncAccelNode.cpp
  - examples/SAR/src/SarBackprojectionTransformAccelNode.cpp
  - examples/SAR/src/D2HAsyncAccelNode.cpp
  - examples/SAR/src/ImageTileMergeNode.cpp

4. SAR sidecar is defined explicitly, but not carried explicitly through runtime node interfaces.
- Canonical type foundation is present in examples/SAR/include/sar/SarMessages.hpp.
- Runtime node contracts are still accel view in/out, not token in/out:
  - examples/SAR/include/sar/H2DAsyncAccelNode.hpp
  - examples/SAR/include/sar/D2HAsyncAccelNode.hpp
  - examples/SAR/include/sar/SarBackprojectionTransformAccelNode.hpp

## Non-Blocking Issues

1. Generic GPU nodes remain SAR-unaware.
- No SAR coupling found in libgpu include/src grep sweep.

2. Tests cover sidecar preservation, but through the current encoded/store path.
- Structural token tests added in examples/SAR/test/test_sar_token_contract.cpp.
- End-to-end preservation test exists in examples/SAR/test/test_sar_accel_nodes.cpp, but still exercises the existing encoded identity + sidecar-store runtime model.

3. Deleted tests obsolete check: N/A.
- No tests were removed in the implemented PR1 diff.

4. Build and test credibility: PASS.
- Fresh build succeeded (no work needed).
- Fresh full CTest succeeded 5/5, including sar_example_unit.

## Suggested Fixes

1. Keep PR1 judged as foundation-only and mark the stricter runtime findings as deferred to PR2/PR3.
2. PR2: remove host_ptr and ready_event identity transport and eliminate sidecar global-store runtime dependency.
3. PR2: add negative tests proving identity is not reconstructed from pointer/event channels.
4. PR3: migrate stage interfaces to explicit token carriage and validate sidecar continuity through token-based I/O.
