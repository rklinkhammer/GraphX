# RRP5 Frozen Scenario Replay Guide

This guide documents the exact local steps for replaying `scenario_001` without having to infer the script structure from the implementation files.

## Purpose

Replay the frozen SAR scenario locally, capture one GraphX image artifact, capture the pinned gotcha-back reference artifact, and compare the two using the deterministic RRP4 comparator.

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

2. Prepare the frozen scenario layout:

```bash
python3 examples/SAR/tools/rrp1_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_rrp1_scenario_001
```

3. Update the generated GraphX config before running GraphX:

- Open `/tmp/graphx_rrp1_scenario_001/graphx/graphx_config.json`
- Set the source fixture path to the local GOTCHA replay fixture
- Keep the generated `SarMaterializedImageSinkNode` materialization path intact

4. Run the GraphX side of the replay:

```bash
/tmp/graphx_rrp1_scenario_001/graphx/run_graphx.sh
```

5. Run the pinned gotcha-back reference side:

```bash
/tmp/graphx_rrp1_scenario_001/reference/run_gotcha_back.sh
```

6. Normalize the reference output if needed:

```bash
python3 examples/SAR/tools/rrp3_gotcha_back_adapter.py \
  normalize-output \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --input-raw /tmp/graphx_rrp1_scenario_001/reference/gotcha_back_image.bin \
  --output-json /tmp/graphx_rrp1_scenario_001/reference/normalized_reference.json
```

7. Compare GraphX and reference artifacts:

```bash
python3 examples/SAR/tools/rrp4_image_comparator.py \
  compare \
  --graphx-contract /tmp/graphx_rrp1_scenario_001/graphx/graphx_output_contract.json \
  --reference-contract /tmp/graphx_rrp1_scenario_001/reference/reference_output_contract.json \
  --report-json /tmp/graphx_rrp1_scenario_001/reports/image_comparison_report.json
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