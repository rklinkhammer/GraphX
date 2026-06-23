# Scenario 001

## Purpose

Freeze the first immutable deterministic image-formation correctness scenario before any external setup, runner, adapter, comparator metrics expansion, or CI fixture promotion.

Scenario 001 is the baseline for a fair comparison where the same known deterministic IQ/phase-history fixture is used to produce:

1. CPU reference backprojection image artifact.
2. GraphX SAR pipeline image artifact.
3. Comparator metric report from both artifacts.

## Comparison Level

- `internal_image_formation_correctness`

## Dataset

- Dataset: AFRL GOTCHA Challenge Problem
- Scenario subset: `scenario_001_local_manual`
- Source mode: local manual external dataset only
- Normalization schema: `graphx.sar.gotcha.normalized.v1`

## Fixture Description

- Fixture id: `deterministic_iq_phase_history_fixture_v1`
- Fixture kind: deterministic IQ/phase-history
- Fixture source: repository-managed fixture contract (no external download in this CI-safe scenario)

## Pulse Range

- Start: 0
- Count: 4

## Range Bins

- Start: 0
- Count: 32

## Output Format

- Format: float32 raster
- DType: float32
- Layout: row-major
- Artifact kind: materialized image

## Output Artifact Contract

- Format: `float32_raster`
- DType: `float32`
- Layout: `row_major`
- Dimensions: `16 x 16`

## Expected Future Flow

Known deterministic IQ/phase-history fixture
	-> CPU reference backprojection
	-> Reference image artifact

Known deterministic IQ/phase-history fixture
	-> GraphX SAR pipeline
	-> GraphX image artifact

Reference image artifact + GraphX image artifact
	-> Comparator
	-> Metric report

## Algorithm

- Algorithm family: backprojection
- Algorithm type: `backprojection`
- Reference path: `cpu_reference_backprojection`
- GraphX path: `graphx_sar_pipeline`
- Range compression mode: matched_filter
- Window: hann

## Immutability Rule

`scenario_001.json` is the frozen experiment definition for the first reference reproduction path.
Changes to dataset selection, fixture identity, pulse range, range bins, image grid, scene center, algorithm, comparator profile, or output artifact contract must be introduced as `scenario_002` rather than by mutating `scenario_001`.

## Explicit Non-Goals

This scenario does not:

- download external data,
- run external packages,
- add SarPy, ISCE3, or gotcha-back integrations,
- implement CPU reference backprojection,
- implement comparator metric logic.