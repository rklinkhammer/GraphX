# SAR Implementation Report - PR1 (Verifier Fixes)

1. Files changed.
- examples/SAR/test/test_sar_accel_nodes.cpp
- plan/reviews/SAR_IMPL_PR1.md

2. Files deleted.
- None.

3. Tests added.
- Added combined invariance test in `examples/SAR/test/test_sar_accel_nodes.cpp`:
- `MergeIdentityIsInvariantWhenReadyEventAndHostPointerBothChange`
- This complements the existing separate host_ptr and ready_event invariance tests by mutating both channels together and asserting merge identity remains sidecar-driven.

4. Tests removed or replaced.
- None.

5. Build commands run.
- `cmake --build build-ninja/ninja-debug-metal-native`
- Result: success
- Note: linker emitted a non-fatal duplicate library warning for `libgpu.a` and `libgraph.a`.

6. Test commands run.
- `ctest --test-dir build-ninja/ninja-debug-metal-native --output-on-failure`
- Result: success, 5/5 passed
- Also executed tool test run summary: passed

7. Remaining follow-up items.
- Verifier fix #1 applied: PR1 remains accepted.
- Verifier fix #2 applied: added transport-telemetry clarification note to `plan/reviews/SAR_IMPL_PR1.md`.
- Verifier fix #3 applied (optional): combined host_ptr+ready_event invariance assertion added.
- PR1 acceptance criteria remain satisfied with current build and SAR unit test evidence.