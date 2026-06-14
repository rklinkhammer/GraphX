# LSD Convert Report

Date: 2026-06-13
Dataset: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData

## Generated JSON Artifacts

The following files were generated in the dataset directory:

- manifest.json
- checksums.sha256
- subData01.mat.json
- subData02.mat.json
- subData03.mat.json
- subData04.mat.json
- subData05.mat.json
- subData06.mat.json
- subData07.mat.json
- subData08.mat.json
- subData09.mat.json
- subData10.mat.json

## Added Scripts

Repository scripts/tools created for this conversion flow:

- tools/sarpy/generate_gotcha_subdata_sidecars.py
- scripts/prepare_gotcha_subdata_json.sh
- scripts/convert_gotcha_subdata_to_crsd.sh
- scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh

## Commands

### 1) Generate sidecars + manifest + checksums

```bash
bash scripts/prepare_gotcha_subdata_json.sh /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData
```

### 2) Attempt CRSD conversion

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
```

### 3) Run working conversion now (graphx-crsd-lite)

```bash
bash scripts/convert_gotcha_subdata_to_graphx_crsd_lite.sh \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_graphx_crsd_lite_output
```

## Observed Results

- JSON artifact generation: successful.
- graphx-crsd-lite conversion: successful.
- CRSD conversion mode: currently fails fast with a clear writer-unavailable message.

## Output Locations

- graphx-crsd-lite output:
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_graphx_crsd_lite_output
- CRSD attempt output directory:
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
