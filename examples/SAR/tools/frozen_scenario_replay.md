# Frozen Scenario Replay Guide

This guide documents the exact local steps for replaying `scenario_001` without having to infer the script structure from the implementation files.

## CI-Safe Local Replay Command Path

The following sequence materializes both artifact contracts and runs the comparator with structured pass/fail output. **No external data download is required.**

```bash
# 1. Scaffold the runner layout from the frozen scenario
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_ci_safe_replay

# 2. Inject the CI-safe tiny fixture into the scaffolded config
#    (edit /tmp/graphx_sar_ci_safe_replay/graphx/graphx_config.json:
#     set src.node_config.fixture_path to the tiny fixture path)
#    The tiny fixture is at:
#    examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json

# 3. Run GraphX with the patched config and capture the materialized image
#    (handled by the C++ executor in the test suite below)

# 4. Write graphx_output_contract.json and deterministic_reference_contract.json
#    (handled by the test suite: test_sar_example_unit --gtest_filter=*FrozenScenarioReplayTest.CiSafeLocalReplayChainProducesArtifactsAndPassesComparator)

# 5. Run the comparator directly against the produced contracts:
python3 examples/SAR/tools/sar_image_comparator.py \
  compare \
  --graphx-contract /tmp/graphx_sar_ci_safe_replay/graphx/graphx_output_contract.json \
  --reference-contract /tmp/graphx_sar_ci_safe_replay/reference/deterministic_reference_contract.json \
  --report-json /tmp/graphx_sar_ci_safe_replay/reports/ci_safe_comparison_report.json
```

The comparison report at `reports/ci_safe_comparison_report.json` contains:

- `"verdict": "pass"` or `"verdict": "fail"`
- Pixel metrics: `l_inf`, `rms`, `relative_l2`
- Per-check breakdown: source_tool, scenario_id, format, layout, dimensions, byte_count, pixel_count

The full CI-safe integration test (`*FrozenScenarioReplayTest.CiSafeLocalReplayChainProducesArtifactsAndPassesComparator`) exercises this entire chain automatically from the test suite:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='*FrozenScenarioReplayTest.CiSafeLocalReplayChainProducesArtifactsAndPassesComparator' -v
```

## Purpose

Replay the frozen SAR scenario locally, capture one GraphX image artifact, capture the pinned gotcha-back reference artifact, and compare the two using the deterministic image comparator.

## Prerequisites

- A configured GraphX build tree with the SAR example target available.
- The GOTCHA dataset unpacked locally.
- A gotcha-back `sarbp` executable available locally.
- Python 3 available on the PATH.

## Exact Local Setup

1. Export the local reference inputs:

```bash
export GOTCHA_DIR=/path/to/unpacked/GOTCHA
export GOTCHA_BACK_BIN=/path/to/gotcha-back/sarbp
```

1. Prepare the frozen scenario layout:

```bash
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_scenario_001
```

1. Update the generated GraphX config before running GraphX:

- Open `/tmp/graphx_sar_scenario_001/graphx/graphx_config.json`
- Set the source fixture path to the local GOTCHA replay fixture
- Keep the generated `SarMaterializedImageSinkNode` materialization path intact

1. Run the GraphX side of the replay:

```bash
/tmp/graphx_sar_scenario_001/graphx/run_graphx.sh
```

1. Run the pinned gotcha-back reference side:

```bash
/tmp/graphx_sar_scenario_001/reference/run_gotcha_back.sh
```

1. Normalize the reference output if needed:

```bash
python3 examples/SAR/tools/gotcha_back_adapter.py \
  normalize-output \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --input-raw /tmp/graphx_sar_scenario_001/reference/gotcha_back_image.bin \
  --output-json /tmp/graphx_sar_scenario_001/reference/normalized_reference.json
```

1. Compare GraphX and reference artifacts:

```bash
python3 examples/SAR/tools/sar_image_comparator.py \
  compare \
  --graphx-contract /tmp/graphx_sar_scenario_001/graphx/graphx_output_contract.json \
  --reference-contract /tmp/graphx_sar_scenario_001/reference/reference_output_contract.json \
  --report-json /tmp/graphx_sar_scenario_001/reports/image_comparison_report.json
```

## Artifact Layout

```text
<output-dir>/
  manifest/
    scenario_001.json
  graphx/
    graphx_config.json
    run_graphx.sh
    graphx_output_contract.json
  reference/
    gotcha_back_invocation.json
    reference_output_contract.json
    run_gotcha_back.sh
    gotcha_back_image.bin
    normalized_reference.json
  reports/
    orchestration_plan.json
    image_comparison_report.json
```

## Replay Expectations

- `scenario_001.json` is immutable and defines the frozen reproduction target.
- In other words, `scenario_001.json is immutable` and should not be edited for local replay setup.
- `run_graphx.sh` does not auto-edit the fixture path; it only prints the exact GraphX invocation boundary.
- `run_gotcha_back.sh` is pinned to the same scenario profile every time.
- The normalized reference contract and GraphX output contract must describe the same scenario, image dimensions, format, layout, and artifact kind.
- The comparison report should be deterministic: matching artifacts produce `pass`; mismatched pixels produce `fail`.

## Scope Boundaries

- does not download external data
- does not clone gotcha-back
- does not change SAR math
- does not alter accel-token architecture
- does not introduce a CI dependency on GOTCHA data
