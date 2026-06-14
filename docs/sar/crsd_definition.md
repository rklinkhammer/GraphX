# CRSD Definition And GOTCHA Mapping

## Scope

This document defines the target concepts for future GOTCHA-to-CRSD work in
GraphX. It is a documentation-only artifact. It does not add a MAT
reader, command-line tool, graphx-sar-normalized writer, CRSD writer, Python helper,
dependency, fixture, or test.

MATLAB is not used by this work and must not become a build-time, runtime, or
test-time dependency. GOTCHA `.mat` files must be read by C++ format readers in
later PRs, not by MATLAB.

GOTCHA is compensated phase history data. Because GraphX does not yet have a
standards-targeted CRSD writer, the first export path may be a single-channel
pseudo-CRSD representation or the permanent `graphx-sar-normalized` representation.
Any pseudo-CRSD output must be labeled as non-standard until a later PR adds and
validates a full CRSD writer.

## CRSD Overview

CRSD is a radar signal data product concept. It represents collected radar signal
data together with the metadata needed to interpret that signal data: collection
identity, transmit and receive timing, channel definitions, per-vector
parameters, support arrays, signal arrays, geometry, and antenna metadata.

For this project, CRSD is the standards-oriented export target. It is distinct
from GraphX internal SAR messages and distinct from the permanent
`graphx-sar-normalized` interchange format. GraphX internal structures may be shaped
for deterministic processing and testing; CRSD output must be shaped for product
interchange and external validation once the full writer exists.

## CRSD, CPHD, And SICD Placement

The SAR product levels relevant to this work are:

- CRSD: collected radar signal data with collection, channel, timing, signal,
  geometry, and antenna metadata.
- CPHD: compensated phase history data. It represents processed phase history
  samples before final image formation.
- SICD: complex image data. It represents focused image pixels and image-domain
  metadata.

GOTCHA is closest to the CPHD level because it is compensated phase history. The
conversion path therefore has to describe GOTCHA phase history in a CRSD-shaped
or graphx-sar-normalized-shaped product without pretending that the source is raw,
uncompensated collection signal data.

The intended data flow is:

```text
GOTCHA MAT data
  -> GraphX normalized GOTCHA pulse model
  -> graphx-sar-normalized or single-channel pseudo-CRSD
  -> standards-targeted CRSD only after the full CRSD writer and validation exist
```

SICD is not the output of the converter described here. SICD appears later only
as an image-domain comparison or validation target after image formation.

## CRSD Concept Model

### Collection Metadata

Collection metadata identifies the product and the collection context. Future
GraphX outputs should carry at least:

- product identifier
- collector or sensor identifier when known
- collection start and stop time when known
- scene center reference
- coordinate frame declaration
- source dataset identifier and provenance
- processing history and GraphX converter version

For GOTCHA, some collection fields may be unavailable from a tiny fixture or may
need to remain provisional until real-data inspection confirms the source MAT
layout. Missing fields must be represented explicitly in conversion reports
rather than silently invented.

### Timing

Timing metadata describes when signal vectors were collected or emitted in the
normalized stream. GraphX currently has `timestamp_us` in
`GotchaNormalizedPulseRecord`; future converters should map it into a stable
per-vector time basis.

When absolute collection time is unavailable, the converter should use a
relative timeline anchored at zero and report that the product has relative
timing. Time units must be explicit in every GraphX interchange format.

### Channel Metadata

Channel metadata describes each signal channel. A channel should define:

- channel identifier
- polarization
- carrier frequency
- bandwidth
- sample rate
- sample layout
- signal array reference
- PVP array reference
- support array references

The initial output may be single-channel because the existing normalized GOTCHA
fixture model exposes one stream of pulse records with a polarization string and
I/Q samples. Multi-channel support should be added only when source data and
tests justify it.

### Per-Vector Parameters

Per-vector parameters, or PVP, describe metadata that varies by signal vector.
For GOTCHA-to-GraphX conversion, likely PVP fields include:

- vector index
- pass identifier
- frame identifier
- pulse block identifier
- range bin start
- range bin count
- aperture span start
- aperture span count
- timestamp or relative time
- platform position
- platform velocity
- calibration gain
- calibration phase
- ordering key

PVP values should be deterministic and directly traceable to either source MAT
fields or GraphX normalized records.

### Support Arrays

