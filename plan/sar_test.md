Index:
- [Dataset investigation prompt](plan/sar_test.md)
- [Coding agent implementation prompt](plan/sar_test_coding_agent_prompt.md)

You are a senior SAR systems engineer, radar data-ingestion specialist, and C++ pipeline architect.

Investigate whether the AFRL Gotcha Volumetric SAR Dataset can be integrated as a real-world dataset for the GraphX SAR pipeline.

Important instruction:
Do not assume dataset details that are not verified. Clearly label each item as:
- Known
- Likely
- Unknown
- Requires inspection
- Requires experiment

Project context:
- Target system: GraphX SAR implementation with plugin-based graph nodes, JSON topologies, explicit DAG tokens/context, and PR3 Metal-native acceleration path.
- Existing synthetic/topology coverage includes windowing, range compression, fanout, tile split/merge, H2D, kernel dispatch, D2H, diagnostics sinks.
- GraphX edges carry tokens/context/metadata, not implicit byte movement.
- Capabilities/backend nodes perform actual data movement and kernel execution.
- Goal: move from synthetic SAR test inputs to validated real-data ingestion and reconstruction.

Primary question:
Can Gotcha phase-history/IQ data be used as a practical real-data source for GraphX SAR reconstruction, diagnostics, benchmarking, and PR3 Metal acceleration?

Produce the following:

1. Go/no-go recommendation
- Recommendation: Go, No-Go, or Go-with-caveats.
- Confidence level.
- Top 5 blockers.
- Top 5 reasons it is valuable for GraphX.

2. Dataset feasibility
Investigate:
- Dataset access path.
- License/use constraints.
- Redistribution restrictions.
- Whether CI fixtures can legally include raw data, derived data, metadata-only slices, or synthetic derivatives.
- Data volume and storage layout.
- Native format(s).
- Metadata completeness.
- Expected preprocessing burden.
- Compatibility with GraphX’s current stripmap/spotlight assumptions.

3. SAR data mapping
Identify the exact fields required by GraphX:
- complex IQ / phase-history samples
- pulse index
- frequency/range sample axis
- carrier or center frequency
- bandwidth / frequency step
- platform position
- platform velocity if available
- aperture/pass ID
- scene center
- coordinate frame
- timing metadata
- calibration terms
- polarization if applicable
- units and scaling
- endian/layout assumptions

Provide a mapping table:

Gotcha field or file item
→ meaning
→ GraphX message/envelope/sidecar field
→ required/optional
→ confidence
→ validation method

4. GraphX integration architecture
Design:
- Offline converter
- Native Gotcha source node
- Metadata normalization node
- Optional calibration/motion compensation node
- Handoff into existing PR3 accel-token pipeline
- Handoff into existing H2D/kernel/D2H/tile-merge path
- Diagnostics sink integration

Make clear which parts belong under:

examples/SAR/
libdsp/
libgpu/
libgraph/

Do not propose framework-wide rewrites unless unavoidable.

5. Message and DAG contracts
Define the GraphX message contracts needed for Gotcha ingestion:
- frame ID
- pass ID
- pulse block ID
- range bin span
- aperture span
- tile ID
- batch ID
- timestamp or synthetic ordering key
- backend/device/queue metadata
- buffer ownership
- host buffer view
- device lease/view
- transfer ticket
- kernel ticket
- completion/watermark behavior

State which contracts already exist and which are new or adapter-only.

6. Minimal viable implementation
Define the smallest useful end-to-end milestone:

Gotcha subset
→ offline conversion
→ GraphX source
→ range preprocessing
→ tiled backprojection
→ image tile merge
→ diagnostics report
→ image output

Include:
- files to create
- nodes to create
- tests to add
- topology JSON to add
- command-line tool behavior
- expected output artifacts

Limit the first implementation to a reviewable PR.

7. Phased roadmap
Phase A: Offline converter and deterministic fixture generation.
Phase B: Native Gotcha stream/source node.
Phase C: PR3 Metal-native acceleration path.
Phase D: Larger-pass performance benchmarking.
Phase E: multi-backend CUDA/SYCL/Metal comparison.

For each phase:
- goal
- deliverables
- dependencies
- estimated effort
- risks
- exit criteria

8. Validation strategy
Define correctness checks:
- known-scene geometry checks
- point-target focus
- impulse/PSF behavior
- image sharpness metrics
- peak location error
- dynamic range sanity checks
- phase consistency where applicable
- comparison against known Gotcha reference images if available

Define deterministic regression tests:
- tiny fixture
- medium local fixture
- benchmark fixture
- golden metadata hash
- golden image/tile tolerances

9. Performance and diagnostics
Report required metrics:
- pulses/sec
- complex samples/sec
- range preprocessing time
- tile split/merge time
- H2D bytes and bandwidth
- D2H bytes and bandwidth
- kernel dispatch count
- kernel time
- queue wait/backpressure
- fan-in wait time
- memory allocation/reuse
- peak memory
- end-to-end latency

Separate:
- graph overhead
- DSP algorithm time
- backend transfer time
- backend kernel time
- diagnostics overhead

10. Risks and mitigations
Address:
- licensing/legal restrictions
- format ambiguity
- undocumented preprocessing assumptions
- coordinate-frame mismatch
- units/scaling mismatch
- missing calibration data
- numerical stability
- dynamic range
- Metal-specific limits
- memory pressure
- lack of CI redistribution permission

11. Decision matrix
Compare:
- direct Gotcha parser
- offline converter to GraphX SAR intermediate format
- HDF5/Zarr/NPZ intermediate
- tiny derived CI fixture
- synthetic-only fallback

For each:
- pros
- cons
- implementation cost
- legal risk
- testability
- performance impact
- recommendation

12. Mermaid architecture diagram
Include a diagram showing:

Gotcha files
→ converter/source
→ metadata normalization
→ DSP preprocessing
→ tile split
→ H2D
→ Metal kernel
→ D2H
→ merge
→ diagnostics/image output

13. Explicit unknowns and experiments
If any unknown blocks execution, define:
- exact file to inspect
- exact metadata to extract
- exact experiment to run
- expected result
- decision enabled by the result

Optional deep dive:
Propose a compact, legally compliant benchmark subset strategy:
- raw-data slice if allowed
- derived-data fixture if raw redistribution is restricted
- metadata-only plus synthetic reconstruction fixture
- image-only validation fixture
- local-only benchmark profile
- CI-safe deterministic profile

Output requirements:
- Start with recommendation and confidence.
- Do not bury blockers.
- Be explicit about known vs unknown.
- Tie every recommendation back to GraphX architecture.
- Do not let SAR math implementation bypass GraphX DAG semantics.