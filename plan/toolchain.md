# Toolchain And Linux Follow-Up Plan

## Scope And Success Criteria

Current state:

- macOS host support is implemented.
- Linux remains the main place where build-system consistency needs to be tightened.
- Existing public preset names should stay stable wherever possible.

Scope for the remaining work:

1. Make Linux compiler selection explicit and deterministic through toolchain files.
2. Keep Linux and macOS preset inheritance aligned so CI can select the right host path cleanly.
3. Group host-specific tests so Linux CI does not execute irrelevant macOS-only suites.
4. Provide a fallback result of not relevant (skip), not hard failure, for host-mismatched tests.
5. Preserve existing developer-facing preset names where practical.

Success means:

1. Linux configure works with explicit compiler toolchain presets and no compiler ambiguity.
2. CI presets select host-appropriate tests without cross-host noise.
3. Full `ctest` on Linux has no failures from irrelevant host-only suites.
4. macOS remains supported without requiring additional build-system changes.

## Phase 1: Linux Toolchain Finalization

Target files to update:

1. `cmake/toolchains/graphx-host.cmake`
2. `CMakePresets.json`
3. Linux-specific toolchain files under `cmake/toolchains/`

Tasks:

1. Keep the host-common base slim:
- Keep Linux/macOS host checks and common cache defaults in the shared host layer.
- Avoid reintroducing compiler probing in shared logic.

2. Keep Linux compiler toolchains explicit:
- One file per Linux compiler, for example:
  - gcc Linux
  - clang Linux
- Each Linux compiler file should set:
  - `CMAKE_C_COMPILER`
  - `CMAKE_CXX_COMPILER`
  - `GRAPHX_CXX_STANDARD_FLAG`

3. Add guardrails in each Linux compiler toolchain:
- Fatal error if the host is incompatible with that compiler preset.
- Keep compiler toolchain deterministic, with no candidate probing.

Deliverable:

1. A Linux toolchain directory with a shared host base plus compiler-specific files, each single-compiler only.

## Phase 2: Preset Graph Migration

Target file:

1. `CMakePresets.json`

Tasks:

1. Keep existing public presets:
- Keep names like `ninja-debug`, `ninja-debug-linux-host`, `ninja-debug-metal-native`.
- Keep the macOS presets that already exist.

2. Introduce hidden compiler base presets for Linux:
- Each hidden Linux preset points to exactly one Linux compiler toolchain file.
- Existing Linux public presets inherit from compiler base + feature profile.

3. Keep CI presets explicit:
- Ensure `ninja-ci-linux` inherits host-correct Linux compiler paths.
- Keep the existing macOS CI preset as the reference counterpart.

4. Add at least one default compiler mapping for Linux:
- Linux default development preset maps to one Linux compiler preset.

Deliverable:

1. Compiler-explicit preset inheritance for Linux with no combined compiler toolchain logic.

## Phase 3: Reduce Compiler Policy Duplication In Top-Level CMake

Target file:

1. `CMakeLists.txt`

Tasks:

1. Convert top-level compiler flag inference to validation-first for Linux:
- Prefer validating that `GRAPHX_CXX_STANDARD_FLAG` is already set by the Linux toolchain.
- Keep a minimal fallback only for deliberate manual configure paths.

2. Keep host feature gating unchanged for now:
- Metal and runtime checks remain as-is during this phase.

Deliverable:

1. Toolchain is the source of truth for Linux compiler selection and language flag.

## Phase 4: Host-Specific Test Grouping

Target files:

1. `libgraph/test/CMakeLists.txt`
2. `libdsp/test/CMakeLists.txt`
3. `libgpu/test/CMakeLists.txt`
4. `examples/SAR/test/CMakeLists.txt`

Tasks:

1. Introduce a host label taxonomy:
- `host-any`
- `host-linux`
- `host-macos`
- `host-local-only`

2. Apply labels consistently:
- Add `host-any` to baseline suites in libgraph/libdsp.
- Add `host-linux` to Linux-specific suites and keep `host-macos` where already required.
- Keep local-only SAR suites labeled `local-only` and add host scope where needed.

3. Ensure all `add_test` calls get explicit labels:
- Fill gaps where tests currently have `add_test` but no `set_tests_properties` labels.

Deliverable:

1. Every test has host scope labels plus functional labels.

## Phase 5: Not Relevant Fallback (Skip) Behavior

Target files:

1. `libgpu/test/CMakeLists.txt`
2. `examples/SAR/test/CMakeLists.txt`

Tasks:

1. Add a shared host-check test wrapper helper (CMake module/script).
2. For host-mismatched suites:
- Return skip result (not failure).
- Set `SKIP_RETURN_CODE` so CTest records skipped, not failed.
3. Keep hard `DISABLED` only for permanently disabled lanes.

Deliverable:

1. Irrelevant host tests report skipped or not relevant cleanly.

## Phase 6: Test Preset Filters Updated To Labels

Target file:

1. `CMakePresets.json`

Tasks:

1. Update CI test presets to include host label filtering:
- `test-ci-linux` includes `host-linux` or `host-any`.
- `test-ci-macos` includes `host-macos` or `host-any`.
2. Keep name-based includes only where they serve lane intent.
3. Ensure SAR SarPy lane uses label combinations that avoid accidental cross-host runs.

Deliverable:

1. Host-accurate `ctest` preset behavior without brittle regex-only filtering.

## Execution Sequence (Recommended Commit Plan)

1. Commit 1: Linux toolchain foundation.
2. Commit 2: Linux preset inheritance migration.
3. Commit 3: Top-level CMake compiler policy cleanup.
4. Commit 4: Host test labels across test CMake files.
5. Commit 5: Not relevant skip wrapper and host-gated execution.
6. Commit 6: Test preset label-filter migration and README updates.

## Validation Matrix

Run these after each major phase:

1. `cmake --preset ninja-debug`
2. `cmake --build --preset build-debug`
3. `ctest --preset test-ci-linux --output-on-failure` on Linux
4. `ctest --preset test-ci-macos --output-on-failure` on macOS
5. `ctest --test-dir build-ninja/ninja-debug --output-on-failure`
6. `ctest -N` with each CI preset to verify selection shape

Acceptance checks:

1. No compiler ambiguity in configure logs.
2. No host-mismatch failures from Linux-only or local-only lanes.
3. Host-mismatch suites show skipped or are excluded by label as designed.

## Risk Controls

1. Keep public preset names stable to minimize developer disruption.
2. Add migration notes in README for new Linux compiler-specific presets.
3. Keep one fallback manual configure path only if needed for ad-hoc debugging.
4. Land in small commits so regressions are easy to isolate and revert.
