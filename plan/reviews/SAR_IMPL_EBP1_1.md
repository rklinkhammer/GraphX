# SAR Implementation Report: EBP1

Date: 2026-06-10
PR: EBP1
Title: External Baseline Policy and Registry
Scope: Establish baseline package roles, licensing boundaries, architecture-protection rules, and comparator-only stance for SarPy, ISCE3, and gotcha-back.

## Summary
EBP1 is implemented as policy and registry artifacts plus CI-safe validation tests. No runtime SAR graph semantics were changed. The implementation enforces comparator-only boundaries and records licensing/architecture guardrails in machine-readable form.

## 1) Files Changed
- examples/SAR/test/CMakeLists.txt
  - Added new EBP1 test source to `test_sar_example_unit`.
  - Added compile definitions for policy and registry file paths:
    - `SAR_EXTERNAL_BASELINE_POLICY_PATH`
    - `SAR_BASELINE_PACKAGE_REGISTRY_PATH`

- examples/SAR/test/test_external_baseline_policy_registry.cpp
  - Added EBP1 validation tests covering:
    - comparator-only package roles
    - licensing boundary metadata presence/constraints
    - architecture-protection rules and canonical contract assertions
    - policy text boundary assertions

- plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md
  - Added external baseline policy document with:
    - comparator-only stance
    - licensing boundaries
    - architecture protection rules
    - CI/local lane boundaries
    - change-control requirements

- plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json
  - Added machine-readable baseline registry with:
    - package roles for SarPy (primary), ISCE3 (secondary), gotcha-back (secondary)
    - declared licenses
    - comparator-only flags
    - lane classification
    - architecture and licensing guardrail keys

## 2) Files Deleted
- None.

## 3) Tests Added
- examples/SAR/test/test_external_baseline_policy_registry.cpp
  - `ExternalBaselinePolicyRegistryTest.DeclaresComparatorOnlyPackageRoles`
  - `ExternalBaselinePolicyRegistryTest.EncodesLicensingAndArchitectureBoundaries`
  - `ExternalBaselinePolicyRegistryTest.PolicyDeclaresComparatorOnlyBoundaries`

## 4) Tests Removed or Replaced
- None.

## 5) Build Commands Run
- CMake Tools target build:
  - `test_sar_example_unit`

## 6) Test Commands Run
- CMake Tools CTest run (configured build tree):
  - `libgraph_unit`
  - `libgraph_integration`
  - `libgpu_stub_unit`
  - `libgpu_metal_runtime`
  - `sar_example_unit`
- Final result: 5/5 tests passed.

## 7) Remaining Follow-Up Items
- Populate explicit EBP1 acceptance criteria text in prompt template file:
  - plan/prompt examples/perf.md
- Optional hardening: add stricter schema validation rules (for allowed role/lane enums and required fields) as a dedicated follow-up test.
