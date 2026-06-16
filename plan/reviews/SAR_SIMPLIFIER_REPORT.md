# 1. Target type model.

The target SAR type model has one canonical runtime transport for active SAR graph edges:

- `SarAccelControlToken = AccelControlToken<SarSidecar>` is the only SAR GPU/dataflow transport type used by maintained SAR runtime topologies.
- `SarSidecar` owns SAR identity, lineage, dimensions, labels, diagnostics, provenance, and artifact references.
- `host_view.host_ptr` and `device_view.ready_event` remain opaque transport handles. SAR identity, image identity, pulse identity, CRSD identity, and product identity must never be inferred from those handles.
- `NormalizedSarProduct` remains the C++ I/O domain model for GOTCHA/CRSD conversion, validation, reports, and file writers. It is not the default graph edge payload.
- CRSD phase-history concepts remain typed domain data, but active graph movement must be represented through `SarAccelControlToken` sidecar references or bounded payload handles rather than free-standing CRSD-specific edge contracts.
- Focused image output is an artifact/result model owned by sink/reporting boundaries. It must be referenced by SAR sidecar lineage when it crosses runtime boundaries.
- External reference products, SarPy validation reports, local GOTCHA reports, comparison images, checksums, and generated JSON are artifact contracts, not runtime graph message types.

Type names must describe capability or data semantics, not planning history. Historical names such as `prXX`, `rrpXX`, and implementation-phase labels are not valid active type names.

# 2. Target node model.

The target SAR node model has one maintained runtime shape:

`source/reader -> normalize/assemble -> SAR tokenized processing -> generic GPU transfer/kernel primitives where applicable -> SAR tokenized output -> focused-image/artifact/diagnostics sinks`

The maintained node categories are:

- Source nodes: emit `SarAccelControlToken` with complete `SarSidecar` identity and deterministic ordering.
- Assembly/normalization nodes: convert file/domain input into token-referenced SAR data and sidecar lineage.
- CPU algorithm nodes: may be retained when they perform real processing and have token-compatible input/output boundaries.
- GPU transfer nodes: use generic libgpu primitives such as H2D/D2H/queue/sync through SAR-token contracts.
- GPU algorithm nodes: may be active only when they bind a real backend capability and execute a real kernel or backend API implementation for the advertised algorithm.
- Sink/report nodes: consume SAR tokens or final artifact references and emit deterministic reports, images, checksums, and diagnostics.

The CRSD focused-image lane should not be a parallel runtime type system. `OrderedCrsdSetInputSourceNode`, aperture assembly, focused-image transform, and focused-image sink should either be token-compatible or explicitly classified as local/reference tooling outside the maintained SAR runtime topology.

libgpu owns generic GPU transport and backend primitives. examples/SAR owns SAR sidecar construction, SAR ordering, SAR metadata mapping, SAR algorithms, and SAR artifact/report semantics.

# 3. Deletion list.

Delete or remove from active maintained surfaces:

- Any active test, config, script, fixture, or documentation that exists only to prove a historical PR or planning artifact existed.
- Planning-era `prXX`, `rrpXX`, implementation/verifier report names, and similar historical labels from active code, tests, tools, docs, configs, and user-visible output. Historical reports may remain only under an explicit history location.
- Duplicate or stale SAR configs that are not part of a maintained stripmap, CRSD input, focused-image, local-validation, or comparison workflow.
- Any reintroduced SAR GPU compatibility alias for H2D, D2H, or backprojection names.
- Any active node registration or config that advertises an unsupported Metal node as production-ready.
- `CollectiveReduceNodeMetal` from active plugin/config surfaces until it has a supported implementation.
- `CrsdFocusedImageTransformMetalNode` from production or maintained configs unless it performs real native Metal focused-image processing end to end. If retained before completion, it must be explicitly experimental and excluded from production claims.
- Tests that validate generic SAR behavior by depending on `host_ptr`, `ready_event`, memory addresses, or incidental transport details.
- Documentation or tool output that implies surrogate SarPy/reference imagery is a true fully focused SarPy image when it is not.
- Any MATLAB build, runtime, or test dependency if it appears in active GOTCHA/CRSD workflows.

