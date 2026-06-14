# SAR Simplifier Report

Date: 2026-06-13
Role: SIMPLIFIER
Scope: naming and artifact-classification only (no implementation)

1. Executive recommendation.
Adopt a strict product-history boundary and execute cleanup as a naming-only architecture refinement.

- Product surfaces in examples/SAR, tools, scripts, docs/sar, and SAR workflow content must eliminate PR/RRP naming from files, symbols, labels, and user strings.
- Preserve all non-PR configs in examples/config, while folding deprecated definitive variants into a single definitive config contract.
- Move all historical implement/verify artifacts in one step from plan/reviews to plan/history/reviews.
- Enforce a hard CMake lint policy: no prNN/rrpNN/SAR_IMPL_PR/SAR_VERIFY_PR tokens outside plan, with plan/history/reviews explicitly exempt.
- Keep runtime behavior unchanged, including permanent graphx-crsd-lite NON-STANDARD behavior.

2. Final naming model.
- Tests: test_<capability>_<behavior>.cpp
- Test suites: <Capability><Behavior>Test
- Docs: capability-oriented, no roadmap IDs
- Scripts: action + capability naming
- Local-only workflows: explicit local + capability names
- Generated reports: conversion/validation/comparison/index terms only
- Fixtures: scenario/capability labels only
- CMake/CTest labels/defs: capability terms only (sar, gotcha, crsd, sarpy, local-only, integration, validation)

3. Keep-and-rename list.
Keep behavior, rename planning-era naming.

- Test files in examples/SAR/test, including:
  - test_pr13_sarpy_tools.cpp
  - test_pr14_sarpy_crsd_harness.cpp
  - test_pr16_graphx_crsd_lite_lane.cpp
  - test_pr17_graphx_image_comparison_lane.cpp
  - test_pr18_local_gotcha_validation.cpp
  - test_rrp1_local_runner.cpp
  - test_rrp2_scenario_image_path.cpp
  - test_rrp3_graphx_scenario_runner.cpp
  - test_rrp3_gotcha_back_adapter.cpp
  - test_rrp4_image_comparator.cpp
  - test_rrp5_comparator_metrics.cpp
  - test_rrp5_frozen_scenario_replay.cpp
  - test_rrp6_ci_correctness_lane.cpp
  - test_rrp6_tiny_fixture.cpp
  - test_rrp7_ci_validation_lane.cpp
  - test_rrp7_sarpy_harness.cpp
  - test_sar_pr2_fanout_json.cpp
  - test_sar_pr2_token_contract.cpp
  - test_sar_pr3_metal_json.cpp
  - sar_pr7_parity_fixture.hpp
- Tool scripts/docs (keep under tools area, rename):
  - examples/SAR/tools/rrp1_local_runner.py
  - examples/SAR/tools/rrp1_scenario_to_run.py
  - examples/SAR/tools/rrp3_gotcha_back_adapter.py
  - examples/SAR/tools/rrp4_image_comparator.py
  - examples/SAR/tools/rrp7_sarpy_harness.py
  - examples/SAR/tools/rrp1_local_runner.md
  - examples/SAR/tools/rrp2_cpu_reference_backprojection.md
  - examples/SAR/tools/rrp5_frozen_scenario_replay.md
  - examples/SAR/tools/rrp7_sarpy_harness.md
  - examples/SAR/tools/rrp4_image_comparison_report.schema.json
- Rename PR/RRP symbols and strings in:
  - examples/SAR/test/CMakeLists.txt
  - tools/sarpy/README.md
  - .github/workflows/sarpy-integration.yml
  - examples/SAR/src/sar_benchmark.cpp

4. Deletion list.
Delete artifacts that are intermediate-only or obsolete once renamed/replaced.

- Remove deprecated definitive variants after fold-in:
  - examples/SAR/config/sar_stripmap_definitive_metal.json
  - examples/SAR/config/sar_stripmap_definitive_nonmetal.json
- Remove tracked cache artifacts:
  - examples/SAR/tools/__pycache__/rrp1_scenario_to_run.cpython-313.pyc
  - examples/SAR/tools/__pycache__/rrp3_gotcha_back_adapter.cpython-313.pyc
- Intermediate-only test candidates to remove after capability replacements are authoritative:
  - examples/SAR/test/test_sar_pr2_fanout_json.cpp
  - examples/SAR/test/test_sar_pr2_token_contract.cpp
  - examples/SAR/test/test_sar_pr3_metal_json.cpp

5. Historical-material list.
Keep unchanged as historical planning material.

- plan/old
- plan/prompt examples
- plan/agents
- Planning reports:
  - plan/reviews/SAR_INSPECTOR_REPORT.md
  - plan/reviews/SAR_SIMPLIFIER_REPORT.md
  - plan/reviews/SAR_PLANNER_REPORT.md
- Implement/verify history files moved to plan/history/reviews remain exempt from lint token checks.

