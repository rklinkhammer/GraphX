# ADR 0002: Dashboard runtime identity and metric semantics

Status: accepted for Phase 3

## Decision

The stable configuration node string ID is the only canonical node identity.
The canonical identity of an edge is:

```text
source_node_id:source_port->destination_node_id:destination_port
```

At rebuild, `GraphRuntimeSession` captures immutable node and edge identity
sets from the effective receiver graph. `GraphBuilder` also records each
configuration ID directly when it registers the corresponding runtime node
with `GraphManager`. Vector positions are private runtime lookup keys only.
`GraphSnapshotCollector` matches the two sets, derives every runtime edge from
the runtime nodes' recorded canonical IDs and exact metadata ports, and then
publishes those IDs. Runtime node or edge reordering therefore cannot change
identity. Runtime indices and runtime/display names remain noncanonical
diagnostic fields during the compatibility period.

The correlation chain is therefore:

```text
configuration ID -> generation-bound runtime mapping
                 -> metric/diagnostic canonical ID
                 -> frontend topology canonical ID
```

The browser must never recover a missing canonical ID from an array position,
runtime name, detector prefix, or presentation-group membership. A missing,
duplicate, cardinality-mismatched, or port-mismatched mapping makes the
authoritative overlay unavailable. The underlying topology remains visible.
FHSS detector-bank groups are presentation objects and never runtime
identities.

## Coherence

`generation` identifies one successful rebuild. `run_epoch` identifies one
execution attempt within that generation. A restart creates a new run epoch.
The canonical FHSS runtime owner replaces the runtime manager for each
execution attempt, but the generic session contract does not require every
owner to do so; therefore counters and peaks declare the runtime-manager
lifetime as their reset boundary rather than claiming a run-epoch reset.
Generation and run epoch zero mean there is no active runtime/run,
respectively.

Metrics and diagnostics carry generation, run epoch, configuration revision,
configuration ETag, capture ID, and server monotonic sampling time. A coherent
snapshot is published only when its runtime state is stable and both resources
match the same tuple. Metrics and diagnostics from another tuple are stale and
must not attach to current visuals. Coalescing is permitted only within an
identical tuple. JSON integer identities are bounded to JavaScript's exact
integer range (`0..2^53-1`); the event publisher requests resynchronization
instead of publishing an unsafe sequence.

## Metric semantics

Metric definitions explicitly declare their JSON field, scope, kind, unit,
monotonicity, availability, capture, reset, aggregation, overflow, and numeric
representation. Counters and peak gauges retain values for the lifetime of
their runtime manager; an owner that replaces the manager at start obtains a
fresh set. Current queue depth is an instantaneous gauge read from the actual
runtime-edge queue. A missing runtime or a value above JavaScript's exact
integer range is unavailable with a reason and `null` value, not an observed
zero.

Rate derivation requires two compatible counter samples from the same
generation, run, configuration, unit, and reset identity over a positive
server-monotonic interval. Phase 3 publishes explicit unavailability when no
compatible previous sample exists; it does not use browser arrival time or
retain unbounded history.

The historic `total_queue_time_ns` storage uses `std::chrono::steady_clock` to
measure successful dynamic-edge `TransferTo` call duration. It is published as
`transfer_service_duration`, with its count and cumulative total. It is not
queue residence, node processing, receiver end-to-end, or dashboard delivery
latency. Those uninstrumented timing classes remain unavailable. A generic
`latency` label is prohibited.

## Execution boundary

Collection performs bounded read-only loads over the active graph. It does not
dequeue data, alter counters, mutate topology, participate in scheduling, or
feed expected truth into receiver execution. Phase 4 owns bounded activity
visualization; this decision introduces no animation or second protocol.
