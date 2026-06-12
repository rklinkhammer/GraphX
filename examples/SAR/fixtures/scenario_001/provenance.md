# Scenario 001 Fixture Provenance

This fixture is synthetic and deterministic.

- Scenario id: scenario_001
- Fixture id: deterministic_iq_phase_history_fixture_v1
- Source: repository-authored synthetic data
- External dataset dependency: none
- External package dependency: none
- Network/download dependency: none
- CUDA dependency: none

This fixture is not sourced from AFRL GOTCHA, Sentinel, SarPy, ISCE3, or gotcha-back outputs.

## Deterministic Generation Contract

The fixture values are generated from a closed-form deterministic formula and fixed seed metadata:

- Seed: 17
- Real: real = 1.0 + 0.1*pulse_index - 0.05*range_bin_index
- Imag: imag = -0.25*pulse_index - 0.125*range_bin_index

## Scenario Linkage

Scenario 001 defines pulse/range intent as 4 pulses x 32 range bins.
This committed CI fixture uses a reduced profile (4 pulses x 8 range bins) and explicitly records that linkage in fixture_manifest.json.
Any semantic changes require a new scenario id (scenario_002+), not mutation of scenario_001.
