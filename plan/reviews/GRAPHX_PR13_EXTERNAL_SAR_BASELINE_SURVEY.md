# GraphX PR13 External SAR Baseline Survey

Date: 2026-06-23
Scope: Planning and documentation only.

## Goal

Document candidate external SAR baseline packages without integrating any dependency into GraphX.

## Candidate Comparison Matrix

- Candidate: SarPy toolchain (local reference scripts)
  - License status: Requires revalidation at integration time.
  - Install complexity: Medium (Python environment plus domain dependencies).
  - Data support fit: Strong for CRSD/SICD metadata and product validation workflows.
  - Output format fit: Strong for local comparison artifacts and metadata checks.
  - Determinism outlook: Medium (depends on pinned environment and reproducible scripts).
  - CI/local feasibility: Local-only viable; not CI-default.

- Candidate: gotcha-back-style local reference flow
  - License status: Requires revalidation at integration time.
  - Install complexity: Medium to high (tool/runtime setup varies).
  - Data support fit: Narrower and GOTCHA-focused.
  - Output format fit: Medium (useful for selected local comparison workflows).
  - Determinism outlook: Medium to low unless environment and inputs are tightly pinned.
  - CI/local feasibility: Local-only viable; not CI-default.

- Candidate: Generic external SAR stacks (deferred shortlist)
  - License status: Unknown until shortlist is finalized.
  - Install complexity: Unknown.
  - Data support fit: Unknown.
  - Output format fit: Unknown.
  - Determinism outlook: Unknown.
  - CI/local feasibility: Not suitable for CI-default before gated runner exists.

## Findings

- No candidate is currently mature enough in-repo to claim integrated support.
- The current GraphX position remains: external SAR baseline tools are local-only reference/comparison aids.
- License and reproducibility checks must be revalidated at implementation time because external package status can change.

## Recommendation

Clear deferral for PR13.

- No package is selected for integration in this PR.
- PR14 may select one candidate only when:
  - license compatibility is verified at implementation time,
  - deterministic local runner behavior is demonstrated,
  - CI-safe gating remains strict and default CI stays dependency-free.

## Truth-In-Labeling

- This survey does not imply package support exists in GraphX runtime or default CI.
- Any future baseline runner remains local-only unless a later PR explicitly adds CI-safe skip behavior.
