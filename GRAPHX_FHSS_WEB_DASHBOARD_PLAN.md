# GraphX FHSS Embedded Web Dashboard Plan

Date: 2026-06-23  
Repository: `GraphX`  
Status: Updated implementation plan  
Primary rule: **Every implementation phase must leave a runnable embedded webserver.**

## 0. Executive Summary

Build the FHSS dashboard as an optional embedded HTTP/WebSocket application
backed by UI-neutral GraphX snapshot, configuration, and command services.

The implementation must grow as a sequence of runnable vertical slices. The
first displayable slice is not metrics or FHSS stepping: it is the effective
JSON graph itself.

The first browser experience must:

1. Start from `graphx-dsp-fhss-demo`.
2. Serve a local page from the embedded server.
3. Display the loaded effective graph as nodes and edges.
4. Let the user expand each node and inspect:
   - graph node id, name, and type;
   - lifecycle and capability metadata when available;
   - configured `node_config`;
   - runtime-reported `IParameterized` values when available (introduced in
     Step 2 via an unstarted inspection graph);
   - parameter descriptions/schema when available;
   - input/output ports and edge token types.
5. Expose the same graph and parameter information through versioned JSON APIs.
6. Support staging configuration changes by RFC 6901 JSON Pointer path.
7. Export the changed effective configuration.

The first implementation must not require a running graph. This is important:
configuration inspection and editing should work after the graph JSON has been
loaded and validated but before execution begins.

The recommended development sequence is:

```text
Server shell
  -> effective JSON graph viewer
  -> parameter inspection and staged JSON Pointer updates
  -> validated graph rebuild/run
  -> generic status and metrics
  -> FHSS message-at-a-time stepping
  -> WebSocket live updates
  -> advanced FHSS visualizations
```

The dashboard must not introduce a second canonical FHSS topology. One
user-visible step means releasing one configured FHSS protocol message and
allowing it to reach a terminal result through the existing canonical graph.
For the initial dashboard initiative, message stepping is controlled only
through the browser and HTTP command API. A dedicated CLI message-step command
is explicitly deferred until the stepping lifecycle and correlation contract
have proven stable.

## 1. Review of the Previous Plan

### 1.1 Strong parts to preserve

The previous plan correctly proposed:

- an optional web adapter;
- immutable/versioned snapshots;
- a serialized command service;
- localhost-only binding by default;
- reuse of GraphX metrics, diagnostics, commands, and topology metadata;
- a source-owned FHSS message injection queue rather than arbitrary node-level
  stepping;
- a small TypeScript frontend;
- artifact export and deterministic tests;
- explicit truth-in-labeling for the deterministic CPU FHSS fixture.

### 1.2 Identified gaps

#### Gap A: No runnable webserver until a late phase

The plan deferred HTTP until after several core refactors. This makes
integration risk accumulate and prevents early browser/API validation.

**Industry-standard solution:** use a walking-skeleton/vertical-slice delivery
model. Phase 1 starts a real server, serves real embedded assets, has a health
endpoint, and displays real graph data. Every later phase extends that same
server.

#### Gap B: The first visible product was too ambitious

The previous minimal frontend included status, metrics, diagnostics, commands,
artifacts, and FHSS schedule controls at once.

**Solution:** make the effective JSON graph the first displayable domain object.
It already exists, is deterministic, and naturally introduces topology,
configuration, parameter metadata, validation, and later execution.

#### Gap C: Configuration and runtime parameter state were conflated

GraphX has several related but distinct surfaces:

- the source graph JSON and `nodes[].node_config`;
- the patched effective graph JSON produced by the FHSS demo;
- `IConfigurable::Configure(JsonView)`;
- `IParameterized::GetParameters()`;
- descriptor/config-field validation;
- facade parameter helpers.

These do not prove that arbitrary parameters are safe to mutate while a node is
initialized or running.

**Solution:** model configuration as a versioned document with explicit apply
semantics:

```text
source config -> effective config -> staged config -> validated config
                                               \-> rebuild and run
```

Runtime mutation is forbidden by default. A future node may opt into a
separate, explicit runtime-mutable contract.

#### Gap D: “JSON path” was underspecified

JSONPath is primarily a query language and has multiple incompatible dialects.
It is a poor mutation contract.

**Solution:** use:

- RFC 6901 JSON Pointer to address one value;
- RFC 6902 JSON Patch for atomic multi-edit requests;
- JSON Merge Patch only for whole-object convenience operations, if needed.

Examples:

```text
/nodes/0/node_config/iq_center_frequency_hz
/nodes/2/node_config/receiver_frequency_indices/24
```

Human-friendly node-id addressing should be translated to canonical JSON
Pointer paths:

```text
node:channelizer/node_config/iq_center_frequency_hz
    -> /nodes/2/node_config/iq_center_frequency_hz
```

The persisted/audited form remains the canonical pointer.

#### Gap E: Node identity and provenance were incomplete

Array positions are unstable after graph edits. The UI needs stable node ids
and must distinguish configured, runtime-reported, default, and staged values.

**Solution:** every node view includes:

- stable graph node `id`;
- configured `node_config`;
- runtime-reported parameters, if available;
- descriptor/config schema;
- value provenance;
- staged differences;
- whether a rebuild is required.

#### Gap F: Parameter update concurrency was not defined

Two browser tabs or CLI/UI operations could overwrite one another.

**Solution:** use optimistic concurrency:

- every effective/staged graph has a monotonically increasing `config_revision`;
- write commands include `expected_revision`;
- stale updates return HTTP `409 Conflict`;
- command/audit history records old value, new value, path, revision, and source.

#### Gap G: Schema coverage is partial

Not every node necessarily implements `IParameterized`, and descriptor metadata
may be incomplete for complex nested objects.

**Solution:** gracefully layer the sources:

1. configured JSON is always displayable;
2. `IParameterized` supplies current values/descriptions where available;
3. descriptor fields supply basic type/required metadata;
4. unknown nested JSON remains editable in raw JSON mode;
5. no UI field is presented as runtime-mutable without an explicit capability.

#### Gap H: Rebuild, reset, and failure recovery were vague

Configuration edits can invalidate the graph or require plugin reconstruction.

**Solution:** make rebuild a first-class command with a transactional boundary:

1. validate staged JSON;
2. allow rebuild only from lifecycle states `not_built`, `stopped`,
  `completed`, or `failed`;
3. reject rebuild during active execution with `409 invalid_state`;
4. attempt construction in a replacement executor/session before discarding the
  previous valid stopped session;
5. retain the previous valid effective config if validation/build fails;
6. activate/swap only after successful construction;
7. if old-session destruction fails after successful activation, keep the new
  session active, report `cleanup_failed` diagnostics/telemetry, and block
  further rebuild attempts until explicit cleanup retry or process restart;
8. report validation errors with node id and JSON Pointer.

#### Gap I: API hardening details were incomplete

**Solution:** add standard HTTP behavior:

- `/healthz` and `/readyz`;
- API content type checks;
- request/body limits;
- structured errors;
- `ETag`/revision support for graph resources;
- CSP and other local-web security headers;
- no arbitrary filesystem or shell access;
- ephemeral-port support for tests.

## 2. Repository-Aware Foundations

Reuse these existing GraphX surfaces:

- `fhss_demo.cpp` for source/effective configuration and executor creation;
- `GraphManager::GetNodes()`, `GetEdges()`, edge metadata, and metrics;
- `NodeFacadeAdapterWrapper::GetName()`, `GetType()`, and adapter access;
- `IParameterized` for parameter values and descriptions;
- `IConfigurable` for configuration support;
- node descriptors and config-field validation;
- `IDiagnosable` for versioned node diagnostics;
- `GraphManager::DisplayGraphDOT()` only as an export aid, not the browser data
  model;
- `CommandRegistry`/command capability concepts, while replacing placeholder
  status output with real snapshot-backed commands.

Do not use RTTI wrapper names as graph labels when stable graph ids, configured
names, or facade names are available.

## 3. Architectural Boundaries

```text
                         Browser / CLI
                              |
                    HTTP, WebSocket, commands
                              |
                 EmbeddedDashboardServer adapter
                              |
        +---------------------+----------------------+
        |                     |                      |
 ConfigurationService   SnapshotService      DashboardCommandService
        |                     |                      |
 effective/staged JSON   topology/status/      serialized mutations
 validation/revisions    metrics/diagnostics   and execution control
        +---------------------+----------------------+
                              |
                    GraphRuntimeSession owner
                              |
             GraphExecutor / GraphManager / FHSS controller
```

### 3.1 Generic GraphX components

Suggested namespace and paths:

```text
libgraph/include/graph/dashboard/
  DashboardSnapshots.hpp
  GraphConfigurationService.hpp
  GraphSnapshotCollector.hpp
  DashboardCommandService.hpp
  GraphRuntimeSession.hpp
  ArtifactExportService.hpp

libgraph/src/dashboard/
  GraphConfigurationService.cpp
  GraphSnapshotCollector.cpp
  DashboardCommandService.cpp
  GraphRuntimeSession.cpp
  ArtifactExportService.cpp
```

