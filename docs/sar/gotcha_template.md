# GOTCHA Workflow Dispatch Template

This document provides concrete input values for running the SarPy integration workflow against:

- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-1`
- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-2`

The workflow currently accepts one dataset root per run, so run it twice.

## Workflow Inputs

Workflow file: `.github/workflows/sarpy-integration.yml`

Required inputs:

- `gotchaDatasetPath`
- `crsdFilePath`

Optional inputs (can be blank):

- `gotchaManifestPath`
- `gotchaChecksumsPath`

If optional inputs are blank, workflow defaults are used:

- `<gotchaDatasetPath>/manifest.json`
- `<gotchaDatasetPath>/checksums.sha256`

## Run A: Disk 1

```yaml
gotchaDatasetPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-1
gotchaManifestPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-1/manifest.json
gotchaChecksumsPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-1/checksums.sha256
crsdFilePath: /absolute/path/to/your/file.crsd
```

## Run B: Disk 2

```yaml
gotchaDatasetPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-2
gotchaManifestPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-2/manifest.json
gotchaChecksumsPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-2/checksums.sha256
crsdFilePath: /absolute/path/to/your/file.crsd
```

## Optional Minimal Inputs

You can leave optional paths blank and rely on defaults.

### Disk 1 minimal

```yaml
gotchaDatasetPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-1
gotchaManifestPath: ""
gotchaChecksumsPath: ""
crsdFilePath: /absolute/path/to/your/file.crsd
```

### Disk 2 minimal

```yaml
gotchaDatasetPath: /Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data-Disk-2
gotchaManifestPath: ""
gotchaChecksumsPath: ""
crsdFilePath: /absolute/path/to/your/file.crsd
```

## If You Need Both Disks In One Run

Current workflow does not accept two dataset roots in one dispatch input set. Use one of these approaches:

1. Trigger two workflow runs (recommended).
2. Add a matrix strategy with two dataset paths.
3. Provide a merged mount/symlink directory and pass that as `gotchaDatasetPath`.

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Input:
- docs/sar/gotcha_large_scene_data_description.md
- docs/sar/crsd_definition.md
- docs/sar/gotcha_crsd_repo_discovery.md
- Current repository state

Goal:
Create a small, reviewable PR plan that addresses the GOTCHA Large Scene Data Description issues and moves the project from partial/subset GOTCHA handling toward correct full-aperture GOTCHA-to-CRSD conversion.

Do not implement code.
Do not redesign GraphX runtime contracts.
Stop after the planner report.

Planning rules:
- MATLAB is not used and must not become a build/runtime/test dependency.
- The GOTCHA source data is processed phase history, not raw collection data.
- The ten `subData*.mat` files are intended to concatenate into one single aperture.
- A correct full-aperture path must preserve all `Np` pulses from each file, not only one selected pulse per file.
- `phdata` is the signal source.
- `K`, `deltaF`, and `minF` must drive waveform/frequency/sample metadata.
- `AntX`, `AntY`, `AntZ`, and `R0` must drive geometry/reference-range/PVP or support-array mapping.
- The local Cartesian coordinate frame with scene center origin must be documented and preserved or explicitly mapped.
- Missing CRSD metadata must be handled honestly: derived, supplied externally, or marked unknown/not modeled.
- Backward compatibility is not required.
- Complexity is a defect.
- Prefer explicit validation/reporting over silent assumptions.
- Keep local-only real GOTCHA workflows optional and out of CI.
- Each PR must compile and test independently.

Required planning coverage:
1. Field inventory validation for required GOTCHA fields:
   - `Np`
   - `K`
   - `deltaF`
   - `minF`
   - `AntX`
   - `AntY`
   - `AntZ`
   - `R0`
   - `phdata`
2. Verification of actual `phdata` shape/orientation and deterministic reporting of `K x Np` versus `Np x K`.
3. Full-pulse ingestion design so every pulse in every MAT file can enter the normalized product.
4. Single-aperture concatenation across `subData01.mat` through `subData10.mat`.
5. Manifest/lexical ordering rules for the ten-file aperture.
6. Normalized product model adjustments, if needed, to preserve full-aperture pulse metadata.
7. CRSD/lite metadata mapping for:
   - frequency axis
   - bandwidth
   - center/carrier frequency
   - sample count
   - antenna phase-center positions
   - `R0`
   - local Cartesian scene-center frame
8. Conversion report updates that clearly state when a subset of pulses was used versus full-aperture conversion.
9. Validator updates for pulse counts, shape consistency, frequency metadata, and geometry completeness.
10. Tests using tiny synthetic fixtures that model multiple files and multiple pulses per file.
11. Local-only validation against the real `/subData` directory, gated by environment variables.
12. Documentation updates that cite `docs/sar/gotcha_large_scene_data_description.md` as the dataset reference.
13. Any blockers for true standards-compliant CRSD caused by missing metadata such as polarization, antenna pattern, absolute collection time, geodetic scene center, velocity, or calibration terms.

Required output:
For each planned PR provide:
- title
- purpose
- files to touch
- files to delete
- tests to add
- tests to delete
- acceptance criteria
- risks
- rollback plan
- whether it is CI-safe or local-only

Output:
Save the planner report to:

plan/reviews/SAR_GOTCHA_FULL_APERTURE_PLANNER_REPORT.md