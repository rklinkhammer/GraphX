# Scenario 001

## Purpose

Freeze the first immutable SAR reference reproduction scenario before any external runner, adapter, comparator, or CI fixture work.

## Dataset

- Dataset: AFRL GOTCHA Challenge Problem
- Scenario subset: `scenario_001_local_manual`
- Source mode: local manual external dataset only
- Normalization schema: `graphx.sar.gotcha.normalized.v1`

## Pulse Range

- Start: 0
- Count: 4

## Range Bins

- Start: 0
- Count: 32

## Output Format

- Format: float32 raster
- Layout: row-major
- Artifact kind: materialized image

## Algorithm

- Algorithm family: backprojection
- Range compression mode: matched_filter
- Window: hann

## Immutability Rule

`scenario_001.json` is the frozen experiment definition for the first reference reproduction path.
Changes to dataset selection, pulse range, range bins, image grid, scene center, algorithm, window, range compression, or output contract must be introduced as a new scenario id rather than by mutating `scenario_001`.