Generic responsibilities:

- load, retain, patch, validate, diff, and export graph JSON;
- expose topology and node-parameter views;
- own one runtime session and its lifecycle;
- serialize commands;
- collect immutable snapshots;
- export generic artifacts.

### 3.2 FHSS-specific components

```text
libdsp/include/dsp/fhss/dashboard/
  FHSSAuthoritativeScenarioConfig.hpp
  FHSSConfigurationDeriver.hpp
  FHSSCrossNodeValidator.hpp
  FHSSScenarioController.hpp
  IFHSSMessageInjectionSource.hpp
  FHSSMessageInjectionCapability.hpp
  FHSSDashboardSnapshots.hpp
  FHSSDashboardCollector.hpp

libdsp/src/dsp/
  FHSSConfigurationDeriver.cpp
  FHSSCrossNodeValidator.cpp
  FHSSScenarioController.cpp
  FHSSMessageInjectionCapability.cpp
  FHSSDashboardCollector.cpp
```

Responsibilities:

- define the authoritative FHSS scenario configuration;
- deterministically regenerate dependent node configuration;
- reject direct writes to generated configuration paths;
- validate semantic invariants across source, downconverter, channelizer,
  detector, preamble, and assembler nodes;
- schedule validation and progress;
- source discovery and access to the source-owned message injection queue;
- one-message injection per step request;
- end-to-end message correlation through canonical FHSS packet metadata;
- authoritative terminal-result publication keyed by scenario, message, and
  release sequence;
- channel, pulse, decoder, and capture snapshots.

### 3.3 Optional web adapter

```text
libgraph/include/graph/web/
  EmbeddedDashboardServer.hpp
  DashboardHttpApi.hpp
  DashboardWebSocketHub.hpp
  EmbeddedAssets.hpp

libgraph/src/web/
  EmbeddedDashboardServer.cpp
  DashboardHttpApi.cpp
  DashboardWebSocketHub.cpp

web/dashboard/
  package.json
  src/
  public/
```

The web layer reads immutable snapshots and submits commands. It never mutates
`GraphManager` or nodes directly.

## 4. Configuration and Parameter Model

### 4.1 Configuration states

```text
SourceGraph
    |
extract authoritative FHSS scenario + deterministic derivation
    v
EffectiveGraph revision N
    |
patch authoritative/user-owned paths
    v
StagedAuthoritativeConfig
    |
regenerate all derived fields and validate cross-node invariants
    v
StagedEffectiveGraph revision N+1
    |
validation + executor construction
    v
ActiveGraph revision N+1
```

### 4.2 Authoritative and derived FHSS configuration

There is one authoritative FHSS scenario document. For compatibility with the
current canonical graph, it is sourced from the
`FHSSSyntheticIqSourceNode.node_config`, but it is exposed by the configuration
service as a first-class logical resource:

```text
/fhss/scenario
```

The service maps this logical resource to the source node by stable node id,
never by `nodes[]` array position. `FHSSConfigurationDeriver` replaces the
ad-hoc patching currently performed by `PatchNodeConfigs()` in
`fhss_demo.cpp`. CLI, HTTP, tests, and graph construction all call the same
deriver.

#### Authoritative user-owned fields

These fields are writable only through `/fhss/scenario`. A request that uses
the corresponding source-node JSON Pointer is normalized to the same
authoritative resource before validation and audit logging; it is not a second
copy of the configuration:

```text
messages
iq_center_frequency_hz
iq_offsets
idle_mode
idle_duration_samples
occupied_bandwidth_hz
max_abs_cfo_hz
enable_noise
enable_doppler
enable_multipath
allow_overlap
```

Within `messages[]`, these fields are authoritative:

```text
message_id
transmit_start_sample
pulses[].frequency_index
pulses[].value
pulses[].role
```

`message_id` values must be unique within the scenario.

#### Generated fields

These values are deterministic projections of the authoritative scenario and
are read-only:

| Target node/field | Derivation |
|---|---|
| `FHSSSyntheticIqSourceNode.active_frequency_indices` | Sorted unique frequency indices in the first 16 preamble pulses; exactly four required. |
| `FHSSPreambleDetectorNode.active_frequency_indices` | Copy of generated source active frequencies. |
| `FHSSPreambleDetectorNode.preamble_pulses` | First message’s first 16 pulses mapped from `value` to `word_value`. |
| Preamble detector RF/impairment fields | Copy `iq_center_frequency_hz`, `occupied_bandwidth_hz`, `max_abs_cfo_hz`, and `allow_overlap` from the authoritative scenario. |
| `FHSSMessageAssemblerNode.node_config` | Generated scenario projection including messages, generated active frequencies, generated preamble pulses, and fixed `truth_from_fixture=true`. |
| `ChannelizerNode.transmitted_active_frequency_indices` | Copy of generated active frequencies. |
| `ChannelizerNode.transmitted_pulse_frequency_indices` | Ordered frequency indices from every scheduled pulse, preserving message and pulse order. |
| Channelizer transmitter-description fields | Copy authoritative `iq_center_frequency_hz`, `occupied_bandwidth_hz`, and `max_abs_cfo_hz`. |

The following channelizer receiver fields remain independently user-owned graph
configuration and are not overwritten by schedule regeneration:

```text
receiver_frequency_indices
channel_ids
sample_rate_hz
channel_sample_rate_hz
decimation_factor
filter_group_delay_input_samples
iq_capture
```

`truth_from_fixture` is a fixed system-generated field for this deterministic
fixture and cannot be changed through the dashboard.

#### Read-only behavior

- Direct JSON Pointer writes to generated paths return
  `409 derived_field_read_only`.
- The error identifies the authoritative path that should be edited instead.
- Raw JSON mode visually labels generated values and does not provide an edit
  control for them.
- A full-document JSON Patch is rejected atomically if any operation targets a
  generated path.
- Exported effective configuration contains generated fields so it remains
  directly runnable.
- Exported source/authoritative configuration contains only user-owned scenario
  fields plus schema/version metadata.

#### Import and migration behavior

- A native authoritative document declares its FHSS scenario schema version;
  unknown required fields or unsupported major versions are rejected.
- When loading a legacy/full effective graph, extract authoritative fields only
  from the source node, regenerate all dependent fields, and compare the
  regenerated projection with the supplied effective graph.
- A mismatch rejects normal load with `derived_projection_mismatch`; the
  service must not silently choose between conflicting copies.
- An explicit offline normalization/migration operation may emit a canonical
  effective graph and a machine-readable diff, but it does not build or run
  until the user accepts the result.
- Generated fields supplied in an authoritative-only document are rejected
  rather than ignored.
- The audit record stores the authoritative schema version, deriver version,
  input hash, generated effective hash, and normalization/migration status.

### 4.3 Regeneration triggers and transaction

Any accepted change under `/fhss/scenario` triggers complete deterministic
regeneration. Regeneration is not performed field-by-field.

The transaction is:

```text
check expected revision
  -> clone authoritative scenario
  -> apply the complete JSON Patch atomically
  -> validate authoritative scenario
  -> derive active frequencies and preamble
  -> regenerate every dependent node configuration
  -> run cross-node semantic validation
  -> produce canonical diff
  -> commit authoritative + effective documents as one new revision
```

If any stage fails:

- neither authoritative nor effective configuration changes;
- no partial derived writes are visible;
- the revision does not advance;
- the response contains the failed validation `level`, `node_id`, JSON
  Pointer, error `code`, and human-readable `message` (with generated target
  pointer when applicable).

The same authoritative input and deriver version must produce byte-identical
canonical generated JSON.

Regeneration triggers include edits to:

- any message, pulse, role, value, frequency, message id, or start sample;
- IQ center or offset metadata;
- bandwidth/CFO configuration;
- idle behavior;
- impairment and overlap flags.

Edits to receiver-only channelizer fields or `iq_capture` do not regenerate the
scenario projection, but they still run cross-node validation.

### 4.4 Cross-node semantic validation

Before a staged effective graph can be validated, rebuilt, exported as active,
or executed, all of these conditions must hold:

1. The graph contains exactly one authoritative
   `FHSSSyntheticIqSourceNode`, one preamble detector, one assembler, and one
   canonical channelizer.
2. The scenario has at least one message; message ids are unique.
3. Each message has 16–256 pulses; its first 16 pulses have role `preamble`;
   later pulses have role `body`.
4. The first message’s preamble contains exactly four distinct selectable
   frequency indices in `[1,62]`.
5. Every later message has the same 16-pulse preamble frequency/value sequence
   as the first message because the canonical graph has one shared preamble
   detector configuration.
6. Every scheduled pulse uses one of those four active frequencies.
7. Scheduled messages are ordered and non-overlapping unless overlap support is
   explicitly implemented. With the current fixture, `allow_overlap=true`,
   noise, Doppler, or multipath requests are rejected using the canonical
   unsupported-impairment status.