Support arrays hold auxiliary numeric arrays that are needed to interpret the
signal data but are not themselves the primary signal array. Future support
arrays may include:

- platform position by vector
- platform velocity by vector
- scene center by vector or by collection
- calibration gain by vector
- calibration phase by vector
- validity masks or provenance flags when fields are derived or missing

Support arrays should not duplicate all PVP fields. They should be used when an
array is large, shared, or naturally consumed as a vector-valued numeric block.

### Signal Arrays

Signal arrays contain the complex radar samples. The current GraphX normalized
GOTCHA model uses `std::complex<float>` through `SarIqSample`, with a declared
sample layout string such as `interleaved_complex_f32`.

Future GraphX interchange and CRSD writers should preserve:

- vector order
- range sample order within each vector
- complex sample type
- byte order
- range bin origin
- sample rate and bandwidth association

If a CRSD-compatible writer requires a different numeric representation, that
conversion must be explicit and reported in `conversion_report.json`.

### Geometry

Geometry describes the relationship among sensor position, velocity, scene
center, and coordinate frame. The current normalized model includes:

- `platform_position_m`
- `platform_velocity_mps`
- `scene_center_m`
- `coordinate_frame`

Future conversion code must not infer high-precision geometry fields that are
not supported by source data. Derived geometry should be named, deterministic,
and listed in the conversion report.

### Antenna Concepts

Antenna metadata describes transmit and receive aperture behavior, phase center
assumptions, and beam or pattern information. The current normalized GOTCHA
fixture model does not expose a full antenna model.

Initial GraphX output should therefore use a minimal antenna declaration with a
clear "unknown", "derived", or "not modeled" status where appropriate. A later
full CRSD writer must either map real source antenna information or state which
CRSD antenna fields are unavailable for GOTCHA input.

## GOTCHA To GraphX To CRSD Mapping

The mapping has three layers:

1. GOTCHA source data.
2. GraphX normalized GOTCHA pulse records.
3. CRSD-shaped or graphx-sar-normalized product records.

The GraphX normalized layer is the contract between MAT ingestion and product
writing. It prevents C++ MAT reader details from leaking into CRSD writer logic.

| GOTCHA concept | GraphX normalized field | CRSD or SAR-normalized target |
| --- | --- | --- |
| pass | `pass_id` | collection grouping or channel grouping metadata |
| frame | `frame_id` | collection segment or product provenance metadata |
| pulse block | `pulse_block_id` | PVP vector grouping |
| range origin | `range_bin_start` | PVP range offset |
| range sample count | `range_bin_count` | signal vector length and PVP count |
| aperture span | `aperture_span_start`, `aperture_span_count` | PVP aperture metadata |
| pulse time | `timestamp_us` | PVP time or relative vector time |
| deterministic ordering | `ordering_key` | vector ordering and index validation |
| stream | `stream_id` | channel identifier or stream provenance |
| backend provenance | `backend_id`, `backend` | GraphX processing provenance only |
| platform position | `platform_position_m` | geometry support array or PVP |
| platform velocity | `platform_velocity_mps` | geometry support array or PVP |
| scene center | `scene_center_m` | collection and geometry metadata |
| carrier | `carrier_hz` | channel RF metadata |
| bandwidth | `bandwidth_hz` | channel RF metadata |
| sample rate | `sample_rate_hz` | signal sampling metadata |
| calibration gain | `calibration_gain` | calibration support array or PVP |
| calibration phase | `calibration_phase_rad` | calibration support array or PVP |
| polarization | `polarization` | channel polarization metadata |
| coordinate frame | `coordinate_frame` | geometry coordinate frame declaration |
| sample layout | `sample_layout` | signal array encoding metadata |
| byte order | `endianness` | signal array byte order metadata |
| I/Q data | `iq_samples` | signal array payload |

`SarSidecar` and `SarAccelControlToken` are runtime GraphX transport and
identity structures. They should not become the file format model. Future
writers may copy relevant sidecar provenance into reports, but CRSD identity and
signal metadata should come from the normalized product model.

## `gotcha_crsd_index.json` Schema

`gotcha_crsd_index.json` is the deterministic index for a converted product. It
is intended to let tests and tools locate product parts without parsing the full
signal payload first.

The schema below is normative for future GraphX-produced JSON unless a later PR
updates this document.

