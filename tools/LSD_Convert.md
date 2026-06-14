> ARCHIVAL STATUS (2026-06-14): Historical conversion snapshot. Some steps reference legacy sidecar-era preparation artifacts and are not required for the current CRSD-only operational lane.

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

- scripts/convert_gotcha_subdata_to_crsd.sh

## Commands

### 1) Attempt CRSD conversion

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
```

### 2) Run conversion to CRSD

```bash
bash scripts/convert_gotcha_subdata_to_crsd.sh \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData \
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
```

## Observed Results

- JSON artifact generation: successful.
- CRSD conversion: successful.

## Output Locations

- CRSD output directory:
  /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData_crsd_output