8. Generated active-frequency and preamble values match the source, preamble
   detector, assembler, and channelizer projections exactly.
9. Channelizer
   `transmitted_pulse_frequency_indices` exactly matches the flattened ordered
   schedule.
10. Channelizer `receiver_frequency_indices` and `channel_ids` have equal
   lengths, unique mappings, and include all active transmit frequencies.
11. The downconverter input IQ center matches the source IQ center; its output
    IQ center matches the channelizer IQ center. Translation/passthrough values
    must satisfy the existing downconverter validation contract.
12. Source, preamble detector, assembler, and channelizer bandwidth/CFO
    metadata agree after regeneration.
13. The canonical 64-output-port and one-detector-per-frequency topology
    invariants remain intact.

Validation reports use stable codes such as:

```text
duplicate_message_id
invalid_preamble_length
inconsistent_message_preamble
invalid_active_frequency_set
pulse_frequency_not_active
scheduled_messages_overlap
unsupported_impairment
derived_projection_mismatch
channelizer_mapping_mismatch
frequency_reference_mismatch
canonical_topology_violation
```

### 4.5 Node parameter view

Each topology node returns:

```json
{
  "id": "channelizer",
  "name": "ChannelizerNode",
  "type": "ChannelizerNode",
  "lifecycle_state": "not_built",
  "capabilities": {
    "configurable": true,
    "parameterized": true,
    "diagnosable": false,
    "runtime_mutable": false
  },
  "parameters": {
    "configured": {},
    "runtime_reported": {},
    "staged": {},
    "descriptions": {},
    "provenance": {
      "iq_center_frequency_hz": "effective_config"
    }
  },
  "ports": {
    "inputs": [],
    "outputs": []
  }
}
```

The node dropdown initially shows a compact summary and expands to tabs:

- Parameters
- Raw `node_config`
- Parameter metadata/schema
- Ports and connected edges
- Diagnostics, when later available
- Configuration diff

### 4.6 CLI parameter commands

Add configuration-oriented commands to the FHSS demo or a reusable GraphX CLI
wrapper:

```bash
# Read one value
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --get-config /nodes/2/node_config/iq_center_frequency_hz \
  --no-run

# Stage one JSON value. The value is parsed as JSON, not as a plain string.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --set-config /fhss/scenario/iq_center_frequency_hz=1240000000.0 \
  --write-effective-config /tmp/effective.json \
  --no-run

# Prefer a stable node-id alias for humans.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --set-node-config channelizer:/iq_center_frequency_hz=1240000000.0 \
  --write-effective-config /tmp/effective.json \
  --no-run

# Apply an atomic RFC 6902 patch file.
graphx-dsp-fhss-demo \
  --graph-config config.json \
  --config-patch changes.json \
  --validate-config \
  --write-effective-config /tmp/effective.json \
  --no-run
```

Recommended parsing rule:

```text
PATH=JSON_VALUE
```

Everything after the first `=` is parsed as JSON. This preserves booleans,
numbers, arrays, objects, strings, and null without type guessing.

Do not use shell-like dotted paths as the canonical format. If a convenience
syntax is added, translate it to JSON Pointer and show the translated path.

CLI scope in this plan is limited to configuration inspection, staged
configuration editing, validation, export, and read-only status/metrics
queries. Do not add a CLI `step-message`, `continue`, or equivalent interactive
FHSS execution command in this initiative. Those commands remain available
through the dashboard HTTP API and browser controls first. Reconsider a CLI
adapter only after the source injection queue, per-message completion
correlation,
cancellation, timeout, and reset semantics are stable and covered by
end-to-end tests.

### 4.7 Parameter update API

```text
GET   /api/v1/config
GET   /api/v1/config/authoritative
GET   /api/v1/config/effective
GET   /api/v1/config/derived-paths
GET   /api/v1/config/value?pointer=/nodes/2/node_config/...
GET   /api/v1/nodes/{nodeId}
GET   /api/v1/nodes/{nodeId}/parameters
PATCH /api/v1/config
POST  /api/v1/config/validate
POST  /api/v1/config/rebuild
POST  /api/v1/config/export
```

Single update:

```json
{
  "schema": "graphx.dashboard.config_update.v1",
  "command_id": "cmd-config-17",
  "expected_revision": 4,
  "pointer": "/fhss/scenario/iq_center_frequency_hz",
  "value": 1240000000.0,
  "apply": "staged"
}
```

Atomic JSON Patch:

```json
{
  "schema": "graphx.dashboard.config_patch.v1",
  "command_id": "cmd-config-18",
  "expected_revision": 4,
  "operations": [
    {
      "op": "replace",
      "path": "/fhss/scenario/iq_center_frequency_hz",
      "value": 1240000000.0
    }
  ]
}
```

Response:

```json
{
  "schema": "graphx.dashboard.config_result.v1",
  "command_id": "cmd-config-18",
  "status": "staged",
  "old_revision": 4,
  "new_revision": 5,
  "rebuild_required": true,
  "regenerated_targets": [
    {
      "node_id": "fhss_source",
      "pointer": "/node_config"
    },
    {
      "node_id": "fhss_preamble_detector",
      "pointer": "/node_config"
    },
    {
      "node_id": "fhss_message_assembler",
      "pointer": "/node_config"
    },
    {
      "node_id": "channelizer",
      "pointer": "/node_config"
    }
  ],
  "validation": {
    "valid": true,
    "levels": [
      "syntax",
      "structure",
      "semantic",
      "descriptor"
    ],
    "errors": []
  }
}
```

### 4.8 Validation levels and error schema

Validation is reported with explicit levels:

```text
level: syntax | structure | semantic | descriptor | construction | initialization
```

Level intent:

- `syntax`: JSON parse/type-shape failures for incoming documents/patches.
- `structure`: graph/document structure validity (required sections, node/edge
  shape, pointer targets).
- `semantic`: FHSS and cross-node invariants.
- `descriptor`: descriptor/config-field validation.
- `construction`: graph/session construction failures before activation.
- `initialization`: node initialization failures after successful construction.

Each validation error record must include:

- `level`
- `node_id` (or `graph` for non-node-scoped failures)
- `pointer` (RFC 6901 JSON Pointer)
- `code` (stable machine-readable error code)
- `message` (human-readable explanation)

Optional fields allowed when relevant: `generated_target_pointer`,
`authoritative_pointer`, `details`, and `retriable`.

Example error record:

```json
{
  "level": "semantic",
  "node_id": "fhss_message_assembler",
  "pointer": "/fhss/scenario/messages/3/pulses/8/frequency_index",
  "code": "fhss.frequency_index_out_of_range",
  "message": "frequency_index must be in [1,62]"
}
```

### 4.9 Apply modes

- `staged`: update the staged document only.
- `validate`: stage and run graph/config validation.
- `rebuild`: validate and construct a replacement runtime session.
- `runtime`: unavailable unless a future explicit runtime-mutable capability
  declares the path safe for the current lifecycle state.

`IConfigurable` alone is not evidence that running-node mutation is safe.

## 5. First Displayable Dashboard

The initial page is a graph/configuration explorer.

```text
+---------------------------------------------------------------+
| GraphX FHSS Dashboard | config revision 1 | not running       |
+-------------------------------+-------------------------------+
| Graph canvas/list             | Selected node                 |
|                               | channelizer                   |
| source -> downconverter       | [Parameters v]                |
|        -> channelizer         | [Raw JSON v]                  |
|        -> detector_00 ...     | [Schema v]                    |
|                               | [Ports/Edges v]               |
+-------------------------------+-------------------------------+
| Validation: valid | staged changes: 0 | Export | Build & Run  |
+---------------------------------------------------------------+
```

For the 75-node FHSS graph, the UI must support:

- pan/zoom or a virtualized hierarchical list;
- collapse of the 64 repeated detector nodes into a visual group;
- expansion to individual detector nodes;
- search/filter by id or type;
- deterministic layout;
- raw JSON fallback.

The API—not the rendered SVG—is the source of truth.

## 6. Initial API

Available in Step 1 (first runnable phase, read-only):

```text
GET  /healthz
GET  /readyz
GET  /
GET  /api/v1/version
GET  /api/v1/graph
GET  /api/v1/config
GET  /api/v1/nodes/{nodeId}
GET  /api/v1/nodes/{nodeId}/parameters
```

Added in Step 2 (staged mutation and validation):

```text
PATCH /api/v1/config
POST  /api/v1/config/validate
POST  /api/v1/config/undo
POST  /api/v1/config/discard
POST  /api/v1/config/export
```

Later additions (Step 3+):

