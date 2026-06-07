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

## Implementation Dashboard

| Phase | Scope | Status | Implementation |
| --- | --- | --- | --- |
| 3.1 | Synthetic source contract | Completed | `SyntheticApertureIqSourceNode` |
| 3.2 | Azimuth tile split | Completed | `AzimuthTileSplitNode` |
| 3.3 | Deterministic backprojection transform | Completed | `SarBackprojectionTransformNode` |
| 3.4 | Image tile merge correctness + status emission | In progress (started) | `ImageTileMergeNode` |

## Correlation To `plan/example3.md`

The current PR1 slice is correlated to the implementation-plan prompt in `plan/example3.md` as follows:

1. PR1 cap of no more than 4 new SAR nodes:
  Implemented: `SyntheticApertureIqSourceNode`, `AzimuthTileSplitNode`, `SarBackprojectionTransformNode`, `ImageTileMergeNode`.
2. JSON topology as the primary demonstration path:
  Implemented in `config/sar_stripmap_pr1.json` with end-to-end source/split/backprojection/merge wiring.
3. Explicit EOS/watermark/tile correctness handling:
  Implemented in `ImageTileMergeNode` via duplicate/missing/out-of-order/watermark tracking and completion gating.
4. Deterministic CI-stable behavior:
  Unit tests use deterministic synthetic messages and fixed assertions.
5. Plugin/provider integration model:
  `ImageTileMergeNode` includes plugin export and plugin-load validation test following existing SAR node patterns.