```json
{
  "schema": "graphx.gotcha_crsd_index.v1",
  "product_id": "string",
  "created_by": "graphx",
  "source": {
    "kind": "gotcha",
    "path": "string",
    "sha256": "hex-string-or-empty",
    "mat_format": "mat73|classic|unknown",
    "mat_reader": "hdf5|classic|unsupported|unknown"
  },
  "output": {
    "kind": "graphx-sar-normalized|pseudo-crsd|crsd",
    "root": "string",
    "metadata_path": "string",
    "signal_path": "string",
    "pvp_path": "string",
    "support_paths": ["string"],
    "report_path": "string"
  },
  "collection": {
    "collector": "string",
    "coordinate_frame": "string",
    "scene_center_m": [0.0, 0.0, 0.0],
    "time_basis": "absolute|relative|unknown"
  },
  "channels": [
    {
      "channel_id": "string",
      "polarization": "string",
      "carrier_hz": 0.0,
      "bandwidth_hz": 0.0,
      "sample_rate_hz": 0.0,
      "vector_count": 0,
      "samples_per_vector": 0,
      "signal_array": "string",
      "pvp_array": "string",
      "support_arrays": ["string"]
    }
  ],
  "chunks": [
    {
      "chunk_id": "string",
      "channel_id": "string",
      "first_vector": 0,
      "vector_count": 0,
      "signal_path": "string",
      "sha256": "hex-string"
    }
  ]
}
```

Required top-level fields are `schema`, `product_id`, `source`, `output`,
`collection`, `channels`, and `chunks`. Paths must be relative to the product
root unless explicitly documented otherwise. Numeric values must use SI units
where possible.

## `conversion_report.json` Schema

`conversion_report.json` records what the converter did, what it could not do,
and which assumptions were used. It is intended for verification and debugging,
not for product consumption.

```json
{
  "schema": "graphx.gotcha_conversion_report.v1",
  "product_id": "string",
  "status": "success|partial|failed",
  "input": {
    "path": "string",
    "sha256": "hex-string-or-empty",
    "mat_format": "mat73|classic|unknown",
    "reader": "hdf5|classic|unsupported|unknown"
  },
  "output": {
    "kind": "graphx-sar-normalized|pseudo-crsd|crsd",
    "root": "string",
    "index_path": "string"
  },
  "counts": {
    "passes": 0,
    "frames": 0,
    "channels": 0,
    "vectors": 0,
    "samples": 0,
    "chunks": 0
  },
  "assumptions": [
    {
      "field": "string",
      "value": "string",
      "reason": "string"
    }
  ],
  "derived_fields": [
    {
      "field": "string",
      "method": "string",
      "source_fields": ["string"]
    }
  ],
  "missing_fields": [
    {
      "field": "string",
      "impact": "string"
    }
  ],
  "warnings": ["string"],
  "errors": ["string"],
  "checksums": [
    {
      "path": "string",
      "sha256": "hex-string"
    }
  ]
}
```

Required top-level fields are `schema`, `product_id`, `status`, `input`,
`output`, `counts`, `assumptions`, `derived_fields`, `missing_fields`,
`warnings`, `errors`, and `checksums`.

For pseudo-CRSD or graphx-sar-normalized outputs, the report must state that the
output is not a standards-validated CRSD product. For full CRSD output, the
report must name the validation method and validation result.

## Permanent SAR-Normalized Boundary

`graphx-sar-normalized` is a permanent GraphX interchange representation. It is not a
temporary debug dump and it is not a claim of CRSD compliance.

SAR-normalized output may use the same conceptual model as CRSD, including collection,
channel, PVP, support, and signal arrays, but it may omit standard-specific XML
structure, binary packing, naming, or validation requirements until a later full
CRSD writer PR owns them.

Future code must keep these outputs distinct:

- `graphx-sar-normalized`: permanent GraphX interchange.
- `pseudo-crsd`: a transitional, clearly labeled CRSD-shaped product.
- `crsd`: standards-targeted output produced only after full writer and
  validation support exists.

## Open Mapping Risks

The following fields may remain provisional until real GOTCHA MAT inspection and
validation are implemented:

- absolute collection time
- complete antenna metadata
- exact channelization for multi-channel input
- coordinate frame interpretation
- calibration term semantics
- standards-complete CRSD metadata required beyond the normalized GraphX model

Future implementation PRs must surface unresolved fields through
`conversion_report.json` rather than silently filling them with plausible-looking
defaults.