```text
POST /api/v1/config/rebuild
GET  /api/v1/operations/{operationId}
POST /api/v1/operations/{operationId}/cancel
GET  /api/v1/status
GET  /api/v1/metrics
GET  /api/v1/metrics/edges
GET  /api/v1/diagnostics
GET  /api/v1/fhss/scenario
GET  /api/v1/fhss/messages
GET  /api/v1/fhss/channels
GET  /api/v1/fhss/pulses

POST /api/v1/commands/start
POST /api/v1/commands/stop
POST /api/v1/commands/step-message
POST /api/v1/commands/continue
POST /api/v1/commands/reset
POST /api/v1/commands/capture
POST /api/v1/commands/export

WS   /api/v1/events
```

### 6.1 Command execution contract (automation default)

For command-style endpoints (`/api/v1/commands/*`, rebuild, export, capture),
use one consistent asynchronous contract:

- server returns `202 Accepted` with `operation_id`, `command_id`, `status`,
  and `submitted_revision`;
- `GET /api/v1/operations/{operation_id}` returns terminal status, result
  summary, and structured failure details when applicable;
- WebSocket events mirror operation state transitions (`queued`, `running`,
  `succeeded`, `failed`, `cancelled`);
- idempotency key behavior is preserved through `command_id` for safe retries;
- short synchronous completion is allowed, but the response schema remains the
  same and includes a terminal status.

Commands must never block HTTP worker threads for long-running execution.

## 7. FHSS Message Injection and Stepping Model

Definitions:

- A scheduled FHSS message is a logical entry in `messages[]`.
- A GraphX token is a transport object on one graph edge.
- A pulse is one preamble/body pulse within a scheduled message.
- An executor lifecycle operation is not a dashboard step.

The current source emits one whole generated fixture. Refactor the canonical
source to follow the existing GraphX CSV data-injection strategy:

1. `FHSSSyntheticIqSourceNode` owns an
   `ActiveQueue<FHSSMessageInjectionRequest>`.
2. The source exposes that queue through a narrow
   `IFHSSMessageInjectionSource` capability.
3. `DataInjectionPolicy`-style discovery registers the source queue in an
   `FHSSMessageInjectionCapability`.
4. The asynchronous dashboard `step-message` command selects exactly one
   pending scheduled message and enqueues one injection request.
5. The source's `Produce()` call blocks in `ActiveQueue::Dequeue()` while no
   request is available. This is the stopped-between-messages behavior for
   stepping; the producer thread remains alive.
6. Enqueuing a request wakes `Produce()`, which generates and returns one
   `FHSSSyntheticIqToken`.
7. `Produce()` returns `std::nullopt` only when the injection queue is disabled
   because the schedule is exhausted, the scenario is explicitly ended, or
   runtime shutdown is requested.

```text
HTTP step command
    -> DashboardCommandService
    -> FHSSScenarioController
    -> FHSSMessageInjectionCapability
    -> source-owned ActiveQueue<FHSSMessageInjectionRequest>
    -> blocked Produce() wakes
    -> one FHSSSyntheticIqToken enters the canonical graph
```

`FHSSMessageInjectionRequest` supports two request forms:

- `ScheduledMessage`: generate one selected scheduled FHSS message. Dashboard
  stepping uses this form.
- `WholeSchedule`: generate the existing complete configured fixture in one
  token. Normal non-dashboard execution uses this form to preserve current
  behavior.

Both modes use the same source queue and `Produce()` implementation. A request
may carry `end_of_stream_after_produce=true`. After dequeuing such a request,
the source disables its queue and then returns the generated token. The next
`Produce()` call observes the disabled queue and returns `std::nullopt`.
Therefore pending requests are never discarded by disabling a queue that still
contains work.

Every `ScheduledMessage` request also carries the immutable correlation
identity assigned before enqueue:

```cpp
struct FHSSMessageCorrelation {
    std::string scenario_id;
    std::uint64_t message_id = 0;
    std::uint64_t release_sequence = 0;
};

struct FHSSMessageInjectionRequest {
    FHSSMessageInjectionKind kind;
    FHSSMessageCorrelation correlation;
    FHSSScheduledMessageSpec scheduled_message;
    bool end_of_stream_after_produce = false;
};
```

`scenario_id` is required for dashboard-managed runs, generated once when a
scenario is loaded/reset, and remains stable for that scenario lifecycle.
`message_id` comes from the configured schedule and must be unique within the
scenario. `release_sequence` is assigned monotonically by the scenario
controller each time a message is accepted for injection; it distinguishes a
message replay from its previous release.

```cpp
std::optional<FHSSSyntheticIqToken>
FHSSSyntheticIqSourceNode::Produce(...) {
    FHSSMessageInjectionRequest request;
    if (!message_queue_.Dequeue(request)) {
        // Queue disable means end-of-stream or shutdown.
        return std::nullopt;
    }

    auto token = GenerateSyntheticIq(config_, request);
    if (request.end_of_stream_after_produce) {
        message_queue_.Disable();
    }
    return token;
}

bool FHSSScenarioController::StepOneMessage() {
    auto request = MakeRequestForNextPendingMessage();
    return message_injection_capability_->Enqueue(request);
}
```

Message state:

```text
Pending -> Queued -> Producing -> InFlight -> Completed
                                          \-> Rejected
                                          \-> TimedOut
```

A `step-message` command succeeds only when exactly one queued message reaches
a terminal state at the FHSS sink/controller. Source emission alone is not
completion.

### 7.1 Queue and flow-control contract

- Default source injection queue capacity is one request for manual stepping.
- At most one message is in flight in manual-step mode.
- A second step request while a message is queued or in flight returns
  `409 message_in_flight`; it does not queue an unbounded backlog.
- `continue` changes the controller to automatic mode. It enqueues the next
  message only after the prior message reaches a terminal state, optionally
  waiting for the configured interval.
- Queue enqueue failure is reported as a command failure and does not advance
  scenario state.
- The controller never disables a queue that still contains pending requests;
  GraphX `ActiveQueue::Disable()` gives disable priority over queued elements.
- Reset is allowed only when no message is in flight in v1. It clears queued
  requests, resets the schedule cursor, and re-enables the queue or rebuilds
  the session according to the runtime lifecycle contract.
- Stop and shutdown disable the source queue, waking a blocked `Produce()`.
- Disabling the queue is terminal for that source lifecycle. Restart/reset after
  disable requires explicit queue re-enable before the producer is started, or
  a rebuilt runtime session.
- Queue depth, enqueue/dequeue counts, and wait time are included in FHSS source
  diagnostics.

### 7.2 Completion contract

Message completion and graph completion are distinct:

- Every injection request carries `scenario_id`, scheduled `message_id`, and
  monotonic `release_sequence`.
- These identifiers are semantic packet metadata, not dashboard-only state.
- The source copies the correlation envelope into
  `FHSSSyntheticIqOutputPacket`.
- Downconverter and channelizer outputs copy the envelope unchanged. Fan-out to
  all 64 channel outputs preserves the same envelope.
- Detector, merge, candidate, CPSM branch-metric, Viterbi, decoded-word, and
  message-assembly stages preserve the envelope. Where multiple pulse records
  are collected or merged, each pulse metadata record carries the envelope and
  the node rejects mixed-correlation aggregation.
- `FHSSAssembledMessagePacket` includes the correlation envelope directly.
- The sink publishes a per-message completion event for every terminal message.
- Per-message completion does not stop the graph.
- The final scheduled-message request is marked
  `end_of_stream_after_produce=true`; after dequeuing it, the source disables
  the now-empty queue and returns the final token.
- The source output thread may reach end-of-stream while the final token is
  still moving through downstream nodes. This does not by itself complete or
  stop the graph.
- Graph completion is signaled only after final-message completion and source
  end-of-stream, or after explicit stop/failure/timeout.

Required packet-contract changes in `FHSSGraphXPackets.hpp`:

```cpp
struct FHSSMessageCorrelation {
    std::string scenario_id;
    std::uint64_t message_id = 0;
    std::uint64_t release_sequence = 0;
};

// Add `FHSSMessageCorrelation correlation{};` to the semantic packet path:
// - FHSSSyntheticIqOutputPacket
// - FHSSDownconvertedIqPacket
// - FHSSChannelizedIqPacket
// - FHSSPerChannelPulseEvidencePacket
// - FHSSDetectedPulseEvidencePacket
// - FHSSPulseCandidateEvidencePacket
// - FHSSCpsmBranchMetricPacket
// - FHSSCpsmSymbolDecisionPacket
// - FHSSDecodedPulseWordsPacket
// - FHSSAssembledMessagePacket
// - FHSSDiagnosticsPacket
```

If an intermediate structure can contain records from more than one input,
each constituent pulse/candidate record also carries correlation metadata and
the node verifies that all records selected for one output share the same
tuple. A default/empty correlation is allowed only for legacy non-dashboard
fixtures during migration; dashboard `ScheduledMessage` requests require a
fully populated correlation tuple.

### 7.3 Authoritative terminal-result contract

Do not infer completion identity from pulse timing, source token id, queue
position, message order, or “last message seen.” Every terminal record is keyed
by the full correlation tuple:

