# Metal Node Truth-In-Labeling Inventory

Status: PR5c blocking corrective guardrail between PR5b and PR6

PR6 gate: blocked until any node classified below as unsupported or experimental-incomplete is either implemented fully or kept explicitly downgraded.

## Classification Rules

Allowed classes:

- memory
- transfer
- sync/control
- sink/source
- kernel primitive
- domain algorithm
- unsupported

Behavior labels:

- real: behavior is implemented through native Metal capability interfaces and succeeds under a capable backend
- simulated: behavior is capability-mediated but can run with default stub capabilities that emulate native operations
- fallback: behavior is intentionally routed to non-native compute path and must be labeled fallback
- unsupported: operation is intentionally not implemented

## Active Metal-Named Nodes

| Node | Class | Binds Metal capabilities | Native usage mode | Launches kernel | Kernel expected | Behavior label | Truth-in-labeling notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| HostIngressPinnedSourceNodeMetal | sink/source | yes (memory pool) | capability-mediated | no | no | real/simulated | Stages host-pinned buffers only; no algorithm claim. |
| H2DAsyncNodeMetal | transfer | yes (context/shared queue/memory/transfer) | capability-mediated | no | no | real/simulated | Real transfer boundary; non-kernel node by design. |
| D2HAsyncNodeMetal | transfer | yes (context/shared queue/memory/transfer) | capability-mediated | no | no | real/simulated | Real transfer boundary; non-kernel node by design. |
| PeerCopyNodeMetal | transfer | yes (context/shared queue/memory/transfer) | capability-mediated | no | no | real/simulated | Real D2D copy boundary; non-kernel node by design. |
| DeviceShardNodeMetal | memory | yes (context/shared queue/memory/transfer) | capability-mediated | no | no | real/simulated | Memory partition and D2D shard copy; no algorithm claim. |
| LeaseReleaseNodeMetal | memory | yes (memory pool) | capability-mediated | no | no | real/simulated | Lease lifecycle release boundary; no algorithm claim. |
| QueueSyncNodeMetal | sync/control | yes (context/shared queue) | capability-mediated | no | no | real/simulated | Queue/event synchronization boundary; no algorithm claim. |
| HostEgressSinkNodeMetal | sink/source | no | none | no | no | real | Sink endpoint for host views; no algorithm claim. |
| DeviceKernelNodeMetal | kernel primitive | yes (context/shared queue/memory/kernel/telemetry) | capability-mediated | yes | yes | real/simulated | Generic descriptor-driven primitive; not a domain algorithm claim. |
| DeviceTransformNodeMetal | kernel primitive | yes (context/shared queue/memory/transfer/kernel/telemetry) | capability-mediated | yes | yes | real/simulated | Generic transform primitive; not a domain algorithm claim. |
| DeviceReduceNodeMetal | kernel primitive | yes (context/shared queue/memory/transfer/kernel/telemetry) | capability-mediated | yes | yes | real/simulated | Generic reduce primitive; not a domain algorithm claim. |
| CollectiveReduceNodeMetal | unsupported | yes (collective capability) | capability-mediated | no | yes for implemented collective algorithm | unsupported | Default collective capability returns false; plugin info explicitly says runtime unsupported. |
| CrsdFocusedImageTransformMetalNode | domain algorithm | yes (context/shared queue/memory/transfer/kernel/telemetry) | capability-mediated | yes | yes | fallback + experimental incomplete | Native path currently uses CPU seed-image generation plus placeholder Metal kernel post-processing. It must not be advertised as complete native focused-image algorithm. |

## Guardrail Policy

- Transfer/memory/sync/control nodes are valid Metal nodes without kernels when they bind and use Metal capabilities honestly.
- Kernel primitive nodes are valid when represented as generic primitives and not mislabeled as complete domain algorithms.
- Domain algorithm nodes must not claim complete native algorithm support unless they implement the advertised algorithm end-to-end in native mode.
- CrsdFocusedImageTransformMetalNode is currently experimental-incomplete and must remain explicitly labeled as such.
- CollectiveReduceNodeMetal must remain unsupported until real collective behavior is implemented.
