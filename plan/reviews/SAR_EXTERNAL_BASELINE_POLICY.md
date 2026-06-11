# SAR External Baseline Policy

Date: 2026-06-10
Applies to: External baseline program for SAR comparison work.

## Intent
GraphX uses external SAR packages as comparators only.
External baselines inform validation and measurement, but do not define GraphX runtime architecture.

## Comparator Registry
The authoritative machine-readable registry is:
- `plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json`

## Approved Baseline Roles
- Primary comparator: SarPy (`ngageoint/sarpy`)
- Secondary comparator: ISCE3 (`isce-framework/isce3`)
- Secondary comparator: gotcha-back (`tbensonatl/gotcha-back`)

## Architecture Protection Rules
1. Comparator-only stance is mandatory for all registered external packages.
2. Do not copy or mirror external package internal APIs into GraphX core.
3. Do not modify `libgraph` or `libgpu` contracts to imitate external framework designs.
4. Keep canonical GraphX SAR runtime contract unchanged: `AccelControlToken<SarSidecar>`.
5. Keep Metal as the first backend in maintained GraphX presets.
6. Keep dynamic loading and resolver behavior GraphX-native.

## Licensing Boundaries
1. Maintain explicit package license metadata in the registry.
2. Keep license-sensitive integrations in harness/adaptor layers, not GraphX core.
3. Keep optional local-only lanes separate from required CI lanes.
4. Do not introduce licensing-risky embedding patterns into GraphX runtime code.

## CI and Local Lane Boundaries
1. CI-safe lanes must use deterministic tiny fixtures and bounded runtime.
2. Large datasets and heavy toolchains stay local-only unless explicitly promoted.
3. Promotion from local-only to CI requires policy and registry update plus tests.

## Change Control
1. Any change to package roles, lane class, licensing boundary, or architecture rules must update both:
   - this policy file
   - the machine-readable registry
2. Policy and registry changes must be covered by tests in `examples/SAR/test`.
