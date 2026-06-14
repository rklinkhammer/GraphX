> ARCHIVAL STATUS (2026-06-14): Historical conversion snapshot. Some steps reference legacy sidecar-era preparation artifacts and are not required for the current CRSD-only operational lane.

# CRSD Convert Report

Date: 2026-06-13

Dataset:

`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData`

Output:

`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output`

## Generated JSON Artifacts

The GOTCHA dataset directory was refreshed with deterministic sidecar and ordering artifacts:

- `manifest.json`
- `checksums.sha256`
- `subData01.mat.json`
- `subData02.mat.json`
- `subData03.mat.json`
- `subData04.mat.json`
- `subData05.mat.json`
- `subData06.mat.json`
- `subData07.mat.json`
- `subData08.mat.json`
- `subData09.mat.json`
- `subData10.mat.json`

## Repository Scripts

The conversion flow is driven by:

- `tools/sarpy/write_crsd_from_graphx_product.py`
- `scripts/convert_gotcha_subdata_to_crsd.sh`

## Commands

Convert GOTCHA MAT inputs to CRSD:

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
```

## Observed Results

- MAT files discovered: 10
- CRSD conversion status: successful
- Conversion mode: `crsd`
- CRSD chunks: 1
- SarPy validation: ok
- Output dimensions: `10 x 21232`
- Signal array format: `CF8`

## CRSD Output Files

Primary CRSD product:

`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd`

Companion artifacts:

- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_index.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/conversion_report.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/conversion_warnings.log`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/metadata.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/pvp.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/provenance.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/chunk_index.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/sarpy_validation/sarpy_crsd_validation_report.json`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/sarpy_validation/sarpy_probe.json`

## Validation Evidence

The generated CRSD product was opened by SarPy with status `ok`.

Validation report:

`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/gotcha_crsd_chunk_0000.crsd/sarpy_validation/sarpy_crsd_validation_report.json`

Conversion report:

`/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output/conversion_report.json`

Observed conversion report summary:

- `format`: `crsd`
- `label`: `STANDARDS-TARGETED`
- `validation_status`: `ok`
- `warnings`: `sarpy_validation_ok`
- `checksum_fnv1a64`: `0xb914e70554682df6`

## Current Scope Note

The current sidecar-backed reader extracts one selected pulse per `.mat` file. The generated product used the default `PULSE_INDEX=0`, producing 10 CRSD vectors from the 10 GOTCHA MAT files. MATLAB is not used by the GraphX converter.