6. Replacement naming map.
- examples/SAR/test/test_pr13_sarpy_tools.cpp -> test_sarpy_reference_compare_tools.cpp
- examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp -> test_sarpy_crsd_validation_harness.cpp
- examples/SAR/test/test_pr16_graphx_crsd_lite_lane.cpp -> test_graphx_crsd_lite_lane.cpp
- examples/SAR/test/test_pr17_graphx_image_comparison_lane.cpp -> test_graphx_image_comparison_lane.cpp
- examples/SAR/test/test_pr18_local_gotcha_validation.cpp -> test_local_gotcha_validation_lane.cpp
- examples/SAR/test/test_rrp1_local_runner.cpp -> test_local_runner_contract.cpp
- examples/SAR/test/test_rrp2_scenario_image_path.cpp -> test_scenario_image_path_contract.cpp
- examples/SAR/test/test_rrp3_graphx_scenario_runner.cpp -> test_graphx_scenario_runner_contract.cpp
- examples/SAR/test/test_rrp3_gotcha_back_adapter.cpp -> test_gotcha_back_adapter_contract.cpp
- examples/SAR/test/test_rrp4_image_comparator.cpp -> test_image_comparator_contract.cpp
- examples/SAR/test/test_rrp5_comparator_metrics.cpp -> test_image_comparator_metrics.cpp
- examples/SAR/test/test_rrp5_frozen_scenario_replay.cpp -> test_frozen_scenario_replay.cpp
- examples/SAR/test/test_rrp6_ci_correctness_lane.cpp -> test_ci_correctness_lane.cpp
- examples/SAR/test/test_rrp6_tiny_fixture.cpp -> test_ci_tiny_fixture.cpp
- examples/SAR/test/test_rrp7_ci_validation_lane.cpp -> test_ci_validation_lane.cpp
- examples/SAR/test/test_rrp7_sarpy_harness.cpp -> test_sarpy_metadata_harness.cpp
- examples/SAR/test/sar_pr7_parity_fixture.hpp -> sar_reference_parity_fixture.hpp
- examples/SAR/tools/rrp1_local_runner.py -> sar_local_runner.py
- examples/SAR/tools/rrp1_scenario_to_run.py -> sar_scenario_to_run.py
- examples/SAR/tools/rrp3_gotcha_back_adapter.py -> gotcha_back_adapter.py
- examples/SAR/tools/rrp4_image_comparator.py -> sar_image_comparator.py
- examples/SAR/tools/rrp7_sarpy_harness.py -> sarpy_metadata_harness.py
- examples/SAR/tools/rrp4_image_comparison_report.schema.json -> sar_image_comparison_report.schema.json
- In examples/SAR/test/CMakeLists.txt, rename all SAR_PR and SAR_RRP definitions to SAR_CAPABILITY_ROLE naming.
- Rename all Pr/Rrp suite identifiers in source to capability suite names.

7. Tests to keep, rename, or delete.
- Keep unchanged:
  - examples/SAR/test/test_gotcha_input_ordering.cpp
  - examples/SAR/test/test_gotcha_mat_reader.cpp
  - examples/SAR/test/test_normalized_sar_product.cpp
  - examples/SAR/test/test_sar_product_validator.cpp
  - examples/SAR/test/test_graphx_crsd_lite_io.cpp
  - examples/SAR/test/test_crsd_io.cpp
  - examples/SAR/test/test_graphx_gotcha_to_crsd_cli.cpp
- Keep and rename:
  - all PR/RRP-named tests in section 3
- Delete:
  - intermediate-only PR config tests in section 4, once replacement coverage is active

8. Docs/reports to keep, rename, move, or delete.
- Keep and rename under tools/docs (final home remains tools):
  - examples/SAR/tools/rrp1_local_runner.md
  - examples/SAR/tools/rrp2_cpu_reference_backprojection.md
  - examples/SAR/tools/rrp5_frozen_scenario_replay.md
  - examples/SAR/tools/rrp7_sarpy_harness.md
  - tools/sarpy/README.md
- Keep product docs, de-PR language:
  - docs/sar/gotcha_crsd_repo_discovery.md
  - docs/sar/crsd_definition.md
- Move in one step:
  - all SAR_IMPL_PR* and SAR_VERIFY_PR* from plan/reviews to plan/history/reviews
- Keep unchanged as history:
  - plan/old, plan/prompt examples, plan/agents

9. Architecture invariants.
- Preserve final functionality:
  - GOTCHA sidecar generation
  - GOTCHA input ordering
  - normalized SAR product
  - SAR product validator
  - graphx-crsd-lite lane (NON-STANDARD, permanent)
  - real CRSD writer lane
  - SarPy validation harness
  - local-only GOTCHA workflow
  - GraphX image comparison lane
- Do not move local-only dataset assumptions into required CI.
- Do not redesign GraphX runtime contracts.
- Benchmark output naming may change freely.
- Enforce no PR/RRP tokens outside plan via CMake lint, with plan/history/reviews exempt.

10. Open questions that block planning.
- Confirm exact final retained non-PR config set in examples/SAR/config after definitive-variant fold-in.
- Confirm lint boundary for "all code, docs, scripts": include top-level docs like README and utility markdown under tools, or only SAR-scoped subtrees.
- Confirm whether CMake lint should scan file contents only, or also file names and CTest labels (recommended: both).
