# SAR Implementation Report - PR1

1. Files changed.
- examples/SAR/test/test_sar_accel_nodes.cpp

2. Files deleted.
- None.

3. Tests added.
- Added negative test: SplitDoesNotEncodeIdentityIntoHostPointerChannel
  - Verifies host pointer channel is opaque/constant and not encoding identity.
- Added negative test: D2HPreservesSidecarIdentityWhenReadyEventChanges
  - Verifies sidecar identity fields are unchanged when ready_event is mutated.

4. Tests removed or replaced.
- None.

5. Build commands run.
- Build_CMakeTools (default target)
- Result: success, no pending rebuild work (ninja reported no work to do).

6. Test commands run.
- RunCtest_CMakeTools
- Result: success
- Summary:
  - libgraph_unit passed
  - libgraph_integration passed
  - libgpu_stub_unit passed
  - libgpu_metal_runtime passed
  - sar_example_unit passed
  - 100% tests passed (5/5)

7. Remaining follow-up items.
- PR1 scope requested is implemented via explicit invariance + negative tests.
- No additional PR1 blockers found from this change set.
- Clarification: `host_ptr` and `ready_event` are treated as transport telemetry/control channels only, not SAR identity channels.
- Future work remains in later roadmap PRs (token boundary cleanup, legacy abstraction deletion, resolver hardening), not touched here.