```cpp
enum class FHSSTerminalState {
    Completed,
    Rejected,
    TimedOut,
    Cancelled,
    Failed
};

struct FHSSMessageTerminalResult {
    FHSSMessageCorrelation correlation;
    FHSSTerminalState terminal_state;
    std::string status_code;
    std::string status_message;

    // Human-readable artifact/event timestamp in UTC.
    std::chrono::system_clock::time_point completed_at_utc;

    // Process-local ordering and latency measurement.
    std::uint64_t completed_at_monotonic_ns = 0;

    FHSSDiagnosticsPacket diagnostics;
};
```

Rules:

- `FHSSMessageSinkNode` emits pipeline terminal results (`Completed`,
  `Rejected`, or `Failed`) with the correlation copied from
  `FHSSAssembledMessagePacket`.
- The scenario controller emits operational terminal results (`TimedOut` or
  `Cancelled`) when no matching sink result arrives or the operation is
  explicitly cancelled.
- A single terminal-result registry owns the atomic first-terminal transition
  for each correlation tuple. The first accepted terminal transition is the
  authoritative operation result.
- The scenario controller maintains pending operations by
  `(scenario_id, message_id, release_sequence)` and completes only the matching
  HTTP operation.
- Duplicate terminal results for the same tuple are rejected and counted in
  diagnostics.
- An unknown tuple is reported as an orphan terminal result; it must not
  complete the currently active step command.
- `completed_at_utc` is serialized as RFC 3339 UTC with fractional seconds.
- Latency is calculated only from monotonic timestamps captured at command
  acceptance, enqueue, source dequeue, source emission, and sink completion.
- A timeout generated by the controller uses the same correlation tuple and
  transitions it once to `TimedOut`; a later sink result is recorded as a late
  result and does not mutate the completed HTTP operation.
- The correlation envelope is validation/trace metadata. Decoder decisions
  must not depend on `scenario_id`, `message_id`, or `release_sequence`.

Dashboard API and WebSocket terminal payload:

```json
{
  "schema": "graphx.fhss.message_terminal.v1",
  "scenario_id": "scenario-01J...",
  "message_id": 17,
  "release_sequence": 4,
  "terminal_state": "completed",
  "status_code": "ok",
  "status_message": "message decoded",
  "completed_at_utc": "2026-06-24T15:42:17.123456Z",
  "completed_at_monotonic_ns": 1844674400,
  "diagnostics": {}
}
```

Do not advertise arbitrary node single-step, executor pause, or live topology
mutation.

In this plan, `step-message` refers to the asynchronous dashboard HTTP command
and its browser control. It does not imply a command-line stepping interface.

## 8. Generic Dashboard Panels

After the graph/config explorer is stable, add:

1. Run status
2. Aggregate and per-edge metrics
3. Node diagnostics
4. Logs and command history
5. Artifact export/browser

All generic panels consume reusable versioned snapshots.

## 9. FHSS-Specific Panels

Add incrementally:

1. Message schedule and step controls
2. 64-channel activity heatmap
3. Expected/detected pulse timeline
4. CPSM/Viterbi decoder view
5. Selected-channel IQ/spectrum view

Always display:

> Deterministic GraphX CPU FHSS fixture. Not a production RF receiver or
> production channelizer.

## 10. Thread and Ownership Model

Rules:

1. One `GraphRuntimeSession` owns executor lifecycle.
2. One serialized command queue performs mutations.
3. HTTP threads only parse requests, read immutable snapshots, and enqueue
   commands.
4. Configuration document updates are protected by revisioned transactions.
5. Snapshot collection never blocks graph worker threads.
6. WebSocket clients receive bounded, rate-limited snapshots/events.
7. Shutdown order:
   - stop accepting HTTP/WebSocket work;
   - close event subscribers;
   - stop snapshot timer;
   - reject/flush commands;
   - stop and join runtime session;
   - join server threads.

Dashboard lifetime and termination defaults:

1. After graph/scenario completion, the dashboard remains alive by default for
  post-run inspection.
2. `--dashboard-exit-on-completion` enables CI-friendly auto-exit once the run
  reaches terminal completion and no command operation is in progress.
3. In `--dashboard-no-run` mode, there is no completion-triggered auto-exit;
  the process remains alive until explicit shutdown.
4. `SIGINT`/`SIGTERM` trigger graceful shutdown using the same shutdown order
  above, including readiness transition to not-ready before teardown.
5. If graceful shutdown exceeds the configured deadline, the process logs the
  timeout and forces termination.

## 11. Technology Recommendation

Use a permissively licensed C++ HTTP/WebSocket library selected through a short
spike. Boost.Beast remains the conservative recommendation when an optional
Boost dependency is acceptable because it provides explicit lifecycle,
HTTP/WebSocket support, limits, and testable asynchronous behavior.

For the frontend:

- TypeScript;
- Vite;
- Cytoscape.js for topology;
- no large application framework for the first slices;
- embed production assets into the executable;
- keep a development mode that serves assets from disk for rapid iteration.

Dependency versions and licenses must be recorded in the repository.

## 12. Build and Runtime Integration

```cmake
option(GRAPHX_BUILD_WEB_DASHBOARD
       "Build the optional embedded GraphX web dashboard"
       OFF)
```

Development run:

```bash
cmake -S . -B build-dashboard \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_BUILD_EXAMPLES_DSP=ON

cmake --build build-dashboard --target dsp_fhss_demo

./build-dashboard/examples/DSP/graphx-dsp-fhss-demo \
  --graph-config libdsp/config/fhss_cpsm_channelized_fixture_500msps.json \
  --plugin-dir build-dashboard/plugins \
  --dashboard \
  --dashboard-port 8080 \
  --dashboard-no-run
```

Naming consistency: CMake target `dsp_fhss_demo` produces executable
`graphx-dsp-fhss-demo`; use the executable name in user-facing run examples.

Required options:

```text
--dashboard
--dashboard-bind 127.0.0.1
--dashboard-port 8080
--dashboard-port 0              # ephemeral test port
--dashboard-run-dir PATH
--dashboard-no-run              # inspect/edit graph before execution
--dashboard-exit-on-completion  # CI convenience; default is persist after completion
--dashboard-open-browser        # optional convenience, default off
```

If dashboard support was not compiled, `--dashboard` returns a clear error.

## 13. Runnable Vertical-Slice Plan

Every phase below must satisfy the common “server remains runnable” contract:

- executable starts with `--dashboard`;
- static page loads;
- `/healthz` returns 200;
- `/readyz` accurately reports readiness;
- server supports port `0`;
- clean shutdown is tested;
- existing headless FHSS behavior remains available.

### Step 1 — Server shell and effective JSON graph display

Deliver:

- optional server dependency/build target;
- embedded minimal HTML/CSS/JS;
- health/readiness/version endpoints;
- load and retain the FHSS effective graph without executing it;
- `/api/v1/graph` and `/api/v1/config`;
- graph node/edge display;
- per-node dropdown with declarative data only in Step 1: configured
  `node_config`, ids/types, and declared ports/edges;
- deterministic grouping of 64 detector nodes;
- real graph ids and types.

CLI:

- `--dashboard-no-run`
- `--get-config`
- `--write-effective-config`

Acceptance:

- browser displays the canonical JSON graph;
- selecting every node is safe, including nodes without parameters;
- curl can retrieve the same graph JSON;
- server is runnable and useful before executor integration.

### Step 2 — Parameter metadata and staged configuration editing

Deliver:

- first-class `/fhss/scenario` authoritative resource extracted from the
  source-node configuration;
- `FHSSConfigurationDeriver` as the only code path that projects the
  authoritative scenario into source, preamble detector, assembler, and
  channelizer effective configurations;
- generated-path metadata used by HTTP, CLI, and the node parameter dropdown
  to mark derived values read-only;
- `FHSSCrossNodeValidator` with stable error codes and pointers;
- construct an unstarted inspection graph/session to query runtime descriptors
  and `IParameterized` values without starting normal execution;
- parameter-fidelity audit and fixes for `IParameterized` implementations before
  labeling values as `current` (known example: ensure
  `FHSSSyntheticIqSourceNode` reports configured state rather than template
  defaults);
- `IParameterized` values/descriptions where available;
- descriptor/config-field metadata;
- JSON Pointer read/update;
- JSON Patch transaction support;
- config revision and `409` stale-write handling;
- staged diff, undo last edit, discard all edits;
- validation errors tied to node id and pointer;
- export staged/effective JSON.

CLI:

- `--set-config PATH=JSON`
- `--set-node-config NODE_ID:POINTER=JSON`
- `--config-patch FILE`
- `--validate-config`

Acceptance:

- the same update produces equivalent output through CLI and HTTP;
- editing an authoritative schedule or RF field regenerates every dependent
  effective node configuration in the same revision;
- direct edits to generated paths fail atomically with
  `409 derived_field_read_only` and identify the authoritative replacement
  pointer;
- a derivation or cross-node validation failure leaves both authoritative and
  effective documents, and the revision, unchanged;
- authoritative and effective configuration exports can each be reloaded, and
  the effective export is directly runnable;
