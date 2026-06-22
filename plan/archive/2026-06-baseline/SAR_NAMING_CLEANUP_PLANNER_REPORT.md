# SAR Naming Cleanup Planner Report

Date: 2026-06-13
Role: PLANNER
Input: `plan/SAR_SIMPLIFIER_REPORT.md` and current repository state
Goal: remove planning-era `prXX`/`rrpXX` naming from active SAR product surfaces while preserving final GOTCHA, CRSD, graphx-crsd-lite, validation, and comparison behavior.

## Planning Notes

- Backward compatibility is not required.
- The cleanup should distinguish active product surfaces from historical planning material.
- Historical planning content may keep PR/RRP language only after it is explicitly quarantined under `plan/history`.
- Active code, tests, scripts, docs, CMake definitions, CTest labels, benchmark output, and user-visible strings should use capability names only.
- Each PR below is intended to compile and test independently.
- The final PR introduces a guardrail search so PR/RRP naming does not return outside allowed history paths.

## PR1: Quarantine Historical Planning Artifacts

Purpose:
- Move implementation/verifier history out of active `plan/reviews` so future lint can exempt historical material without weakening active product cleanup.

Files to touch:
- `plan/reviews/SAR_IMPL_PR*_*.md`
- `plan/reviews/SAR_VERIFY_PR*_*.md`
- `plan/reviews/SAR_GOTCHA_TO_CRSD_CURRENT_STATE.md` if classified as historical current-state review rather than final documentation
- Any index or README under `plan/` that lists review artifacts, if present

Files to delete:
- None. This PR moves files only.

Tests to add:
- None.

Tests to delete:
- None.

Acceptance criteria:
- All `SAR_IMPL_PR*` and `SAR_VERIFY_PR*` reports currently in `plan/reviews` are moved to `plan/history/reviews`.
- `plan/reviews` retains only active architectural reports that the team still uses as inputs, such as inspector/simplifier/planner reports and active policy artifacts.
- No code, CMake, or runtime behavior changes.

Risks:
- Existing prompts or docs may still reference old `plan/reviews/SAR_IMPL_PR*` paths.

Rollback plan:
- Move the reports back to `plan/reviews` if a workflow still depends on the old location.

CI-safe or local-only:
- CI-safe.

## PR2: Delete Tracked Cache And Intermediate-Only Config Tests

Purpose:
- Remove artifacts that only exist because of implementation history or Python cache generation.
- Reduce the active cleanup surface before broad renames.

Files to touch:
- `examples/SAR/test/CMakeLists.txt`

Files to delete:
- `examples/SAR/tools/__pycache__/rrp1_scenario_to_run.cpython-313.pyc`
- `examples/SAR/tools/__pycache__/rrp3_gotcha_back_adapter.cpython-313.pyc`
- `examples/SAR/test/test_sar_pr2_fanout_json.cpp`
- `examples/SAR/test/test_sar_pr2_token_contract.cpp`
- `examples/SAR/test/test_sar_pr3_metal_json.cpp`

Tests to add:
- None.

Tests to delete:
- The three intermediate-only SAR PR config tests above, after confirming equivalent capability coverage remains in:
  - `test_sar_json_runtime.cpp`
  - `test_sar_token_contract.cpp`
  - `test_sar_transport_opaque_contract.cpp`
  - `test_sar_accel_token_guardrails.cpp`
  - `test_sar_main_executable.cpp`

Acceptance criteria:
- Deleted tests are removed from `test_sar_example_unit` sources.
- Equivalent accel-token, resolver, definitive-config, and `examples/SAR/main.cpp` guardrails still run.
- Full SAR unit binary passes.

Risks:
- A deleted file may contain a unique assertion not yet represented by behavior-named tests.

Rollback plan:
- Restore the specific deleted test file and rename it into a behavior-based test if a coverage gap is found.

CI-safe or local-only:
- CI-safe.

## PR3: Rename Active RRP Tooling To Capability Names

Purpose:
- Remove `rrpXX` naming from active scripts, schemas, and local workflow docs under `examples/SAR/tools`.

