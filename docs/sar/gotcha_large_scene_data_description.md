# GOTCHA Large Scene Data Description Summary

Source reviewed:

- `/Users/rklinkhammer/workspace/Gotcha-Large-Scene-Data/subData/Data_Description.pdf`

## Summary

The PDF describes the "Large Scene Gotcha Data Example" dataset. The dataset
contains ten MATLAB files named `subData01.mat` through `subData10.mat`, with a
combined size of about 5.9 GB.

Each file contains the same data fields:

| Field | Meaning |
| --- | --- |
| `Np` | Number of pulses in the file |
| `K` | Number of data samples per pulse |
| `deltaF` | Frequency step size between samples, in hertz |
| `minF` | Frequency of the first sample, in hertz |
| `AntX` | Radar antenna phase-center x position relative to scene center, in meters |
| `AntY` | Radar antenna phase-center y position relative to scene center, in meters |
| `AntZ` | Radar antenna phase-center z position relative to scene center, in meters |
| `R0` | Distance from radar antenna phase center to scene center, in meters |
| `phdata` | Processed radar phase-history data array |

The antenna phase-center coordinates are in a local Cartesian coordinate system
whose origin is the scene center. The PDF text spells the z-coordinate field as
`AntZ` in the field list and later as `antZ` in prose; the implementation should
treat `AntZ` as the authoritative field name unless field inventory proves
otherwise.

The ten files may be concatenated to form one single aperture. The PDF states
that this combined aperture produces a SAR image with roughly one-foot azimuth
resolution.

## Project Implications

The project should account for the following dataset facts:

- The source is processed phase history, not raw radar collection data.
- `phdata` is the signal array source for GOTCHA ingestion.
- `Np` pulses per file should be preserved. A full-aperture conversion should
  ingest all pulses from all ten files, not only one selected pulse from each
  file.
- Lexical ordering of `subData01.mat` through `subData10.mat` is meaningful for
  the single-aperture concatenation unless an explicit manifest overrides it.
- `K`, `deltaF`, and `minF` define the frequency/sample axis and should drive
  normalized waveform metadata, bandwidth, center/carrier-frequency derivation,
  and CRSD signal metadata.
- `AntX`, `AntY`, `AntZ`, and `R0` provide the geometry needed for platform or
  phase-center position metadata, reference range, and PVP/support-array
  population.
- The coordinate frame is local Cartesian with scene center as origin. CRSD
  output should either preserve that clearly as local/derived geometry or map it
  through a documented geodetic reference if one is later introduced.
- The PDF does not document polarization, antenna pattern, absolute collection
  time, geodetic scene center, platform velocity, transmit waveform details, or
  calibration terms beyond the listed fields. Those CRSD fields must therefore
  be derived, marked unknown/not modeled, or supplied by an additional source.

## Follow-Up Work To Consider

- Update GOTCHA ingestion so the normalized product can represent every pulse in
  each MAT file and then concatenate all ten files into a single aperture.
- Verify the actual `phdata` shape and orientation for each file and document
  whether samples are stored as `K x Np`, `Np x K`, or another layout.
- Add field-inventory checks that require or report `Np`, `K`, `deltaF`,
  `minF`, `AntX`, `AntY`, `AntZ`, `R0`, and `phdata`.
- Ensure conversion reports state when only a subset of pulses is used.
- Use `R0` explicitly in reference geometry and PVP mapping rather than
  replacing it with an inferred scene-center range.
- Keep MATLAB out of the build, runtime, and test dependency chain. The PDF
  documents MATLAB file content, but it does not require MATLAB execution.