- the webserver remains runnable and displays the staged authoritative,
  generated, and effective values before runtime integration;
- values are labeled `current` only after implementation-specific fidelity audit
  confirms they reflect configured/runtime state; otherwise they are labeled as
  runtime-reported defaults/templates with provenance;
- invalid type/unknown field is rejected by existing descriptor/config
  validation where metadata exists;
- no running node is silently reconfigured.

### Step 3 — Transactional graph build and generic runtime status

Deliver:

- `GraphRuntimeSession`;
- validate/build/rebuild commands;
- rebuild lifecycle policy with explicit allowed states and `409 invalid_state`
  rejection during execution;
- retain previous valid config on failure;
- real lifecycle status;
- start/stop/execute behavior appropriate to FHSS completion;
- browser Build & Run controls.

Acceptance:

- edited config can build and run;
- rebuild is accepted only from `not_built`, `stopped`, `completed`, or
  `failed` states;
- rebuild during execution is rejected with `409 invalid_state` and leaves
  runtime/session state unchanged;
- invalid rebuild does not destroy the last valid configuration/session;
- replacement session is fully constructed before previous valid stopped
  session is discarded;
- session activation occurs only after successful construction;
- old-session destruction failure after activation leaves the new session
  active, emits `cleanup_failed`, and blocks additional rebuilds until cleanup
  retry or restart;
- runtime status is no longer placeholder text.

### Step 4 — Generic metrics, topology activity, and diagnostics

Deliver:

- aggregate GraphMetrics snapshot;
- per-edge metrics using edge metadata;
- node diagnostics enumeration;
- topology edge coloring;
- CLI commands backed by the same snapshot services.

Acceptance:

- browser and CLI report the same snapshot values;
- no dashboard-only metrics implementation;
- server remains runnable with graph stopped or completed.

### Step 5 — FHSS scenario controller and queue-driven one-message stepping

Deliver:

- scenario controller;
- source-owned blocking message injection queue;
- injection-source discovery/capability modeled after CSV data injection;
- `ScheduledMessage` and compatibility `WholeSchedule` injection requests;
- canonical `FHSSMessageCorrelation` packet metadata;
- `FHSSAssembledMessagePacket` correlation fields;
- authoritative `FHSSMessageTerminalResult` registry, with pipeline results
  from the sink and operational timeout/cancellation results from the
  controller;
- one configured message per step;
- message state correlation to terminal sink result;
- reset and continue;
- non-web controller tests;
- asynchronous HTTP command endpoints and browser controls;
- no CLI message-step or continue command.

Acceptance:

- one step releases exactly one scheduled FHSS message;
- an empty enabled injection queue blocks `Produce()` rather than ending the
  source;
- queue disable/end-of-schedule is the only normal path to `nullopt`;
- normal non-dashboard execution enqueues one `WholeSchedule` request and
  preserves the current one-token fixture behavior;
- the stepped path preserves `scenario_id`, `message_id`, and
  `release_sequence` from injection through the assembled packet and terminal
  sink result;
- the controller completes an operation only from an exact correlation-tuple
  match;
- at most one message is queued or in flight in manual-step mode;
- per-message completion does not stop the graph;
- canonical topology remains singular;
- default non-dashboard run preserves whole-fixture behavior.
- message stepping is exercised through the controller API, HTTP API, and
  browser without requiring a CLI stepping surface.

### Step 6 — WebSocket live snapshots

Deliver:

- `/api/v1/events`;
- status, metrics, diagnostics, command, and FHSS progress events;
- bounded subscriber queues, coalescing, and rate limits;
- reconnect with latest snapshot.
- event envelope fields: `schema`, `event_type`, `sequence`, `timestamp`,
  optional `revision`, and event `payload`.
- reconnect cursor via last-seen `sequence` with explicit resync-required
  signaling when retention is exceeded.

Acceptance:

- sequence numbers are monotonic;
- slow clients cannot block runtime threads;
- clean shutdown closes clients.

### Step 7 — FHSS schedule, channel heatmap, and pulse timeline

Deliver:

- schedule panel;
- 64-channel summary;
- expected/detected timeline;
- confidence/rejection visualization;
- pagination/windowing.

Acceptance:

- deterministic fixture renders consistently;
- snapshot sizes and refresh rates remain bounded.

### Step 8 — Decoder and signal investigation views

Deliver:

- branch/path margin and Viterbi diagnostics;
- selected-channel preview;
- opt-in SigMF capture;
- artifact bundle.

Acceptance:

- no raw full-run IQ is streamed through JSON;
- capture paths stay inside approved run directory;
- fixture truth-in-labeling remains visible.

## 14. Test Strategy

### Common server tests for every step

- starts on ephemeral port;
- `/healthz`;
- `/readyz`;
- root asset load;
- malformed request handling;
- request size limit;
- clean shutdown and thread join;
- dashboard-disabled build/headless regression.

### Graph/config tests

- topology JSON matches effective graph ids/edges;
- detector grouping does not alter API data;
- parameter dropdown handles missing `node_config`;
- JSON Pointer escaping (`~0`, `~1`);
- arrays and nested objects;
- atomic patch rollback;
- stale revision conflict;
- CLI/HTTP equivalence;
- export/reload round trip;
- strict legacy/full-graph import rejects inconsistent derived projections;
- explicit normalization produces a canonical effective graph plus diff
  without starting a runtime;
- descriptor validation error path;
- known authoritative scenario produces the expected canonical source,
  preamble detector, assembler, and channelizer projections;
- every authoritative trigger category causes complete regeneration;
- direct HTTP and CLI writes to every generated path category are rejected;
- failed derivation and cross-node validation roll back both documents without
  advancing the revision;
- identical authoritative input plus deriver version produces byte-identical
  canonical effective JSON;
- generated active frequencies, shared preamble, and flattened channelizer
  pulse ordering match the schedule;
- later-message preamble mismatches are rejected;
- each cross-node semantic validation rule reports its stable code, node id,
  authoritative pointer, and generated target pointer where applicable;
- deterministic replay controls: fixed seed, fixed clock source, UTC timezone,
  and stable locale for CI runs.

### Runtime tests

- transactional rebuild;
- previous valid session retained on failure;
- real status transitions;
- metrics/diagnostics snapshot consistency.

### FHSS tests

- one step equals one scheduled message;
- source blocks between steps and resumes after queue injection;
- `Produce()` does not return `nullopt` merely because the queue is temporarily
  empty;
- disabling a queue with pending requests is forbidden and tested;
- queue disable wakes the source and ends production;
- duplicate/concurrent step request is rejected while one message is queued or
  in flight;
- correlation survives every canonical FHSS stage and 64-way channel fan-out;
- packet-contract tests assert correlation fields exist on every canonical
  semantic packet listed in Section 7.2;
- source-to-sink tests assert byte-for-byte/equality preservation of the
  correlation tuple;
- merge/assembly rejects mixed-correlation inputs;
- replaying the same `message_id` produces a new `release_sequence`;
- duplicate, orphan, and late terminal results do not complete the wrong
  operation;
- terminal records contain terminal state, RFC 3339 UTC completion time, and
  monotonic completion time;
- no test correlates terminal messages solely by pulse timing or release order;
- ordered continue;
- timeout/rejection;
- reset;
- canonical topology unchanged;
- existing FHSS decode regression.
- controller and HTTP stepping behavior are covered without adding a CLI step
  command.

### Browser tests

Use a small browser automation smoke suite:

- load graph;
- select node;
- expand parameter dropdown;
- stage a value;
- observe diff;
- validate;
- export/rebuild;
- later step one message and inspect completion.
- reconnect/gap test: force sequence gap and verify required HTTP resync before
  stream resume.

## 15. Security and Operational Standards

- bind to `127.0.0.1` by default;
- require explicit external binding;
- warn prominently on external binding;
- no shell execution;
- no arbitrary file browsing;
- artifact/config paths restricted to approved roots;
- CSP header default:
  `default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self' ws:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'`.
- `X-Content-Type-Options: nosniff`;
- `X-Frame-Options: DENY`;
- `Referrer-Policy: no-referrer`;
- strict `Content-Type` validation for request and response payloads;
- request body limit: 1 MiB default for JSON endpoints;
- WebSocket frame limit: 256 KiB; max reassembled message: 1 MiB;
- normalized/validated URL paths;
- structured audit log for mutation commands;
- generated command ids when omitted;
- idempotency handling for repeated command ids.

If HTTPS is enabled in any non-localhost deployment profile, WebSocket
connections must use `wss:` and the CSP `connect-src` policy must be updated to
`'self' wss:` for that profile.

Authentication is unnecessary for localhost-only MVP. If external binding is
ever supported beyond trusted development, add token authentication and CSRF
protection before treating it as generally safe.

## 16. Artifact and Audit Model

Each run/export directory may contain:

