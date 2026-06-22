# GraphX Node Simplification Roadmap

Source request: GraphX Node Generalization Plan attachment.

Invariant for every PR:

- Do not change existing public node class names.
- Do not change input/output port counts, names, indices, or token contracts.
- Do not change graph JSON behavior, plugin registration names, or observable execution semantics.
- Generalize mechanics, not meaning.

## PR1: Inventory And Port Contract Guardrails

Purpose:

- Freeze current node shapes before refactoring.
- Convert the inventory into compile/runtime guardrails for representative nodes.

Files to touch:

- `libgraph/test/unit/*node_contract*`
- `libgraph/test/unit/test_fhss_graphx_guardrails.cpp`
- SAR/DSP/FHSS existing node contract tests where appropriate

Tests to add:

- Port count/name/type checks for split, merge, FHSS, DSP, SAR, and GPU representative nodes.
- Plugin registration checks for node names affected by later PRs.

Acceptance:

- Reports and tests agree on current node contracts.
- No implementation refactor yet.

Risk:

- Large test inventory may be brittle if it over-specifies private behavior.

## PR2: Extract Shared Transform Mechanics

Purpose:

- Add a small `TransformNodeBase` or helper layer for 1->1 token transforms.

Files to touch:

- New helper under `libgraph/include/graph` or domain-local experimental helper.
- One or two low-risk FHSS pass-through/transform nodes first.

Tests to add:

- Existing port contracts unchanged.
- Behavior parity for selected nodes.

Acceptance:

- Concrete node names and ports unchanged.
- No semantic policy moves into the base.

Risk:

- The first base may overfit FHSS and not help SAR/DSP.

## PR3: Extract Stable Config/Parameter Helpers

Purpose:

- Remove repeated `GetParameters`, `GetParameterDescription`, and `GetParameterNames` mechanics where fields are declarative.

Files to touch:

- GraphX config helper headers.
- Selected FHSS/SAR/DSP nodes with simple config surfaces.

Tests to add:

- Exact JSON parameter key tests for migrated nodes.
- Existing config parser tests.

Acceptance:

- Parameter JSON remains stable.
- No graph behavior changes.

Risk:

- Key order or description wording may be test-observable.

## PR4: Extract GPU Queue/Capability Binding Helpers

Purpose:

- Generalize queue id resolution, queue ownership, view/lease/ticket validation, and backend compatibility parsing.

Files to touch:

- `libgpu/include/gpu/*`
- `libdsp/include/dsp/DspIqH2DNode.hpp`
- `libdsp/include/dsp/DspMagnitudeD2HNode.hpp`
- selected Metal transfer nodes

Tests to add:

- Metal/DSP GPU node diagnostics unchanged.
- Invalid view/capability behavior unchanged.

Acceptance:

- Transfer/kernel node ports unchanged.
- Capability binding behavior unchanged.

Risk:

- Backend differences may limit sharing to helper functions rather than a base class.

## PR5: Extract Repeated Port Binding Utilities

Purpose:

- Generalize repeated input/output type-list construction and repeated port metadata.

Files to touch:

- `libgraph/include/graph` port utility headers or domain-local FHSS helper first.
- `ChannelizerNode`
- `FHSSPulseMergeNode`

Tests to add:

- Channelizer still exposes exactly 64 outputs.
- Pulse merge still exposes 65 inputs and 2 outputs.
- Port names and token types unchanged.

Acceptance:

- Macro-expanded code is reduced or isolated.
- Public port contracts unchanged.

Risk:

- Template indirection may reduce readability if not kept narrow.

## PR6: Extract Split Policy Base

Purpose:

- Introduce a shape-specific split/fan-out base for non-trivial 1->N nodes.

Candidate migrations:

- `ChannelizerNode` after PR5.
- Possibly `DeviceShardNodeMetal`.
- Leave `SarPulseFanoutNode` on existing `SplitNode4` unless the new base demonstrably improves it.

Tests to add:

- Same graph JSON executes.
- Output packet metadata parity.
- Failure behavior parity for invalid input/config.

Acceptance:

- Split mechanics centralized.
- Domain packet creation stays in policy.

Risk:

- `ChannelizerNode` 64-port invariant must remain easy to see.

## PR7: Extract Merge/Barrier Policy Base

Purpose:

- Introduce a merge base that can handle domain matching/buffering policy without becoming universal.

Candidate migrations:

- `FHSSPulseMergeNode`.
- `ImageTileMergeNode` only if logical N->1 behavior can be represented without hiding SAR diagnostics.
- `CompletionAggregatorNode` only if it remains visibly a completion/control node.

Tests to add:

- Duplicate, missing, partial, EOS, and completion behavior parity.
- Port contract tests for high-port merge nodes.

Acceptance:

- Buffering/queue mechanics centralized.
- Matching/completion policy remains domain-specific.

Risk:

- Trying to merge all merge-like nodes in one PR would be too broad.

## PR8: Introduce Optional Router/N-M Mechanics

Purpose:

- Address N->M and staged routing only after split/merge bases are proven.

Candidate migrations:

- FHSS channelizer plus pulse merge subsystem diagnostics.
- Future backend dispatch or demux nodes.

Tests to add:

- Graph JSON route invariants.
- Port metadata invariants.

Acceptance:

- No universal node framework.
- Router base exists only if at least two concrete nodes benefit.

Risk:

- Router abstraction may be unnecessary after PR5-PR7.

## PR9: Remove Duplicated Legacy Mechanics

Purpose:

- Delete duplicated helper code only after equivalent bases/helpers are covered.

Candidate removals:

- Macro-expanded port boilerplate if replaced.
- Repeated JSON parameter helpers.
- Repeated GPU queue config helpers.

Tests to add:

- Guardrails preventing removed duplicate patterns from returning.

Acceptance:

- Net code reduction without public behavior changes.

Risk:

- Mechanical cleanup can accidentally touch unrelated formatting.

## PR10: Documentation And Examples Audit

Purpose:

- Update docs to describe the new mechanics while preserving concrete node meanings.
- Ensure examples still report performance metrics.

Files to touch:

- `docs/`
- `README.md` if example index changes.
- Example docs for SAR/DSP/FHSS as needed.

Tests to add:

- Truth-in-labeling/guardrail tests for unchanged node names and performance-report language.

Acceptance:

- Docs distinguish node mechanics from domain semantics.
- No docs claim a universal node framework.

Risk:

- Documentation can drift from port-contract tests if not linked.
