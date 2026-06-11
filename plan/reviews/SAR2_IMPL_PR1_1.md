# SAR2 Implementation Report: PR1

Date: 2026-06-10
PR: PR1
Title: Retire Unused SAR Transport Helper Structs
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## Overview

Removed three dormant SAR transport helper struct types from the canonical message model to reduce ambiguity and surface area.

## Files Changed

1. **[examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp)**
   - Removed: `SarMessageEnvelope` struct (lines 49–62)
   - Removed: `SarBufferDescriptor` struct (lines 64–70)
   - Removed: `SarGpuMetadata` struct (lines 72–82)
   - Kept: canonical `AccelControlToken<SidecarT>`, `SarAccelControlToken` alias, `SarSidecar`, `SarStageTimingMetrics`, `SarDiagnosticsSnapshot`

2. **[examples/SAR/test/test_sar_token_contract.cpp](examples/SAR/test/test_sar_token_contract.cpp)**
   - Added explicit canonical alias assertion in `WrapperAliasesUseCanonicalTokenType` test:
     - `EXPECT_TRUE((std::is_same_v<sar::SarAccelControlToken, sar::AccelControlToken<sar::SarSidecar>>));`

## Files Deleted

None.

## Tests Added

- Strengthened existing test suite with explicit canonical alias verification in [examples/SAR/test/test_sar_token_contract.cpp](examples/SAR/test/test_sar_token_contract.cpp)

## Tests Removed or Replaced

None.

## Build Command Run

```bash
# CMake Tools build
Build_CMakeTools
# Result: success (exit code 0)
```

## Test Command Run

```bash
# Full SAR unit test suite
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit
# Result: 113/114 tests passed (see below for non-blocking flaky test)
```

Test coverage:
- Focused token contract tests: PASS (3/3)
- All SAR unit tests: 113 PASS, 1 FAIL (flaky, unrelated to PR1)

## Acceptance Criteria Verification

### ✅ No in-repo references to retired struct names remain
- Code search: zero matches in examples/SAR, libgpu, libgraph for SarMessageEnvelope, SarBufferDescriptor, SarGpuMetadata
- Documentation/plan references exist but are historical context, not active code paths

### ✅ Canonical token contract remains unchanged and test-backed
- `AccelControlToken<SarSidecar>` alias is unchanged
- `SarAccelControlToken` alias is unchanged
- `SarSidecar` definition unchanged
- New explicit canonical alias assertion in `WrapperAliasesUseCanonicalTokenType` test covers the contract

## Remaining Follow-up Items

1. **SarPr2FanoutJsonTest.ExecutesGraphVisibleFanoutTopology**: Intermittent test failure unrelated to PR1 scope. Test passes when run in isolation, suggests flaky timing dependency. Not a blocking issue for PR1 acceptance.
2. RRP7 fixture-path macro was missing and was added in separate scoped fix (SAR_RRP7_TINY_GOTCHA_FIXTURE_PATH in CMakeLists.txt).

## Summary

PR1 successfully retires three dormant helper types and strengthens the canonical token contract coverage. Acceptance criteria met. Full suite is stable with one known flaky test unrelated to PR1 changes.