```text
manifest.json
source-config.json
authoritative-fhss-scenario.json
effective-config.json
staged-config.json
config-patch.json
config-audit.jsonl
fhss-derivation-manifest.json
topology.json
topology.dot
status.json
metrics.json
edge-metrics.json
diagnostics.json
fhss-scenario.json
fhss-message-terminal-results.json
summary.json
captures/
```

The manifest records:

- schema version;
- GraphX revision/build information;
- source/effective config hashes;
- authoritative scenario hash;
- FHSS derivation schema/version and generated-target hashes;
- active config revision;
- command history hash;
- artifact hashes;
- start/end timestamps;
- truth-in-labeling statement;
- terminal-result correlation tuples and completion timestamps.

## 17. Decisions Required Before Implementation

1. HTTP/WebSocket library: use Boost.Beast.
2. Frontend topology library: use Cytoscape.js.
3. Generic core placement under `libgraph`: yes.
4. JSON Pointer and JSON Patch as canonical mutation standards.
5. Rebuild-required default for all configuration edits.
6. Localhost-only MVP.
7. CLI configuration commands live directly in the FHSS demo for now.
8. Dashboard lifetime default: server remains alive after completion for
  inspection; CI/test runs may opt in to auto-exit with
  `--dashboard-exit-on-completion`.

### 17.1 Finalized practical defaults

The following decisions are now explicit defaults for implementation:

- Decision 9: Session ownership model is single-session. One process owns one
  active dashboard/runtime session.

- Decision 10: Snapshot delivery practical default (industry-standard hybrid):
  - Delivery model: hybrid baseline+stream.
    - Clients load canonical baseline state via HTTP snapshot endpoints.
    - Clients then subscribe to `/api/v1/events` for incremental live updates.
    - If stream is unavailable or interrupted, clients fall back to polling
      snapshots until stream is restored.
  - Event contract:
    - Every event includes `schema`, `event_type`, monotonic `sequence`,
      `timestamp`, optional `revision`, and `payload`.
    - Clients track last-seen `sequence` and detect gaps/reordering.
  - Backpressure/drop behavior:
    - Use bounded per-client event queues with coalescing of high-frequency
      snapshot updates.
    - Default per-client queue depth: 128 events.
    - Default coalescing window for snapshot-style updates: 100 ms.
    - Slow clients must not block runtime/command threads.
    - When queue limits are exceeded, drop stale intermediate updates and emit
      an explicit resync-required signal.
  - Gap and reconnect behavior:
    - Reconnect may provide last-seen `sequence`; server resumes only if data is
      still available in retention window.
    - Default event retention window: 120 seconds.
    - Otherwise, server returns/announces resync-required and client reloads
      canonical HTTP snapshots before resubscribing.
  - Operational guardrails:
    - Server exposes counters for dropped/coalesced events and reconnects.
    - CI and soak tests must verify that slow subscribers cannot degrade command
      latency or runtime throughput.

- Decision 11: Configuration persistence policy is memory-only until explicit
  export.

- Decision 12: Mutation conflict practical default (industry-standard
  optimistic concurrency):
  - All mutation requests must include `expected_revision`.
  - If `expected_revision` != current revision, server returns `409 Conflict`
    and applies no partial changes.
  - `409` response includes current `revision`, conflict `code`, and optional
    conflicting `pointer` paths when known.
  - Client flow on conflict: refetch latest config, rebase intent, and retry
    with a new `expected_revision`.
  - Automatic server-side merge is disabled by default in v1 to preserve
    deterministic, auditable behavior.
  - Optional future merge helper may be added only for explicitly proven
    non-overlapping JSON Patch operations and must still produce a single new
    revision plus full audit record.

- Decision 13: API error schema and compatibility policy:
  - Use canonical error schema `graphx.dashboard.error.v1` for all non-2xx
    API responses.
  - Error payload fields: `schema`, `status`, `code`, `message`, `details`,
    `request_id`, optional `command_id`, optional `revision`, optional
    `pointer`, and `retriable`.
  - Compatibility policy for `/api/v1/*`: additive-only changes in v1
    (new optional fields/endpoints). No removals or semantic breaks in v1.
    Any breaking change requires `/api/v2/*`.

- Decision 14: Command/audit retention policy is fixed as follows: default
  in-memory command history is 100 entries (configurable); log files are never
  auto-deleted and must be deleted by the user; the application must indicate
  when a log file is written; no redaction is required.

- Decision 15: Web asset/toolchain practical default:
  - Build tool: Vite.
  - Embed strategy: split assets in development; embed one hashed production
    bundle in release artifacts.
  - Source maps: enabled in debug/development builds only; disabled in release
    builds.

- Decision 16: Browser/API security practical default for localhost mode:
  - CORS: disabled by default (no wildcard origins).
  - Same-origin: required for embedded page and dashboard API by default.
  - Dev-only override: allowed only behind an explicit development flag with a
    localhost-only allowlist and a prominent startup warning.

- Decision 17: Readiness semantics practical default (automation-friendly):
  - `/healthz`: returns 200 whenever the process/event loop is alive.
  - `/readyz`: returns 200 when the dashboard can accept API commands for the
    current mode; returns 503 during transient rebuild/swap windows and startup
    phases where command handling is not yet safe.
  - `--dashboard-no-run`: `/readyz` returns 200 after effective config and
    command services are initialized (execution not required).
  - Rebuild/swap: `/readyz` flips to 503 at rebuild start and back to 200 only
    after the new session/config is active (or the previous valid session is
    restored).

- Decision 18: Deterministic dashboard test baseline practical default
  (automation-friendly):
  - Golden comparison surface: compare canonical JSON API payloads only
    (`/api/v1/version`, `/api/v1/graph`, `/api/v1/config`, and selected stable
    status/snapshot fields), not rendered pixels or browser timing artifacts.
  - Canonicalization: JSON comparisons must sort object keys and ignore
    non-deterministic fields (`timestamp`, `request_id`, `command_id`, elapsed
    timings, ephemeral port values, and host-specific absolute paths).
  - Numeric tolerance defaults:
    - Integer/count fields: exact match required.
    - Boolean/string/enum fields: exact match required.
    - Floating-point fields: pass if `abs(actual - expected) <= 1e-9` for
      normalized/probability-scale values and `<= 1e-6` for signal/metric
      magnitudes unless a stricter endpoint-specific contract is declared.
  - Platform variance rules: macOS/Linux and supported compilers must produce
    identical canonical topology/config JSON; only the explicitly ignored
    non-deterministic fields above may differ.
  - Baseline workflow: maintain one checked-in golden fixture per schema version
    and compare via CI contract tests; intentional changes require an explicit
    golden update in the same PR with a short rationale.

- Decision 19: Rebuild lifecycle practical default (v1):
  - Rebuild is allowed only from lifecycle states `not_built`, `stopped`,
    `completed`, or `failed`.
  - Rebuild request during active execution is rejected with
    `409 invalid_state`.
  - Replacement session construction is attempted before discarding the
    previous valid stopped session.
  - Session activation/swap occurs only after successful construction.
  - If old-session destruction fails after activation, keep the new session
    active, emit `cleanup_failed` diagnostics/telemetry, and block further
    rebuild attempts until explicit cleanup retry or process restart.

- Decision 20: CLI message stepping is deferred:
  - Do not add `--step-message`, `--continue-messages`, an interactive CLI
    prompt, or equivalent command-line execution controls in this dashboard
    initiative.
  - Keep CLI work limited to configuration inspection/editing, validation,
    export, and read-only snapshot queries.
  - Implement message stepping first through the shared scenario controller,
    asynchronous HTTP command API, and browser controls.
  - A future CLI adapter may reuse the same command service only after
    per-message identity, completion, timeout, cancellation, reset, and
    shutdown behavior are stable and fully tested.

- Decision 21: FHSS stepping uses source-owned queue injection:
  - Mirror the established CSV data-injection architecture.
  - `FHSSSyntheticIqSourceNode` owns a blocking `ActiveQueue` of scheduled
    message injection requests.
  - Dashboard step commands enqueue requests through a discovered injection
    capability; they do not call `Produce()` directly.
  - An empty enabled queue means wait between messages.
  - `Produce()` returns `std::nullopt` only after queue disable/end-of-stream or
    shutdown.
  - Dashboard steps enqueue `ScheduledMessage` requests; normal headless mode
    enqueues one compatibility `WholeSchedule` request.
  - The request that represents the last available work marks
    `end_of_stream_after_produce`; the source disables the now-empty queue only
    after dequeuing that request and before returning its token.
  - Manual stepping permits one queued/in-flight message at a time.
  - Per-message completion is separate from scenario/graph completion.