# 4. Replacement list.

Replace current complexity with these target forms:

- Replace free-standing CRSD phase-history graph edge contracts with `SarAccelControlToken` boundaries carrying sidecar lineage plus references to phase-history payloads or bounded buffers.
- Replace free-standing focused-image graph edge contracts with SAR-token lineage into a focused-image artifact/result sink contract.
- Replace domain-level GPU wrappers that only simulate or delegate CPU work with either real backend algorithm nodes or CPU-only nodes with honest names.
- Replace production references to incomplete Metal CRSD focused-image support with a CPU focused-image lane plus an explicit future Metal implementation boundary.
- Replace unsupported `CollectiveReduceNodeMetal` plugin exposure with no active node until the backend capability exists.
- Replace scattered CRSD/GOTCHA generated JSON concepts with a small set of named artifact contracts: ordered input manifest, sidecar/metadata inventory, conversion report, validation report, comparison report, and checksum manifest.
- Replace ambiguous CRSD-lite naming with a GraphX-owned normalized/intermediate SAR artifact name that does not imply CRSD compliance.
- Replace active planning report clutter in `plan/reviews` with a small active set: current inspector, simplifier, planner, policy, and architecture reports. Move implementation/verifier history to `plan/history`.
- Replace external-tool assumptions in runtime code with local-only artifact validation and comparison hooks outside libgraph/libgpu.

# 5. Architecture invariants.

- SAR identity is carried by `SarSidecar`, not by raw host pointers, device pointers, events, queue handles, or file paths alone.
- Maintained SAR graph edges use `SarAccelControlToken` unless an explicitly approved architecture decision creates a bounded non-token reference lane.
- Generic GPU nodes stay generic. SAR-specific metadata, CRSD concepts, GOTCHA concepts, image dimensions, aperture lineage, and report semantics stay in examples/SAR.
- A node named or documented as Metal/GPU must execute a real backend operation for its advertised algorithm, or it must be marked unsupported/experimental and excluded from production configs.
- Simulated capabilities, CPU fallbacks, and validation harnesses cannot be used as evidence of native Metal algorithm support.
- CRSD compliance must be truthful. Intermediate GraphX artifacts are not CRSD and must not be named or reported as CRSD.
- `--mode crsd` means standards-targeted CRSD output only. If standards-targeted output cannot be produced, the command must fail before writing misleading files.
- Local-only GOTCHA and external reference workflows remain optional, explicitly gated, and outside CI requirements unless tiny synthetic fixtures are used.
- External Python/SarPy/gotcha-back tools may validate, compare, or generate reference artifacts, but they must not define libgraph or libgpu runtime contracts.
- Deterministic ordering, deterministic diagnostics, deterministic report schemas, and checksumable outputs are required for maintained SAR workflows.
- Backward compatibility with planning-era names and intermediate aliases is not required.

# 6. Open questions that block planning.

- Must every active SAR runtime edge be converted to `SarAccelControlToken`, including CRSD phase-history and focused-image stages, or is a bounded typed CRSD CPU lane allowed?
- Should the CRSD focused-image lane be treated as production SAR runtime, local/reference tooling, or a staged bridge while tokenization is completed?
- Should `CrsdFocusedImageTransformMetalNode` be deleted from active surfaces now, or completed as a real native Metal focused-image implementation before further CRSD planning?
- Should `CollectiveReduceNodeMetal` be removed from plugin registration until supported, or retained as explicitly unsupported documentation-only surface?
- Which SAR configs are the maintained canonical configs, and which should be deleted as obsolete examples?
- Which historical report folders are allowed to retain `prXX` and verifier/implementer names without violating active naming hygiene?
- Is the current external reference-image workflow acceptable as a surrogate comparison lane, or is a true fully focused SarPy/reference image required before image-quality planning continues?
- What is the minimum real-data local validation contract: ingest only, normalized artifact generation, focused image generation, SarPy validation, image comparison, or all of these?
- Should real CRSD binary reading be implemented as native C++ only, or may a local-only helper bridge be used outside runtime contracts?
