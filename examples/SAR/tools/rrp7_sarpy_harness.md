# RRP7 SarPy Product Metadata Harness

This harness is local-only.

It does not run in normal CI, does not download data, and does not change GraphX core SAR contracts.

## Scope

The harness exists to normalize SarPy-side product or metadata summaries into a GraphX-compatible contract for later artifact-level validation.

It is not proof of GraphX phase-history image-formation correctness.

## Local-only rules

- SarPy installation is optional and local/manual only.
- Manual dataset paths are acceptable.
- No SarPy imports are required by GraphX core build or normal SAR unit lanes.
- No large datasets are committed.

## Commands

Probe the local environment:

```bash
python3 examples/SAR/tools/rrp7_sarpy_harness.py \
  probe-environment \
  --output-json /tmp/graphx_rrp7_sarpy_probe.json
```

Normalize SarPy-side metadata JSON into a GraphX-compatible contract:

```bash
python3 examples/SAR/tools/rrp7_sarpy_harness.py \
  normalize-metadata \
  --scenario examples/SAR/scenarios/scenario_001.json \
  --input-json /path/to/local/sarpy_metadata.json \
  --output-json /tmp/graphx_rrp7_sarpy_contract.json
```

## Expected input JSON for normalization

The local/manual SarPy-side JSON should contain at least:

- `source_tool`
- `product_type`
- `source_product_path`
- `metadata_fields`

## Boundary statement

This harness validates product and metadata contracts only.
In other words, this is product/metadata validation only.
It must not define GraphX node, token, resolver, or sidecar contracts.