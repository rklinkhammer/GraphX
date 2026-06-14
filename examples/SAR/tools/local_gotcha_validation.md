# Local-Only Real GOTCHA Validation

This workflow validates a local GOTCHA `.mat` directory through the existing
`graphx-gotcha-to-crsd` `graphx-sar-normalized` lane.

It is explicitly local-only:

- No dataset download is performed.
- No GOTCHA data is checked into the repository.
- CI does not require this workflow.
- Full CRSD validation is not part of this lane.
- MATLAB is not required.

## Required Environment

```bash
export GRAPHX_SAR_GOTCHA_DATASET=/path/to/local/gotcha_mat_directory
```

The dataset directory must contain top-level `.mat` files and the preflight
artifacts expected by `scripts/verify_gotcha_dataset.sh`:

```text
manifest.json
checksums.sha256
```

Override these paths when needed:

```bash
export GRAPHX_SAR_GOTCHA_MANIFEST=/path/to/manifest.json
export GRAPHX_SAR_GOTCHA_CHECKSUMS=/path/to/checksums.sha256
```

## Optional Environment

```bash
export GRAPHX_SAR_GOTCHA_TO_CRSD_BIN=/path/to/graphx-gotcha-to-crsd
export GRAPHX_SAR_GOTCHA_OUTPUT_DIR=/tmp/graphx_sar_real_gotcha_validation
export GRAPHX_SAR_GOTCHA_COLLECTION_ID=local-real-gotcha
export GRAPHX_SAR_GOTCHA_MAX_OUTPUT_SIZE_MB=512
```

## Run

```bash
bash examples/SAR/tools/local_gotcha_validation.sh
```

Expected outputs in `GRAPHX_SAR_GOTCHA_OUTPUT_DIR`:

```text
gotcha_sar_normalized_index.json
conversion_report.json
conversion_warnings.log
gotcha_sar_normalized_chunk_*.graphx-sar-normalized/
```

The output format is `graphx-sar-normalized`, a permanent non-standard GraphX
intermediate format.
