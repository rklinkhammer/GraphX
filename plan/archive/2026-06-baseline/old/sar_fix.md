Title:
Replace legacy SAR GPU message path with explicit AccelControlToken<SarSidecar>

Role

You are a senior GraphX GPU/DAG architect and C++ framework engineer working inside the current GraphX repository.

Inspect the actual repository before making recommendations.

Repository inspection overrides assumptions in this prompt.

Do not assume previous revisions.

This is an analysis task first.

Do not implement code unless explicitly asked.

---------------------------------------------------------------------

Primary Philosophy

GraphX architecture takes precedence over SAR convenience.

Correctness
>
Architectural clarity
>
Observability
>
Performance
>
Convenience

The codebase is pre-production.

Backward compatibility is explicitly NOT required.

Deletion is preferred over compatibility.

There shall be exactly one canonical SAR GPU path.

Avoid creating transitional abstractions that will later require removal.

---------------------------------------------------------------------

Required Architecture

Graph edges carry accel-control tokens.

Tokens contain:

- BufferLease
- HostPinnedBufferView
- DeviceBufferView
- TransferTicket
- KernelTicket

SAR identity is carried as a strongly typed sidecar.

GPU nodes transform token state.

Capabilities perform backend work.

Generic GPU infrastructure must not know SAR semantics.

SAR nodes may interpret SarSidecar.

Metal is the primary backend.

Other backends are deferred.

Required flow:

SAR DSP/source nodes
    ↓
AccelControlToken<SarSidecar>
    ↓
Generic GPU transfer/kernel nodes
    ↓
AccelControlToken<SarSidecar>
    ↓
SAR merge/diagnostics nodes

No alternate path is allowed.

---------------------------------------------------------------------

Architectural Invariants

Do not make GPU views pretend to be SAR messages.

Do not encode SAR identity into:

- host_ptr
- ready_event
- buffer addresses
- pointer values
- event handles

Do not use global sidecar maps as primary architecture.

Do not allow raw SAR payload-envelope messages across transfer/kernel boundaries.

Do not allow SAR message types to leak into generic GPU abstractions.

Do not allow SAR dataset formats to influence GraphX contracts.

Do not let SAR math bypass GraphX DAG semantics.

---------------------------------------------------------------------

Repository Inspection

Inspect at minimum:

- SarMessages.hpp
- SarAccelTokenSidecarStore
- H2DAsyncAccelNode
- SarBackprojectionTransformAccelNode
- D2HAsyncAccelNode
- DeviceKernelNodeMetal
- ResolvingNodeProvider
- NodeResolutionRegistry
- SAR JSON configs
- accel-token tests

Search for:

host_ptr
ready_event
sidecar
token
payload
resolver
Metal
Sar*

Repository inspection overrides assumptions.

---------------------------------------------------------------------

Audit

Classify every occurrence as:

Observed
Inferred
Unknown

Classify every issue as:

Blocker
Required PR fix
Follow-up
Documentation mismatch

Find all locations where:

- host_ptr is used as token identity
- ready_event carries SAR identity
- sidecar state is stored globally
- resolver substitution risks sidecar loss
- raw SAR payload concepts cross transfer/kernel boundaries
- multiple GPU message paths exist
- legacy contracts survive

---------------------------------------------------------------------

Required End State

Introduce:

AccelControlToken<SidecarT>

and:

SarSidecar

Convert:

split
H2D
kernel
D2H
merge

to explicit token passing.

Preserve:

- dynamic loading
- resolver substitution
- provider model

Metal remains the first concrete backend.

There shall be one canonical SAR GPU message type:

AccelControlToken<SarSidecar>

---------------------------------------------------------------------

Deletion Policy

You are expected to remove obsolete code.

Compatibility shims are prohibited.

Dual paths are prohibited.

Delete obsolete:

classes
functions
configs
tests
schemas
aliases
messages

Default answer:

DELETE

unless there is a compelling architectural reason to keep something.

Do not preserve:

- SarDeviceLeaseMessage
- SarTransferTicketMessage
- encoded host_ptr identities
- encoded ready_event identities
- global sidecar stores as primary mechanism

If a test validates obsolete behavior:

delete or replace it.

---------------------------------------------------------------------

Output Requirements

Produce:

1. Executive diagnosis

2. Architectural violations

3. Current type model

4. Proposed type model

5. Current node model

6. Proposed node model

7. Resolver strategy

8. Dynamic loading strategy

9. File-level plan

10. Tests to add

11. Tests to delete

12. Classes to delete

13. Config/schema changes

14. Migration sequence

15. Acceptance criteria

16. Things not to do

---------------------------------------------------------------------

Things Not To Do

Do not propose:

compatibility layers

dual message paths

adapter pyramids

global sidecar registries

symbolic pointer hacks

event-handle identity tricks

SAR-specific GPU abstractions

large framework rewrites

premature support for CUDA or SYCL

transitional designs intended to survive indefinitely

Prefer removing complexity over managing complexity.
