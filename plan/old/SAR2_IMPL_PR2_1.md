# SAR2 Implementation Report: PR2

Date: 2026-06-10
PR: PR2
Title: Freeze Opaque Transport Semantics for `host_ptr` and `ready_event`
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## Overview

Froze transport semantics by documenting that `host_ptr` (in HostPinnedBufferView) and `ready_event` (in DeviceBufferView) are opaque transport infrastructure metadata, NOT used for SAR identity decisions. SAR identity derives exclusively from the sidecar.

## Files Changed

1. **[examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp)**
   - Added comprehensive documentation comment to `AccelControlToken<SidecarT>` template
   - Clarified that sidecar fields carry SAR identity semantics
   - Documented that `device_view.ready_event` and `host_view.host_ptr` are opaque transport metadata
   - Stated the invariant: Code must NOT use these fields for SAR identity decisions

2. **[examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp)**
   - Added PR2 comment: `output.ready_event = 0u;` with note that it's opaque transport metadata

3. **[examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp)**
   - Added PR2 comment: `output.host_ptr = OpaqueHostPointer();` with note that it's opaque transport metadata

4. **[examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp)**
   - Added PR2 comment: `output.ready_event = 0u;` with note that it's opaque transport metadata

5. **[examples/SAR/src/GotchaReplaySourceNode.cpp](examples/SAR/src/GotchaReplaySourceNode.cpp)**
   - Added PR2 comment to both `host_ptr = OpaqueHostPointer();` assignments (data token + EOS token)

6. **[examples/SAR/src/SyntheticApertureIqSourceNode.cpp](examples/SAR/src/SyntheticApertureIqSourceNode.cpp)**
   - Added PR2 comment to both `host_ptr = OpaqueHostPointer();` assignments (data token + EOS token)

7. **[examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp)**
   - Added PR2 comment: `host_ptr = OpaqueHostPointer();` with note that it's opaque transport metadata

8. **[examples/SAR/test/CMakeLists.txt](examples/SAR/test/CMakeLists.txt)**
   - Added new test file: `test_sar_transport_opaque_contract.cpp`

## Files Deleted

None.

## Tests Added

**[examples/SAR/test/test_sar_transport_opaque_contract.cpp](examples/SAR/test/test_sar_transport_opaque_contract.cpp)** (6 test cases):

1. **SidecarCarriesAllSarIdentity**: Validates that sidecar contains all fields needed for SAR identity decisions (sequence_id, batch_id, aperture_id, pulse_range_*, stream_id, tile_id, tile_count, backend_id, backend, marker, synthetic, payload_byte_count, queue IDs).

2. **TransportFieldsAreOpaqueToIdentity**: Creates two tokens with identical sidecars but different transport fields (different host_ptr values, different ready_event IDs) and verifies that identity is determined by sidecar only.

3. **HostPtrIsTransportOnlySentinel**: Demonstrates that host_ptr in HostPinnedBufferView is a transport infrastructure field set by source/transport nodes as a sentinel, and changes to it do not affect sidecar (which carries identity).

4. **ReadyEventIsTransportOnlySentinel**: Demonstrates that ready_event in DeviceBufferView is a transport infrastructure field set by transport nodes as an opaque GPU event ID, and changes to it do not affect sidecar (which carries identity).

5. **IdenticalSidecarsImplyIdenticalSarSemantics**: Proves that two tokens with identical sidecars have identical SAR semantics even when transport fields differ, cementing the contract that transport fields are opaque.

6. **SyntheticTransportPointersAreValid**: Validates that synthetic pointer values and event IDs commonly used by transport nodes (null, sentinel values) are valid and opaque to SAR identity.

## Tests Removed or Replaced

None.

## Build Command Run

```bash
# CMake Tools build
Build_CMakeTools
# Result: success (exit code 0)
# Build included all new test compilation and plugin registration
```

## Test Command Run

```bash
# PR2-specific transport opaque contract tests
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='SarTransportOpaqueContractTest.*'
# Result: PASS (6/6 tests)

# Full SAR unit test suite
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
# Result: PASS (120/120 tests)
# Note: 120 tests = 114 existing + 6 new PR2 tests
```

Test Results Summary:
- New PR2 tests: 6/6 PASS
- Existing SAR tests: 114/114 PASS (no regressions)
- Total: 120/120 PASS

## Acceptance Criteria Verification

### ✅ Documentation and tests consistently treat transport fields as opaque metadata

- **Documentation**: SarMessages.hpp now has clear contract documentation in AccelControlToken template comments stating:
  - SAR identity MUST derive from sidecar fields only
  - device_view.ready_event is opaque to SAR identity
  - host_view.host_ptr is opaque to SAR identity
  - Transport semantics derive from GPU/accel infrastructure, NOT SAR domain logic
- **Code comments**: All node implementations (H2D, D2H, backprojection, source nodes) now have PR2 comments clarifying that ready_event and host_ptr are opaque transport metadata
- **Tests**: 6 new tests explicitly validate that:
  - Sidecar carries all SAR identity information
  - Transport fields don't affect SAR semantics
  - Identical sidecars imply identical SAR behavior regardless of transport fields

### ✅ No regression in existing SAR runtime tests

- All 114 existing SAR tests continue to pass
- Full suite runs: 120/120 PASS
- No existing test failures introduced by PR2 changes

## Design Notes

PR2 freezes the semantic boundary between:
- **SAR Identity & Algorithm Layer** (sidecar): sequence_id, batch_id, aperture_id, pulse_range, stream_id, tile_id, tile_count, backend_id, backend, marker, synthetic, payload_byte_count, queue_ids, merge state, timing counters
- **GPU Transport Infrastructure Layer** (device_view.ready_event, host_view.host_ptr): Opaque synchronization/pointer sentinels set by H2D/D2H nodes, not used for SAR domain decisions

This design ensures:
1. SAR algorithm logic is decoupled from GPU transport implementation details
2. Future transport infrastructure changes (new GPU backends, new synchronization mechanisms) can evolve independently of SAR domain semantics
3. Identity-critical decisions derive only from sidecar, making them testable in isolation from GPU specifics

## Remaining Follow-up Items

None blocking. PR2 is complete and verified.

## Summary

PR2 successfully freezes opaque transport semantics by documenting the semantic boundary between SAR identity (sidecar) and GPU transport infrastructure (ready_event, host_ptr). Both acceptance criteria are satisfied: documentation/tests treat transport fields as opaque, and no regressions occur. All 6 new tests pass; all 114 existing tests pass.
