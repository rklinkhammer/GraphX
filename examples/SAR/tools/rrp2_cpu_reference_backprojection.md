# RRP2 CPU Reference Backprojection

This document describes the deterministic CPU reference image-formation path introduced for Scenario 001.

## Scope

RRP2 provides only the reference-image side:

Scenario 001 deterministic IQ / phase-history fixture
    -> CPU reference backprojection
    -> reference image artifact

RRP2 does not run GraphX SAR comparison. That begins in RRP3.

## Implementation Notes

Location:

- `examples/SAR/include/sar/SarScenario001CpuReference.hpp`

The implementation uses scalar, deterministic computation with double precision for geometric/range/phase math, and emits float32 raster pixels.

## Equation And Assumptions

Backprojection model per pixel p and pulse i:

- $R_i(p) = ||s_i - p||$
- $R_{0,i} = ||s_i - c||$ where c is fixture scene center
- $\Delta R_i = R_i - R_{0,i}$
- $\Delta r = c_0 / (2 f_s)$ with $f_s$ from fixture sample_rate_hz
- local range-bin index from nearest-neighbor mapping around the center bin:
  - $k_i = k_{center,i} + round(\Delta R_i / \Delta r)$
- phase correction:
  - $\phi_i = 4\pi R_i / \lambda$ with $\lambda = c_0 / f_c$
- image pixel magnitude:
  - $I(p) = |\sum_i x_i[k_i] e^{j\phi_i}| / N_{pulses}$

where:

- $x_i[k_i]$ is fixture complex IQ sample for pulse i, bin $k_i$,
- $s_i$ is fixture platform_position_m for pulse i,
- $f_c$ is fixture carrier_hz,
- $c_0$ is speed of light.

Assumptions:

- fixture range bins are centered around the pulse-wise scene-center slant range,
- nearest-neighbor range-bin sampling is sufficient for RRP2 baseline,
- flat pixel plane at scenario scene_center.z.

Expected limitations:

- no interpolation beyond nearest bin,
- no full matched-filter chain,
- intentionally simple for deterministic baseline clarity.

## Output Artifact Format

- raster: float32 row-major binary (`.bin`)
- contract: JSON sidecar (`*_contract.json`)
- metadata includes:
  - `scenario_id`
  - `fixture_id`
  - `algorithm = cpu_reference_backprojection`
  - `width`, `height`
  - `dtype = float32`
  - `layout = row_major`
  - `format = float32_raster`
  - `artifact_kind = materialized_image`
  - deterministic hash (`fnv1a64` over float bytes)
