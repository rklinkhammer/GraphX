# SAR CRSD To Focused Image VERIFIER Report - PR7

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR7 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: SarPy/reference focused-image generation harness from CRSD

## Verification Result

PASS

## Findings (ordered by severity)

1. Reference script accepts ordered CRSD input sets.
- `tools/sarpy/reference_image_from_crsd.py` supports:
  - `--input-crsd-set-json` (object with `crsd_paths`/`ordered_crsd_paths` or array)
  - repeated `--input-crsd`
- Direct runtime verification with tiny multi-segment fixture set produced:
  - `ordered_set_count=3`
  - focused artifact and metadata outputs present

2. Focused reference artifact path is implemented and emits one artifact for the ordered set.
- Script emits one focused-reference artifact (`--output-reference-npy`) plus metadata and magnitude PNG.
- Metadata marks `reference_kind=independent_local_focused_surrogate` and records ordered-set lineage/hash fields.
- For this environment, probe reports `supports_true_focused_reference=false`; limitation is documented and no misleading true-focused SarPy claim is made.

3. Local-only/gated boundaries are preserved.
- Probe and metadata expose `local_only=true`, `ci_safe=false`.
- Optional smoke test is env-gated via `GRAPHX_SARPY_CRSD_FILE` and skipped when unset.
- No evidence that this workflow became a GraphX runtime dependency.

4. Quick-look extraction is explicitly rejected as focused reference by tests and docs.
- Guardrail in script rejects `--force-quicklook-extraction` with `quicklook_rejected` error.
- Test `SarpyCrsdValidationHarnessTest.GuardrailRejectsQuicklookExtractionAsFocusedReference` validates rejection behavior.
- `tools/sarpy/README.md` documents quick-look rejection boundary.

5. No out-of-scope changes detected for PR7.
- No GraphX focused-image algorithm rewrite identified.
- No comparison lane implementation work added in PR7 scope.
- No local real-data workflow redesign added.
- No MATLAB dependency introduced.
- No CI SarPy requirement introduced; workflow remains local-only/gated.

## Required Checks

1. Reference script accepts an ordered CRSD input set.
- PASS

2. Reference path produces one focused reference image artifact when a true reference image-formation path is available.
- PASS (conditional path handled correctly)
- True SarPy-focused path is not available in this harness/environment and is explicitly documented.
- Independent local focused surrogate path is implemented and produces one focused-reference artifact for the ordered set without misleading claims.

3. SarPy/reference workflow remains local-only/gated and not a runtime or CI dependency.
- PASS

4. Tests/documentation reject CRSD signal block magnitude extraction as a focused reference.
- PASS

5. If SarPy cannot generate a true focused image, the limitation is documented clearly and no misleading focused-output claim is made.
- PASS

6. No GraphX focused-image algorithm rewrite, comparison lane, real-data workflow, MATLAB dependency, or CI SarPy requirement was added.
- PASS

## Commands/Evidence Used

```bash
rg -n "input-crsd-set-json|--input-crsd|output-reference-npy|quicklook_rejected|supports_true_focused_reference|supports_independent_local_surrogate|local_only|ci_safe|limitation|reference_kind" tools/sarpy/reference_image_from_crsd.py examples/SAR/test/test_sarpy_crsd_validation_harness.cpp tools/sarpy/README.md

cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarpyCrsdValidationHarnessTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarpyReferenceCompareToolsTest.ProbeCommandsAreLocalOnlyAndNonBlocking:SarpyCrsdValidationHarnessTest.ProbeCommandsDeclareLocalOnlyHarness'

python3 tools/sarpy/reference_image_from_crsd.py probe-environment --output-json /tmp/graphx_pr7_probe.json

# Ordered-set runtime generation check (tiny fixture)
python3 tools/sarpy/reference_image_from_crsd.py generate-reference \
  --input-crsd-set-json /tmp/.../ordered_set.json \
  --output-reference-npy /tmp/.../reference_focused.npy \
  --output-magnitude-png /tmp/.../reference_magnitude.png \
  --output-metadata-json /tmp/.../reference_metadata.json
```

## Verifier Conclusion

PR7 satisfies the required checks. The reference harness now accepts ordered CRSD sets, remains local-only/gated, rejects quick-look-as-focused-reference misuse, documents the true-focused SarPy limitation clearly, and provides a non-misleading independent local focused surrogate path for reference artifact generation.