Files to touch:
- `examples/SAR/tools/rrp1_local_runner.py`
- `examples/SAR/tools/rrp1_scenario_to_run.py`
- `examples/SAR/tools/rrp3_gotcha_back_adapter.py`
- `examples/SAR/tools/rrp4_image_comparator.py`
- `examples/SAR/tools/rrp7_sarpy_harness.py`
- `examples/SAR/tools/rrp1_local_runner.md`
- `examples/SAR/tools/rrp2_cpu_reference_backprojection.md`
- `examples/SAR/tools/rrp5_frozen_scenario_replay.md`
- `examples/SAR/tools/rrp7_sarpy_harness.md`
- `examples/SAR/tools/rrp4_image_comparison_report.schema.json`
- `examples/SAR/tools/local_gotcha_validation.md`
- `examples/SAR/test/CMakeLists.txt`
- Tests and docs that reference those tool paths

Files to delete:
- Old tool/doc/schema filenames after moves:
  - `examples/SAR/tools/rrp1_local_runner.py`
  - `examples/SAR/tools/rrp1_scenario_to_run.py`
  - `examples/SAR/tools/rrp3_gotcha_back_adapter.py`
  - `examples/SAR/tools/rrp4_image_comparator.py`
  - `examples/SAR/tools/rrp7_sarpy_harness.py`
  - `examples/SAR/tools/rrp1_local_runner.md`
  - `examples/SAR/tools/rrp2_cpu_reference_backprojection.md`
  - `examples/SAR/tools/rrp5_frozen_scenario_replay.md`
  - `examples/SAR/tools/rrp7_sarpy_harness.md`
  - `examples/SAR/tools/rrp4_image_comparison_report.schema.json`

Tests to add:
- None, unless a moved tool lacks direct path coverage after CMake definition updates.

Tests to delete:
- None.

Replacement names:
- `rrp1_local_runner.py` -> `sar_local_runner.py`
- `rrp1_scenario_to_run.py` -> `sar_scenario_to_run.py`
- `rrp3_gotcha_back_adapter.py` -> `gotcha_back_adapter.py`
- `rrp4_image_comparator.py` -> `sar_image_comparator.py`
- `rrp7_sarpy_harness.py` -> `sarpy_metadata_harness.py`
- `rrp1_local_runner.md` -> `sar_local_runner.md`
- `rrp2_cpu_reference_backprojection.md` -> `cpu_reference_backprojection.md`
- `rrp5_frozen_scenario_replay.md` -> `frozen_scenario_replay.md`
- `rrp7_sarpy_harness.md` -> `sarpy_metadata_harness.md`
- `rrp4_image_comparison_report.schema.json` -> `sar_image_comparison_report.schema.json`
- `local_gotcha_validation.md` title changes from PR18 wording to final local-only GOTCHA validation wording

Acceptance criteria:
- No `rrp`/`RRP` tokens remain in `examples/SAR/tools` filenames or active tool/doc contents.
- Tool path compile definitions in `examples/SAR/test/CMakeLists.txt` use capability names, for example `SAR_LOCAL_RUNNER_PATH`, `SAR_IMAGE_COMPARATOR_PATH`, and `SARPY_METADATA_HARNESS_PATH`.
- Existing tool tests still pass.

Risks:
- Python scripts may import each other by old filenames.
- Markdown guides may contain command examples that reference old names.

Rollback plan:
- Restore the old filename for the single tool that breaks, then reapply the rename with updated imports and tests.

CI-safe or local-only:
- CI-safe. Local-only workflows remain optional and gated.

## PR4: Rename Active PR/RRP Test Files And Suites

Purpose:
- Remove planning-era naming from retained tests while preserving behavior coverage.

