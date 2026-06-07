# GraphX SAR Example (PR1 Scaffold)

This directory contains the initial scaffold for the GraphX SAR example package.

PR1 scope for this package:

- Keep all SAR-specific implementation under `examples/SAR`
- Use JSON topology as the main demonstration path
- Keep deterministic synthetic data and CI-stable behavior
- Introduce no more than four new SAR nodes in PR1

Current scaffold contents:

- `config/sar_stripmap_pr1.json`: starter topology placeholder
- `src/main.cpp`: starter executable entrypoint
- `CMakeLists.txt`: local build integration for the `sar_example` binary

Build toggle:

- Controlled by top-level CMake option: `GRAPHX_BUILD_EXAMPLES_SAR`
