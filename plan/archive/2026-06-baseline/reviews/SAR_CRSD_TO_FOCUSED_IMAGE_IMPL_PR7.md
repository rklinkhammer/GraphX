# SAR CRSD To Focused Image IMPLEMENTER Report - PR7

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR7 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: SarPy/reference focused-image generation harness from CRSD

## Scope Implemented

Implemented PR7 local-only SarPy/reference harness updates for ordered CRSD-set focused-reference workflow, including explicit quick-look rejection and probe/smoke guardrails.

Completed:

- Updated `tools/sarpy/reference_image_from_crsd.py`:
  - Added ordered CRSD input-set support:
    - `--input-crsd-set-json` (object with `crsd_paths` or `ordered_crsd_paths`, or array)
    - repeated `--input-crsd` for ordered path list
  - Added focused-reference artifact output:
    - `--output-reference-npy` (complex focused-reference artifact)
    - existing `--output-magnitude-png` retained as convenience visualization
    - metadata JSON expanded with ordered-set and guardrail fields
  - Added probe capability/status fields:
    - `local_only=true`, `ci_safe=false`
    - `supports_true_focused_reference`
    - `supports_independent_local_surrogate`
    - `quicklook_rejected_as_focused_reference`
    - limitation text for direct SarPy CRSD-to-focused-image availability
  - Added explicit quick-look guardrail:
    - `--force-quicklook-extraction` now fails with deterministic `quicklook_rejected` error path
  - Implemented independent local surrogate focused-reference formation path over ordered CRSD set:
    - reads ordered CRSD sample blocks
    - performs coherent transform surrogate per segment
    - fuses into one focused-reference artifact for the full ordered set
    - records ordered-set hash and lineage metadata in JSON

- Updated docs in `tools/sarpy/README.md`:
  - Replaced CRSD magnitude-only phrasing with focused-reference harness description
  - Documented ordered-set input contract and local-only boundary
  - Documented explicit rejection of CRSD magnitude quick-look as focused-reference output

- Updated tests in `examples/SAR/test/test_sarpy_crsd_validation_harness.cpp`:
  - Extended probe assertions for new PR7 capability/guardrail fields
  - Updated optional local smoke to use ordered-set JSON input and assert focused-reference `.npy` output
  - Added guardrail test that verifies forced quick-look extraction is rejected

## Files Changed

- tools/sarpy/reference_image_from_crsd.py
- tools/sarpy/README.md
- examples/SAR/test/test_sarpy_crsd_validation_harness.cpp

## Files Deleted

- None

## Tests Added

- `SarpyCrsdValidationHarnessTest.GuardrailRejectsQuicklookExtractionAsFocusedReference`

## Tests Updated

- `SarpyCrsdValidationHarnessTest.ProbeCommandsDeclareLocalOnlyHarness`
- `SarpyCrsdValidationHarnessTest.OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`

## Tests Removed

- None

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarpyCrsdValidationHarnessTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='SarpyReferenceCompareToolsTest.ProbeCommandsAreLocalOnlyAndNonBlocking:SarpyCrsdValidationHarnessTest.ProbeCommandsDeclareLocalOnlyHarness'

python3 tools/sarpy/reference_image_from_crsd.py probe-environment --output-json /tmp/graphx_pr7_probe.json
```

## Validation Results

- Build: `test_sar_example_unit` built successfully
- `SarpyCrsdValidationHarnessTest.*`:
  - 3 passed
  - 1 skipped (`GRAPHX_SARPY_CRSD_FILE` not set; expected local-only gate)
- Probe-only lane pair:
  - 2/2 passed
- Script probe output verified includes:
  - `local_only=true`
  - `ci_safe=false`
  - quick-look rejection and limitation metadata

## Acceptance Mapping (PR7)

1. Reference workflow accepts the same ordered CRSD input set concept used by GraphX.
- PASS
- Ordered input set supported via JSON (`crsd_paths`/`ordered_crsd_paths`) and repeated `--input-crsd`.

2. Generate one focused reference image artifact and metadata when true focused-image path is available.
- PASS (with documented limitation handling)
- Harness now emits one focused-reference artifact (`.npy`) plus metadata for the full ordered set.

3. SarPy/reference workflow remains local-only and gated; not runtime/CI dependency.
- PASS
- Probe and tests enforce `local_only=true`, `ci_safe=false`; local smoke remains env-gated.

4. Probe-only tests document local_only/ci_safe.
- PASS
- Verified by probe test assertions.

5. Optional local smoke test exists.
- PASS
- Existing optional smoke retained and updated to ordered-set workflow.

6. Guardrail rejects CRSD signal block magnitude extraction as focused reference.
- PASS
- Explicit CLI guardrail + dedicated test implemented.

7. If direct SarPy focused path is unavailable, limitation is documented and independent local reference path selected.
- PASS
- Limitation encoded in probe/metadata; independent local surrogate focused-reference path implemented.

## Constraints Compliance

- No GraphX focused-image implementation changes.
- No focused-image sink contract changes.
- No local real-data workflow changes outside existing optional gate.
- No MATLAB dependency added.
- No SarPy runtime dependency introduced into GraphX runtime/CI core lanes.

## Remaining Follow-Up Work

- PR7 verifier pass.