Files to touch:
- `examples/SAR/test/CMakeLists.txt`
- `examples/SAR/test/test_pr13_sarpy_tools.cpp`
- `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp`
- `examples/SAR/test/test_pr16_graphx_crsd_lite_lane.cpp`
- `examples/SAR/test/test_pr17_graphx_image_comparison_lane.cpp`
- `examples/SAR/test/test_pr18_local_gotcha_validation.cpp`
- `examples/SAR/test/test_rrp1_local_runner.cpp`
- `examples/SAR/test/test_rrp2_scenario_image_path.cpp`
- `examples/SAR/test/test_rrp3_graphx_scenario_runner.cpp`
- `examples/SAR/test/test_rrp3_gotcha_back_adapter.cpp`
- `examples/SAR/test/test_rrp4_image_comparator.cpp`
- `examples/SAR/test/test_rrp5_comparator_metrics.cpp`
- `examples/SAR/test/test_rrp5_frozen_scenario_replay.cpp`
- `examples/SAR/test/test_rrp6_ci_correctness_lane.cpp`
- `examples/SAR/test/test_rrp6_tiny_fixture.cpp`
- `examples/SAR/test/test_rrp7_ci_validation_lane.cpp`
- `examples/SAR/test/test_rrp7_sarpy_harness.cpp`
- `examples/SAR/test/sar_pr7_parity_fixture.hpp`

Files to delete:
- Old filenames after moves.

Tests to add:
- None. This is a rename and suite-name cleanup PR.

Tests to delete:
- None in this PR.

Replacement names:
- `test_pr13_sarpy_tools.cpp` -> `test_sarpy_reference_compare_tools.cpp`
- `test_pr14_sarpy_crsd_harness.cpp` -> `test_sarpy_crsd_validation_harness.cpp`
- `test_pr16_graphx_crsd_lite_lane.cpp` -> `test_graphx_crsd_lite_lane.cpp`
- `test_pr17_graphx_image_comparison_lane.cpp` -> `test_graphx_image_comparison_lane.cpp`
- `test_pr18_local_gotcha_validation.cpp` -> `test_local_gotcha_validation_lane.cpp`
- `test_rrp1_local_runner.cpp` -> `test_local_runner_contract.cpp`
- `test_rrp2_scenario_image_path.cpp` -> `test_scenario_image_path_contract.cpp`
- `test_rrp3_graphx_scenario_runner.cpp` -> `test_graphx_scenario_runner_contract.cpp`
- `test_rrp3_gotcha_back_adapter.cpp` -> `test_gotcha_back_adapter_contract.cpp`
- `test_rrp4_image_comparator.cpp` -> `test_image_comparator_contract.cpp`
- `test_rrp5_comparator_metrics.cpp` -> `test_image_comparator_metrics.cpp`
- `test_rrp5_frozen_scenario_replay.cpp` -> `test_frozen_scenario_replay.cpp`
- `test_rrp6_ci_correctness_lane.cpp` -> `test_ci_correctness_lane.cpp`
- `test_rrp6_tiny_fixture.cpp` -> `test_ci_tiny_fixture.cpp`
- `test_rrp7_ci_validation_lane.cpp` -> `test_ci_validation_lane.cpp`
- `test_rrp7_sarpy_harness.cpp` -> `test_sarpy_metadata_harness.cpp`
- `sar_pr7_parity_fixture.hpp` -> `sar_reference_parity_fixture.hpp`

Acceptance criteria:
- File names, test suite names, namespace aliases, skip messages, and string literals in retained tests use capability names rather than PR/RRP names.
- CMake source list points to the new names only.
- Full SAR unit binary passes.

Risks:
- Large rename PR may obscure behavior changes.
- GTest filters used outside the repo may break; backward compatibility is explicitly not required.

Rollback plan:
- Revert the rename batch, then split into smaller test-family rename PRs if review becomes too noisy.

CI-safe or local-only:
- CI-safe. Local-only tests remain skipped unless explicitly enabled.

## PR5: Rename SAR Configs And Compile Definitions

Purpose:
- Remove `prXX` naming from active SAR JSON config filenames and CMake definitions.

