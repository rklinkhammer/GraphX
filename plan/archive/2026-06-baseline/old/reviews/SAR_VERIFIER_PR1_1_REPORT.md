# SAR PR1 Verifier Report (PR1 Acceptance)

Role spec: [plan/agents/GRAPHX_SAR_AGENT_ROLES.md](plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

Inputs reviewed:
- [plan/reviews/SAR_INSPECTOR_REPORT.md](plan/reviews/SAR_INSPECTOR_REPORT.md)
- [plan/reviews/SAR_SIMPLIFIER_REPORT.md](plan/reviews/SAR_SIMPLIFIER_REPORT.md)
- [plan/reviews/SAR_PR_ROADMAP.md](plan/reviews/SAR_PR_ROADMAP.md)
- Implemented PR1 commit: `682b072` (Arch Review PR1 snapshot)

## Verdict

FAIL

## Blocking issues

1. Encoded host_ptr identity transport still exists in SAR runtime path.
- Token identity is explicitly encoded and then copied into host_ptr in split stage: [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L19), [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L60).
- D2H still assigns synthetic identity-like host_ptr values: [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L49).
- This violates the acceptance check "No encoded host_ptr identity remains." even though downstream merge logic now reads sidecar directly.

## Non-blocking issues

1. ready_event fields are still sequence-derived, but not used as SAR identity in merge/diagnostics.
- H2D assignment and ticket completion event linkage: [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp#L65), [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp#L82).
- Backprojection assignment: [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp#L214).
- Current usage appears transport/runtime-event semantics rather than SAR identity transport; this is likely acceptable if criterion is interpreted as "no SAR identity in ready_event".

2. Legacy SAR message structs still exist alongside canonical token definitions.
- Canonical types are present: [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L85), [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L104), [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L120).
- Legacy structs remain in same file after canonical types (not a PR1 blocker by current scope, but still mixed model surface).

3. "Deleted tests were obsolete" is not strongly applicable in this PR.
- In commit `682b072`, SAR tests were primarily updated and one new token contract test was added; no clear obsolete SAR test deletions were observed.

## Acceptance checks summary

1. No encoded host_ptr identity remains.
- FAIL (see blocking issue #1).

2. No encoded ready_event identity remains.
- PASS with caveat (event fields remain sequence-derived but no direct SAR identity decode path found).

3. No global sidecar store remains as primary path.
- PASS.
- Sidecar store files were deleted in PR1 commit (`D`):
  - examples/SAR/include/sar/SarAccelTokenSidecarStore.hpp
  - examples/SAR/src/SarAccelTokenSidecarStore.cpp
- No current references to SarAccelTokenSidecarStore in SAR CMake/test/plugin lists.

4. SAR sidecar is carried explicitly.
- PASS.
- Explicit sidecar/token model in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L85).
- Merge consumes sidecar directly: [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp#L31).

5. Generic GPU nodes remain SAR-unaware.
- PASS.
- Search across `libgpu` and `libgraph` found no references to SAR token/sidecar symbols.

6. Tests cover sidecar preservation.
- PASS.
- Unit preservation test: [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L272).
- End-to-end runtime preservation test: [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp#L412).

7. Deleted tests were obsolete.
- INCONCLUSIVE / N/A for this commit (no clear obsolete SAR test deletions in `682b072`).

8. Build and test results are credible.
- PASS.
- Fresh verifier run:
  - Build_CMakeTools: result code 0.
  - RunCtest_CMakeTools: 5/5 passed (libgraph_unit, libgraph_integration, libgpu_stub_unit, libgpu_metal_runtime, sar_example_unit).

## Suggested fixes

1. Remove host_ptr identity encoding entirely from SAR runtime path.
- In [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L19), stop deriving token identity bits from SAR envelope fields for host_ptr transport.
- In [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L60) and [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L49), treat host_ptr as opaque non-identity placeholder only.
- Ensure no runtime logic relies on host_ptr-derived values.

2. Tighten PR1 tests to explicitly enforce the prohibition.
- Add assertions that changing host_ptr does not alter reconstructed SAR identity/diagnostics output when sidecar is unchanged.
- Add assertions that no node decodes SAR envelope identity from host_ptr.

3. Optional hardening for ready_event criterion clarity.
- Add tests that SAR identity fields are invariant to ready_event values.
- Keep ready_event as backend/event bookkeeping only.
