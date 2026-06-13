# GraphX PR13 SarPy Tools (Local-Only)

This directory contains local-only Python reference and comparison tooling.

Scope boundaries:

- These scripts are for reference/comparison workflows only.
- They are not GraphX runtime dependencies.
- MATLAB is not required.
- SarPy CRSD validation is out of scope for PR13.

## Setup

```bash
python3 -m pip install -r tools/sarpy/requirements.txt
```

## 1) Field Discovery

```bash
python3 tools/sarpy/reference_image_from_gotcha.py \
  discover-fields \
  --input-mat /path/to/file.mat \
  --output-json /tmp/gotcha_field_inventory.json
```

## 2) Reference Image Generation

Generate a complex reference image from a local JSON fixture with `real` and `imag` 2D arrays.

```bash
python3 tools/sarpy/reference_image_from_gotcha.py \
  generate-reference \
  --input-json /path/to/complex_fixture.json \
  --output-reference-npy /tmp/reference_image.npy \
  --output-magnitude-png /tmp/reference_magnitude.png \
  --output-metadata-json /tmp/reference_metadata.json
```

## 3) Deterministic Image Metrics

```bash
python3 tools/sarpy/compare_images.py \
  compare \
  --reference-npy /tmp/reference_image.npy \
  --candidate-npy /tmp/candidate_image.npy \
  --output-report-json /tmp/comparison_report.json \
  --output-diff-magnitude-png /tmp/difference_magnitude.png \
  --output-phase-difference-png /tmp/phase_difference.png
```

## Environment Probe

```bash
python3 tools/sarpy/reference_image_from_gotcha.py probe-environment --output-json /tmp/ref_probe.json
python3 tools/sarpy/compare_images.py probe-environment --output-json /tmp/cmp_probe.json
python3 tools/sarpy/validate_crsd.py probe-environment --output-json /tmp/crsd_validate_probe.json
python3 tools/sarpy/reference_image_from_crsd.py probe-environment --output-json /tmp/crsd_ref_probe.json
```

## 4) CRSD Validation Harness (PR14, local-only)

```bash
python3 tools/sarpy/validate_crsd.py \
  validate \
  --input-crsd /path/to/local/file.crsd \
  --output-json /tmp/crsd_validation_report.json
```

The validation JSON includes best-effort CRSD metadata fields:

- CRSD version
- dimensions
- dtype
- sample slices preview
- PVP array summary
- validation status and errors

## 5) Reference Magnitude Image From CRSD (PR14, local-only)

```bash
python3 tools/sarpy/reference_image_from_crsd.py \
  generate-reference \
  --input-crsd /path/to/local/file.crsd \
  --output-magnitude-png /tmp/crsd_reference_magnitude.png \
  --output-metadata-json /tmp/crsd_reference_metadata.json
```

## Scope Boundary For PR14

- No CRSD writer is implemented here.
- SarPy remains local-only tooling and not a GraphX runtime dependency.
