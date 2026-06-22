# Local GOTCHA Reproduction Runner

This local-only harness consumes `scenario_001` and prepares the GraphX and external reference execution boundaries.

## Command

```bash
python3 examples/SAR/tools/sar_local_runner.py \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --output-dir /tmp/graphx_sar_scenario_001
```

## What It Does

- validates the frozen scenario manifest
- creates a stable artifact layout
- copies the scenario manifest into the output
- generates a GraphX config scaffold from `sar_gotcha_external_manual.json`
- writes a GraphX boundary script
- writes a pinned gotcha-back invocation spec and boundary script
- writes a reference output contract for normalization
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
    gotcha_back_invocation.json
    reference_output_contract.json
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

Before any local/manual run, replace the `fixture_path` placeholder in `graphx/graphx_config.json`, set `GOTCHA_DIR` to the unpacked GOTCHA dataset, and set `GOTCHA_BACK_BIN` to the gotcha-back `sarbp` executable used by the pinned reference script.
