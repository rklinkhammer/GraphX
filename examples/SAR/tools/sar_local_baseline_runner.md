# SAR Local Baseline Runner

This tool is local-only.

It does not run by default in CI, does not add external dependencies to GraphX runtime, and does not change GraphX SAR contracts.

## Selected Baseline

PR14 selects SarPy as the local-only baseline package for runner integration.

## Gating

- Opt-in is required: `GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1`
- Local dataset path is required for smoke execution: `GRAPHX_SARPY_CRSD_FILE=/path/to/product.crsd`

If these conditions are not met, the runner emits deterministic skip diagnostics.

## Commands

Probe local environment:

```bash
python3 examples/SAR/tools/sar_local_baseline_runner.py \
  probe-environment \
  --output-json /tmp/graphx_sar_local_baseline_probe.json
```

Run local-only smoke path:

```bash
GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1 \
GRAPHX_SARPY_CRSD_FILE=/path/to/local/product.crsd \
python3 examples/SAR/tools/sar_local_baseline_runner.py \
  run-local-smoke \
  --output-json /tmp/graphx_sar_local_baseline_smoke.json
```

## Boundary Statement

This runner is for local/manual baseline checks only.
It must not be treated as a GraphX runtime dependency or required by default CI.