- Decision 22: Terminal message correlation is an end-to-end packet contract:
  - Use required `scenario_id`, configured `message_id`, and monotonic
    `release_sequence` for dashboard-managed scheduled-message injections.
  - Preserve the correlation envelope through every canonical FHSS packet
    boundary and directly on `FHSSAssembledMessagePacket`.
  - The sink emits pipeline terminal results; the controller emits timeout or
    cancellation terminal results. A first-terminal-wins registry publishes
    the authoritative result with terminal state, status, RFC 3339 UTC
    completion timestamp, monotonic completion timestamp, and diagnostics.
  - The controller completes HTTP operations only by exact correlation-tuple
    match.
  - Pulse timing, token ids, queue order, and release order are never accepted
    as terminal-message identity.
  - Correlation metadata is for tracing/validation and must not influence the
    decoder algorithm.

- Decision 23: The source schedule is the authoritative FHSS configuration:
  - Expose one logical `/fhss/scenario` document, backed by the canonical
    source-node schedule for compatibility.
  - Treat active frequencies, preamble detector settings, assembler scenario
    projection, and channelizer transmitter-description fields as generated
    read-only values.
  - Use one versioned `FHSSConfigurationDeriver` from graph construction, CLI,
    HTTP, tests, and export; do not preserve independent patching logic in the
    demo.
  - Any authoritative scenario edit regenerates all dependent node
    configurations and commits authoritative and effective documents in one
    revision only after cross-node validation succeeds.
  - Reject direct writes to generated paths with
    `derived_field_read_only`; never attempt a last-writer-wins merge between
    duplicated fields.
  - Keep channelizer receiver mapping, sample-rate/decimation/filter fields,
    and capture controls independently graph-owned, subject to cross-node
    semantic validation.
  - Require all scheduled messages to share the canonical 16-pulse preamble
    until the graph supports multiple preamble detector configurations.
  - On legacy/full-graph import, regenerate from source authority and reject
    mismatched supplied projections by default. Canonicalization is available
    only as an explicit offline normalization operation with a reviewable diff.

- Decision 24: Dashboard lifetime and shutdown practical default (v1):
  - Dashboard persists after run completion by default for post-run inspection.
  - `--dashboard-exit-on-completion` enables auto-exit for CI/non-interactive
    execution.
  - `--dashboard-no-run` never exits automatically due to completion state.
  - `SIGINT` and `SIGTERM` perform graceful shutdown in defined order, with
    readiness set to not-ready before teardown.
  - If graceful shutdown exceeds its deadline, log timeout and force
    termination.

- Decision 25: RFC 6902 operation allowlist (v1):
  - Allow only `add`, `remove`, `replace`, and `test`.
  - Reject `move` and `copy` with `400 patch_op_not_allowed`.

- Decision 26: Configuration mutation scope (v1):
  - Topology edits are not permitted in v1.
  - Writable paths are limited to authoritative scenario paths
    (`/fhss/scenario/**`) and explicitly approved graph-owned
    `nodes/*/node_config/**` paths.
  - Node/edge add/remove/rewire and non-config topology metadata edits are
    rejected with `400 topology_edit_not_allowed`.

- Decision 27: Patch safety limits (v1):
  - Maximum patch operations per request: 128.
  - Maximum JSON pointer depth: 32 segments.
  - Maximum resulting canonical config document size: 4 MiB.
  - Exceeding limits returns `413 config_limit_exceeded`.

- Decision 28: Generated-field write protection (v1):
  - Direct edits to generated fields are always rejected with
    `409 derived_field_read_only` and an authoritative replacement pointer.

- Decision 29: Absolute sample-origin behavior for per-message generation (v1):
  - Every scheduled message is generated independently, but all timestamps and
    sample indices are expressed in one absolute scenario timeline.
  - `absolute_start_sample` is authoritative; emitted message windows are
    computed as absolute offsets from that origin.
  - No per-message local-origin reset is allowed inside a scenario revision.

- Decision 30: Assembled-packet/message cardinality (v1):
  - One assembled terminal packet corresponds to exactly one scheduled message.
  - Multi-message aggregation or message splitting into multiple terminal
    packets is not supported in v1.

- Decision 31: Reset semantics after terminal outcomes (v1):
  - `reset` clears in-flight/queued step requests, resets scenario cursor and
    source queue state, and returns runtime to a clean pre-step state.
  - Completed/rejected/timed-out terminal records remain queryable in history
    but are not considered active in-flight work after reset.

- Decision 32: Operation cancellation contract (v1):
  - Provide `POST /api/v1/operations/{operationId}/cancel`.
  - Cancellation is valid for `queued` and `running` operations that have not
    reached a terminal result.
  - Cancellation is cooperative; terminal `succeeded`/`failed`/`cancelled`
    states are first-terminal-wins and immutable.

- Decision 33: Retention policies by data class (v1):
  - Operation results retention: 24 hours or 1000 results (whichever first).
  - In-memory command history retention: 100 entries (as in Decision 14).
  - Idempotency-key retention: 24 hours.

- Decision 34: Reused command id with different payload (v1):
  - If a known `command_id` is reused with a different normalized request body,
    reject with `409 idempotency_key_reused_with_different_payload`.
  - If payload matches exactly, return the original operation/result reference.

- Decision 35: `Host` and `Origin` validation (v1):
  - Validate `Host` against explicit localhost/bind allowlist.
  - Require same-origin `Origin` for browser mutation endpoints by default.
  - Reject mismatches with `403 origin_or_host_not_allowed` to mitigate local
    DNS-rebinding/browser attacks.

- Decision 36: Artifact root safety and collision policy (v1):
  - Resolve paths with symlink-safe canonicalization and require containment
    under configured artifact root.
  - Default overwrite policy: no overwrite.
  - Filename collisions produce deterministic suffixing (`.1`, `.2`, ...)
    unless explicit overwrite is requested.

- Decision 37: Dependency/toolchain and offline build policy (v1):
  - Minimum Boost version: 1.83.
  - Minimum Node.js version for frontend build: 20.x LTS.
  - Use lockfiles (`package-lock.json` or equivalent) and deterministic install
    (`npm ci`) in CI.
  - Offline builds must be supported from pinned lockfiles and vendored/cacheable
    dependencies.
  - Generated production frontend assets are build artifacts and are not
    committed by default.

- Decision 38: Canonical projection stability for golden tests (v1):
  - Use one stable canonical projection serializer (deterministic key ordering,
    normalized numeric formatting, stable array ordering by semantic key) across
    platforms.
  - Golden comparisons must use this canonical projection output.

- Decision 39: Log growth, rotation, and retention policy (v1):
  - Default policy remains no auto-delete of user-visible logs.
  - Rotation is allowed by size/time boundary, but rotated logs are retained
    until explicit user deletion.
  - When configured storage ceiling is reached, switch to protective mode:
    reject new log-file creation and emit `log_storage_limit_reached`.

- Decision 40: Runtime execution ownership thread (Step 3+):
  - Runtime execution is owned by a dedicated session-owner thread beginning in
    Step 3.
  - Command handlers communicate with runtime only through serialized command
    queue handoff; no direct HTTP-thread runtime mutation.

## 18. Definition of Done

The dashboard initiative is complete when:

- every committed development step has a runnable server;
- the first shipped UI displays the real effective JSON graph;
- every node can expose a safe parameter/configuration dropdown;
- CLI and HTTP share JSON Pointer-based configuration services;
- graph edits are revisioned, validated, auditable, and transactional;
- the source schedule is the sole authoritative FHSS scenario; dependent
  source, preamble detector, assembler, and channelizer fields are regenerated
  transactionally and cannot be edited independently;
- cross-node semantic validation is required before build, activation, or
  effective-config export;
- generic status/metrics/diagnostics are reusable outside FHSS;
- one FHSS dashboard step means one scheduled message reaches a terminal state;
- terminal completion is proven by an exact
  `(scenario_id, message_id, release_sequence)` match in the terminal-result
  registry; successful/rejected decode completion must originate from the
  sink;
- message stepping works through the browser and HTTP API; a CLI stepping
  command is not required for completion of this initiative;
- the canonical FHSS graph remains singular;
- headless builds and existing tests remain supported;
- the embedded frontend requires no external network resources;
- shutdown, security limits, and artifact provenance are tested.

## 19. Recommended First Implementor Prompt

```text
Implement Step 1 only from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

The result must be a runnable embedded webserver in graphx-dsp-fhss-demo.
Add the optional GRAPHX_BUILD_WEB_DASHBOARD build flag and keep it OFF by
default. Support --dashboard, --dashboard-port 0, and --dashboard-no-run.

Serve an embedded minimal page, /healthz, /readyz, /api/v1/version,
/api/v1/graph, and /api/v1/config. Load the real effective FHSS graph without
executing it. Display graph nodes and edges. Each node must have an expandable
view showing its id, type, configured node_config, and ports/edges. Handle nodes
without node_config. Group the 64 detector nodes visually without changing the
API graph.

Also add CLI read-only configuration commands using RFC 6901 JSON Pointer:
--get-config and --write-effective-config. Do not implement parameter mutation,
metrics, FHSS stepping, or WebSockets yet.

Add ephemeral-port, health, graph API, static asset, clean-shutdown, and
headless-regression tests. Preserve the current canonical FHSS graph and all
existing working-tree changes.
```
