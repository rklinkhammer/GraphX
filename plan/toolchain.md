# Toolchain And Host-Test Implementation Plan

## Scope And Success Criteria

1. Split compiler selection into separate toolchain files, one compiler per file.
2. Keep host policy shared, but remove multi-compiler selection logic from the shared toolchain.
3. Group host-specific tests so they do not run on irrelevant hosts.
4. Provide a fallback result of not relevant (skip), not hard failure, for host-mismatched tests.
5. Preserve existing developer-facing preset names where practical.

Success means:

1. Configure works with explicit compiler toolchain presets on macOS and Linux.
2. CI presets select host-appropriate tests without cross-host noise.
3. Full ctest on each host has no failures from irrelevant host-only suites.

## Phase 1: Toolchain Split (No Behavior Change Yet)

Target files to update:

1. `cmake/toolchains/graphx-host.cmake`
2. `CMakePresets.json`

Tasks:

1. Refactor shared host toolchain:
- Keep host checks and CMAKE_SYSTEM_NAME policy from `cmake/toolchains/graphx-host.cmake`.
- Remove compiler candidate lists and auto-selection from `cmake/toolchains/graphx-host.cmake`.
- Keep only host-common cache defaults.

2. Add separate compiler toolchain files:
- One file per compiler, for example:
  - gcc Linux
  - clang Linux
  - appleclang macOS
  - optional IntelLLVM Linux
- Each compiler file includes shared host toolchain and sets:
  - `CMAKE_C_COMPILER`
  - `CMAKE_CXX_COMPILER`
  - `GRAPHX_CXX_STANDARD_FLAG`

3. Add guardrails in each compiler toolchain:
- Fatal error if host is incompatible with that compiler preset.
- Keep compiler toolchain deterministic, no candidate probing.

Deliverable:

1. A toolchain directory with shared host base plus compiler-specific files, each single-compiler only.

## Phase 2: Preset Graph Migration

Target file:

1. `CMakePresets.json`

Tasks:

1. Keep existing public presets:
- Keep names like `ninja-debug`, `ninja-debug-linux-host`, `ninja-debug-metal-native`.

2. Introduce hidden compiler base presets:
- Each hidden preset points to exactly one compiler toolchain file.
- Existing public presets inherit from compiler base + feature profile.

3. Keep CI presets explicit:
- Ensure `ninja-ci-linux` and `ninja-ci-macos` inherit host-correct compiler paths.

4. Add at least one default compiler mapping per host:
- Linux default development preset maps to one Linux compiler preset.
- macOS default development preset maps to appleclang preset.

Deliverable:

1. Compiler-explicit preset inheritance with no combined compiler toolchain logic.

## Phase 3: Reduce Compiler Policy Duplication In Top-Level CMake

Target file:

1. `CMakeLists.txt`

Tasks:

1. Convert top-level compiler flag inference to validation-only:
- Replace inference with a check that `GRAPHX_CXX_STANDARD_FLAG` is already set by toolchain.
- Keep a minimal fallback only if intentionally supporting non-preset manual configure.

2. Keep host feature gating unchanged for now:
- Metal and runtime checks remain as-is during this phase.

Deliverable:

1. Toolchain is source of truth for compiler selection and language flag.

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
- Add `host-macos` to Metal runtime suite in libgpu.
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

1. Host-accurate ctest preset behavior without brittle regex-only filtering.

## Execution Sequence (Recommended Commit Plan)

1. Commit 1: Toolchain split foundation.
2. Commit 2: Preset inheritance migration.
3. Commit 3: Top-level CMake compiler policy cleanup.
4. Commit 4: Host test labels across test CMake files.
5. Commit 5: Not relevant skip wrapper and host-gated execution.
6. Commit 6: Test preset label-filter migration and README updates.

## Validation Matrix

Run these after each major phase:

1. `cmake --preset ninja-debug`
2. `cmake --build --preset build-debug`
3. `ctest --preset test-ci-macos --output-on-failure` on macOS
4. `ctest --preset test-ci-linux --output-on-failure` on Linux
5. `ctest --test-dir build-ninja/ninja-debug --output-on-failure`
6. `ctest -N` with each CI preset to verify selection shape

Acceptance checks:

1. No compiler ambiguity in configure logs.
2. No host-mismatch failures from Metal-only or local-only lanes.
3. Host-mismatch suites show skipped or are excluded by label as designed.

## Risk Controls

1. Keep public preset names stable to minimize developer disruption.
2. Add migration notes in README for new compiler-specific presets.
3. Keep one fallback manual configure path only if needed for ad-hoc debugging.
4. Land in small commits so regressions are easy to isolate and revert.
