# CRSD To Focused Image Flow And Guardrails

## Scope

This document defines the accepted CRSD-to-focused-image path in GraphX,
including execution boundaries and required evidence.

This is a documentation contract only. It does not add runtime behavior.

## Canonical Boundary

GraphX SAR operations are split into three lanes:

1. GOTCHA to CRSD conversion
2. CRSD quick-look validation (local-only)
3. CRSD to focused GraphX image

Quick-look output is an inspection aid. It is not focused-image evidence and
must not be used as a focused-image acceptance artifact.

Diagnostic-only or timing-only execution is also insufficient for focused-image
acceptance.

## Ordered CRSD Set Ingestion

Focused-image execution starts from an ordered CRSD segment set and uses
`OrderedCrsdSetInputSourceNode` as the source contract.

Accepted tiny CI config examples:

- `examples/SAR/config/sar_crsd_tiny_fixture_full_pipeline.json`
- `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json`
- `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json`

Accepted local GOTCHA-derived config example:

- `examples/SAR/config/sar_crsd_gotcha_local_validation.json`

All SAR edges in this lane must remain `SarAccelControlToken`-compatible and
the topology `edge_contract` must stay `accel-token`.

## Token-Based Phase-History Flow

The canonical CRSD-focused-image path is:

1. `OrderedCrsdSetInputSourceNode`
2. `CrsdApertureAssemblyAdapterNode`
3. `CrsdFocusedImageTransformNode` (CPU) or `CrsdFocusedImageTransformMetalNode` (Metal)
4. `CrsdFocusedImageSinkNode`

Contract requirements:

- One focused image per full assembled aperture.
- No focused image for data-marker or empty/diagnostic-only payloads.
- Ordered-set lineage must survive to output artifacts.
- Per-segment payload lineage and output hashes must be recorded.

## CPU And Metal Focused-Image Paths

CPU path:

- `CrsdFocusedImageTransformNode` produces deterministic focused output and
  records `output_hash` and `input_ordered_set_hash`.

Metal path:

- `CrsdFocusedImageTransformMetalNode` preserves the same lineage fields while
  attaching GPU transfer/kernel sidecar evidence.

GPU backfill evidence is expected in sidecar diagnostics (bytes H2D/D2H,
dispatch counts, backend/queue identity). This evidence supplements, but does
not replace, focused-image output correctness evidence.

## Split/Merge Topology And Determinism

Split/merge topologies are valid in SAR token pipelines and must remain
deterministic for repeated executions with identical inputs.

Determinism requirements:

- Stable focused-image output hash for identical inputs.
- Input perturbation changes output hash.
- Ordered-set and per-segment lineage remains preserved.

## Local-Only Boundaries

SarPy tools are optional local reference/validation tooling.

- SarPy is not a GraphX runtime dependency.
- SarPy is not required for CI focused-image lanes.

MATLAB is not a GraphX build-time, runtime, or test-time dependency and must
not be added as one.

Real-data lanes must remain explicit opt-in and local-only (for example via
`GRAPHX_SAR_CRSD_ROOT` gating).

## Focused-Image Evidence Matrix

Focused-image acceptance requires all rows below:

| Evidence Category | Required | Example Evidence |
| --- | --- | --- |
| Ordered CRSD set consumed | Yes | ordered `crsd_paths` and assembled aperture contract |
| Token edge contract | Yes | `edge_contract: accel-token` and `SarAccelControlToken` continuity |
| Data-dependent focused output | Yes | nonzero/finite focused response with perturbation sensitivity |
| Hash lineage recorded | Yes | `per_segment_input_hashes`, `ordered_set_hash`, `output_hash` |
| Deterministic replay | Yes | repeated run hash equality for identical inputs |
| Split/merge deterministic behavior | Yes | stable split/merge replay metrics and output |
| GPU backfill execution evidence | Required for Metal lane | sidecar backend/queue/transfer/kernel evidence |
| Quick-look-only output | No | not accepted as focused-image proof |
| Diagnostic-only or timing-only output | No | not accepted as focused-image proof |

## Verification Pointers

Representative tests and lanes:

- `examples/SAR/test/test_crsd_focused_image_transform_node.cpp`
- `examples/SAR/test/test_crsd_focused_image_metal.cpp`
- `examples/SAR/test/test_crsd_focused_image_sink.cpp`
- `examples/SAR/test/test_sar_json_pipeline.cpp`
- `examples/SAR/test/test_ci_validation_lane.cpp`
- `examples/SAR/test/test_local_gotcha_validation_lane.cpp`
