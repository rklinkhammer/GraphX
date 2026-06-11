# RRP1 Local GOTCHA Reproduction Runner

RRP1 adds the smallest local-only harness that consumes `scenario_001` and prepares the GraphX and external reference execution boundaries.

## Command

```bash
python3 examples/SAR/tools/rrp1_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_rrp1_scenario_001
```

## What It Does

- validates the frozen scenario manifest
- creates a stable artifact layout
- copies the scenario manifest into the output
- generates a GraphX config scaffold from `sar_gotcha_external_manual.json`
- writes a GraphX boundary script
- writes a gotcha-back boundary script
- writes an orchestration plan JSON

## Output Layout

```text
<output-dir>/
  manifest/
    scenario_001.json
  graphx/
    graphx_config.json
    run_graphx.sh
  reference/
    run_gotcha_back.sh
  reports/
    orchestration_plan.json
```

## Scope Boundaries

- does not download external data
- does not clone gotcha-back
- does not run GraphX automatically
- does not run gotcha-back automatically
- does not compare images
- does not modify SAR math or accel-token architecture

## Manual Follow-Up

Before any local/manual run, replace the `fixture_path` placeholder in `graphx/graphx_config.json` with a local normalized GOTCHA replay file and provide the local gotcha-back binary/dataset path for the reference boundary.