Files to touch:
- `examples/SAR/config/sar_stripmap_pr1.json`
- `examples/SAR/config/sar_stripmap_pr2_fanout.json`
- `examples/SAR/config/sar_stripmap_pr3_metal_window.json`
- `examples/SAR/config/sar_stripmap_pr3_metal_compression.json`
- `examples/SAR/config/sar_stripmap_pr3_metal_fanout.json`
- `examples/SAR/config/sar_stripmap_pr6_matched_filter.json`
- `examples/SAR/config/sar_stripmap_pr7_materialized_image.json`
- `examples/SAR/config/sar_projectile_approach_pr1.json`
- `examples/SAR/config/sar_stripmap_definitive_metal.json`
- `examples/SAR/config/sar_stripmap_definitive_nonmetal.json`
- `examples/SAR/test/CMakeLists.txt`
- Any README/docs/tests that reference the old config names

Files to delete:
- Old config filenames after moves.
- `examples/SAR/config/sar_stripmap_definitive_metal.json`
- `examples/SAR/config/sar_stripmap_definitive_nonmetal.json`, after their retained behavior is folded into final config tests or documented as obsolete.

Tests to add:
- If deletion of definitive variants removes explicit coverage, add capability-named cases to `test_sar_json_runtime.cpp` or `test_sar_main_executable.cpp`.

Tests to delete:
- None beyond PR2’s already-deleted intermediate tests.

Replacement names:
- `sar_stripmap_pr1.json` -> `sar_stripmap_simulated.json`
- `sar_stripmap_pr2_fanout.json` -> `sar_stripmap_fanout.json`
- `sar_stripmap_pr3_metal_window.json` -> `sar_stripmap_metal_window.json`
- `sar_stripmap_pr3_metal_compression.json` -> `sar_stripmap_metal_compression.json`
- `sar_stripmap_pr3_metal_fanout.json` -> `sar_stripmap_metal_fanout.json`
- `sar_stripmap_pr6_matched_filter.json` -> `sar_stripmap_matched_filter.json`
- `sar_stripmap_pr7_materialized_image.json` -> `sar_stripmap_materialized_image.json`
- `sar_projectile_approach_pr1.json` -> `sar_projectile_approach.json`
- CMake definitions move from `SAR_PR*_...` / `SAR_RRP*_...` to capability names, for example `SAR_FANOUT_JSON_CONFIG_PATH`, `SAR_METAL_WINDOW_JSON_CONFIG_PATH`, and `SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH`.

Acceptance criteria:
- No active config filename contains `pr`/`PR`.
- CMake compile definitions no longer include `SAR_PR` or `SAR_RRP`.
- `examples/SAR/main.cpp` still runs with `sar_stripmap_definitive.json`.
- Full SAR unit binary passes.

Risks:
- README snippets and local scripts may point to old config names.
- Removing definitive variants may remove useful debug presets if they are still manually used.

Rollback plan:
- Restore a removed config under a capability name if its behavior is still needed.

CI-safe or local-only:
- CI-safe.

## PR6: Clean Product Docs, Benchmark Output, Comments, And Scenario Text

Purpose:
- Remove planning-era wording from product-facing docs, benchmark output, comments, and scenario docs.

