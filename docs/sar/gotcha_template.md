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