Files to touch:
- `examples/SAR/README.md`
- `examples/SAR/BENCHMARK_REPORT.md`
- `docs/sar/gotcha_crsd_repo_discovery.md`
- `docs/sar/crsd_definition.md`
- `tools/sarpy/README.md`
- `examples/SAR/scenarios/scenario_001.md`
- `examples/SAR/src/sar_benchmark.cpp`
- SAR source comments containing `PR2`, including:
  - `examples/SAR/src/AzimuthTileSplitNode.cpp`
  - `examples/SAR/src/D2HAsyncAccelNode.cpp`
  - `examples/SAR/src/GotchaReplaySourceNode.cpp`
  - `examples/SAR/src/H2DAsyncAccelNode.cpp`
  - `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
  - `examples/SAR/src/SyntheticApertureIqSourceNode.cpp`

Files to delete:
- None.

Tests to add:
- None.

Tests to delete:
- None.

Acceptance criteria:
- Product docs describe final capabilities, not PR/RRP milestones.
- Benchmark stdout no longer contains `PR4`, `PR5`, `PR6`, `pr2`, or `pr3`; labels use capability names such as cost buckets, fidelity metrics, matched-filter runtime, keep-native, defer-generic-reduce.
- Comments describe architecture invariants directly, for example `host_ptr is opaque transport metadata only`.
- Scenario docs remove `RRP0` wording and describe CI-safe synthetic fixture boundaries directly.
- Existing benchmark trace/schema tests pass.

Risks:
- Some docs, especially `gotcha_crsd_repo_discovery.md`, may be intentionally historical. If so, either move them under `plan/history` or rewrite them as final docs in `docs/sar`.

Rollback plan:
- Restore any doc section where historical context is still required, but move it to `plan/history` rather than product docs.

CI-safe or local-only:
- CI-safe.

## PR7: Add Naming Hygiene Lint And Final Verification Gate

Purpose:
- Enforce the final naming model by scanning active files and filenames for forbidden planning-era tokens.

Files to touch:
- Root or project CMake files where tests/lints are registered
- `examples/SAR/test/CMakeLists.txt` or a new CMake script under `cmake/`
- Optional helper script, for example `scripts/check_sar_naming_hygiene.sh`
- CI workflow files if the lint should run outside CTest

Files to delete:
- None.

Tests to add:
- A CTest or unit-style lint test that fails on forbidden tokens outside allowed historical paths.

Tests to delete:
- None.

Forbidden tokens:
- File paths and active file contents matching:
  - `\bpr[0-9]+\b`
  - `\bPR[0-9]+\b`
  - `\brrp[0-9]+\b`
  - `\bRRP[0-9]+\b`
  - `SAR_IMPL_PR`
  - `SAR_VERIFY_PR`
  - `SAR_PR[0-9]+`
  - `SAR_RRP[0-9]+`

Allowed paths:
- `plan/history/**`
- `plan/old/**`
- `plan/agents/**`
- `plan/prompt examples/**`
- `plan/SAR_SIMPLIFIER_REPORT.md`
- `plan/SAR_NAMING_CLEANUP_PLANNER_REPORT.md`
- Any explicitly historical architecture document if the team chooses to allow it

Acceptance criteria:
- Lint scans both filenames and file contents.
- Lint excludes generated build directories, `.git`, binary artifacts, external datasets, and allowed history paths.
- Lint is part of the normal build/test path.
- The lint passes after PR1-PR6.
- Full SAR unit binary still passes.

Risks:
- False positives in unrelated historical docs outside `plan/history`.
- Over-broad regex may catch legitimate non-PR words.

Rollback plan:
- Narrow the allowed path list or regex while preserving active SAR product cleanup.

CI-safe or local-only:
- CI-safe.

## Final Verification Commands

Recommended after each implementation PR:

```bash
cmake --build build-ninja/ninja-debug --target test_sar_example_unit
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit
```

Recommended final naming scan before merging PR7:

```bash
rg -n --hidden \
  --glob '!build-*' \
  --glob '!.git' \
  --glob '!plan/history/**' \
  --glob '!plan/old/**' \
  --glob '!plan/agents/**' \
  --glob '!plan/prompt examples/**' \
  '\b(pr|PR|rrp|RRP)[0-9]+\b|SAR_IMPL_PR|SAR_VERIFY_PR|SAR_PR[0-9]+|SAR_RRP[0-9]+' .
```

## Residual Open Questions

- Should `docs/sar/gotcha_crsd_repo_discovery.md` remain product documentation after PR wording is removed, or move to `plan/history` as a discovery artifact?
- Should `doc/architecture/CUDA_GRAPH_NODE_IMPLEMENTATION_PLAN.md` be exempt as historical architecture material or rewritten to remove `PR1` language?
- Should `plan/agents/SAR_PR_AGENTS.md` remain as historical prompt material under `plan/agents`, or be moved to `plan/history/agents` after this cleanup?
