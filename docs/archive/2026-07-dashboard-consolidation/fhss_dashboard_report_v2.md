# GraphX FHSS Dashboard V2 Architecture Review Report

> Archived historical dashboard-planning record. Not current authority.

**Date:** 2026-07-24  
**Reviewer:** Architecture Analysis  
**Status:** Complete Analysis  
**Scope:** FHSS-specific embedded dashboard modernization initiative

---

## Executive Summary

The proposed FHSS Dashboard V2 represents a strategic modernization of the GraphX FHSS visualization layer while maintaining strict compatibility with existing backend architecture, OpenAPI contracts, and security boundaries. The initiative adopts a **compatibility-first** approach using **React Flow + ELK.js + TypeScript** on the frontend while retaining the proven C++ HTTP/WebSocket server architecture.

### Key Strategic Decisions

| Dimension | Decision | Rationale |
|-----------|----------|-----------|
| **Frontend Library** | React Flow + ELK.js | Rich typed node content, explicit port handles, typed FHSS operator workflows |
| **Topology Layout** | ELK.js layered algorithm | Supports explicit ports, orthogonal routing, compound graphs for directed pipeline |
| **Backend Ownership** | Existing C++ server | Proven loopback-only security, transactional lifecycle, real-time metrics |
| **API Versioning** | Preserve `/api/v1/fhss` | No breaking changes; additive optional fields only |
| **Deployment Model** | Single in-place implementation | One dashboard throughout source/build/install; no legacy/v2 selector |
| **Evidence Boundary** | Synthetic-only | No HWIL, conducted RF, OTA, or production RF claims |
| **Runtime Owner** | Application-scoped | Single authority over executor, lifecycle, stop token, cleanup |

### Critical Architectural Constraints

1. **One Dashboard Implementation** — replaces prototype in place, not alongside it
2. **Loopback-Only Security** — trusted local workstation deployment exclusively  
3. **Receiver Truth Isolation** — generator schedules never visible to receiver
4. **Deterministic Synthetic Evidence** — reproducible IQ generation and replay
5. **Versioned Contract Authority** — OpenAPI/JSON Schema govern API evolution
6. **Bounded Instrumentation** — aggregated metrics, not per-message animation

---

## Architecture Overview

### System Layers

```
┌────────────────────────────────────────────────────────┐
│  Browser Frontend (React Flow + TypeScript)            │
│  - Typed node components with port handles             │
│  - Topology visualization + FHSS-specific panels       │
│  - Keyboard/accessibility support (WCAG 2.2 AA)       │
└──────────────────────┬─────────────────────────────────┘
                       │ HTTP + RFC 6455 WebSocket
┌──────────────────────┴─────────────────────────────────┐
│  C++ Application Server (Boost.Beast)                  │
│  - HTTP/1.1 RFC 9110/9112 compliant                   │
│  - WebSocket RFC 6455 streaming                        │
│  - Loopback-only binding (IPv4/IPv6)                  │
│  - Request/response/connection rate limits             │
└──────────────┬──────────────────────┬──────────────────┘
               │                      │
        ┌──────▼──────┐        ┌──────▼──────┐
        │ Configuration│        │ Runtime     │
        │ Authority    │        │ Owner       │
        │              │        │             │
        │ • Graph YAML │        │ • Executor  │
        │ • IQ Gen     │        │ • Graph Mgr │
        │ • Receiver   │        │ • Stop Token│
        │   Config     │        │ • Metrics   │
        └──────────────┘        └──────────────┘
               │
        ┌──────▼──────────────────────────┐
        │ FHSS DSP Receiver Graph         │
        │ 75 nodes | 137 edges | 64-way  │
        │ Channelizer/Detector/Merge/Decode
        └────────────────────────────────┘
```

### Deployment Artifact

```
examples/DSP/dashboard/dist/
├── index.html                      # SPA entry point
├── assets/dashboard-<hash>.js     # Compiled TypeScript/React
├── assets/dashboard-<hash>.css    # Styles with CSP hashes
└── openapi.yaml                    # Machine-readable contract
```

Installed via:
```
share/graphx/fhss-dashboard/
```

No Node.js runtime required on target; node and webpack are build-time only.

---

## Core Architectural Components

### 1. Frontend Architecture (React Flow + ELK.js)

#### React Flow Advantages for GraphX

| Feature | Benefit for FHSS |
|---------|-----------------|
| Explicit source/target handles | Maps naturally to GraphX port abstractions |
| Custom node components | Rich execution state, metrics, diagnostics display |
| Custom SVG edges | Bounded motion, intensity visualization, aggregate activity |
| Selection/pan/zoom | Ergonomic topology navigation and inspection |
| TypeScript integration | Type safety for port mapping and node properties |
| Accessibility primitives | Foundation for WCAG 2.2 keyboard and screen-reader support |

#### ELK.js Layout Algorithm

- **Algorithm:** Layered (org-eclipse-elk-layered)
- **Strengths:** Explicit ports, orthogonal routing, compound graphs, deterministic output
- **FHSS Use:** Top-level directed pipeline (IQ source → Sink)
- **Limitation:** Generic layout insufficient for 64-detector bank visualization

#### Detector Bank Presentation Strategy

The 64-channel detector bank requires specialized handling:

1. **Collapsed View (Default)**
   - Single compound node in pipeline
   - Represents 128 boundary edges as bundle (not authoritative)
   - Bundle carries full edge identity + port mappings

2. **Heatmap View**
   - 64-channel activity matrix (8×8 grid)
   - Shows operational channel metrics in real-time
   - Toggleable with collapsed view

3. **Expanded Hierarchical View**
   - All 64 detector nodes with `parentId` reference
   - Preserves authoritative port numbers (0–63 input, 0 output)
   - Replaces bundle edges with canonical 128 boundary edges

4. **Detailed Inspector**
   - Per-detector state, queue depth, diagnostics
   - Advanced operational analysis

### 2. Backend Architecture (C++ HTTP/WebSocket Server)

#### Boost.Beast HTTP Implementation

**Rationale:** The existing hand-written POSIX parser is incompatible with modern protocol compliance.

**Advantages:**
- RFC 9110/9112 HTTP semantics compliance
- RFC 6455 WebSocket protocol support
- Asynchronous I/O with deadline enforcement
- Strand-based concurrency safety
- Graceful connection draining

**Replaces:** Ad hoc socket/parser loop in Phase 1

#### Loopback-Only Security Model

```cpp
// Bind only configured address
Options::host -> IPv4/IPv6 loopback ONLY (default)
Rejects:       127.0.0.2, 0.0.0.0, ::1 (invalid mixing)
Result:        Unauthenticated service safe for local operator
```

**Deployment Boundary:** Trusted workstation only; no network exposure

#### Request/Response Limits

| Limit | Purpose | Default |
|-------|---------|---------|
| Max request headers | Prevent slowloris | 16 KiB |
| Max request body | JSON nesting DoS | 64 MiB |
| Max response body | Resource exhaustion | 256 MiB |
| Concurrent connections | Operator cardinality | 8 |
| Request timeout | Hung client cleanup | 30 seconds |
| Idle connection timeout | Resource reclaim | 120 seconds |
| Total operation timeout | Long-running tasks | 300 seconds |

### 3. Runtime Ownership Model

**Single runtime owner per application instance:**

```cpp
class RuntimeOwner {
  // Owns executor lifecycle, not shared state
  std::unique_ptr<Executor> executor;
  std::stop_source stop_token;
  GraphGeneration active_generation;
  
  // Transactional operations
  bool build_and_replace(NewConfig);   // atomic
  bool start_real_work();              // joins executor
  bool stop_request();                 // cooperative stop
  void cleanup_on_failure();           // no leaked threads
};
```

**Guarantees:**
- Start → executor thread created
- Stop → thread joined within timeout
- Rebuild → new generation activated only after successful construction
- Cleanup → no orphaned threads or resources

### 4. Configuration Authority

#### FHSS-Specific Policy (Not Generic)

```
FHSSDashboardConfigurationPolicy
├── Parse preamble_pulses → {preamble config, assembler config}
├── Derive active frequencies from preamble (no redundant fields)
├── Validate overlap/timing using architecture samples/pulse-gap rules
├── Generate receiver-minimal config (no truth, no messages)
└── Emit RFC 6902 patches + RFC 6901 pointers (canonical addressing)
```

**Configuration Ownership Boundary:**
- **Generic:** Document/revision/operation service (content-neutral)
- **FHSS:** Policy derivation, truth isolation, validation rules

#### Atomic RFC 6902 Patch Semantics

```http
PATCH /api/v1/fhss/configuration HTTP/1.1
Content-Type: application/json-patch+json
If-Match: "current-etag"

[
  { "op": "replace", "path": "/preamble_pulses", "value": [...] }
]
```

**Guarantees:**
- Stale validators rejected with `412 Precondition Failed`
- Missing precondition rejected with `428 Precondition Required`
- Failed patch leaves document unchanged
- Success includes new ETag

---

## Data Flow and Event Model

### 1. Configuration Flow

```
External Operator Input
    ↓
FHSSDashboardConfigurationPolicy
    ├─ Derivation (preamble → config)
    ├─ Validation (overlap/timing)
    ├─ ETag generation
    └─ Error schema (RFC 9457 problem+json)
    ↓
GraphX GraphManager
    ├─ Graph rebuild
    ├─ Snapshot collection
    └─ Metric binding
    ↓
Binary-IQ Receiver Graph
    (no generator truth visible)
```

### 2. Runtime Metrics and Telemetry

#### Event Model (Proposed Aggregation)

```json
{
  "event_type": "graph.edge_activity",
  "window_start_ns": 47199000000,
  "window_end_ns": 47199200000,
  "publisher_epoch": "2026-07-24T12:34:56Z",
  "sequence": 1847,
  "graph_generation": "gen-42",
  "configuration_revision": "cf-abc123",
  "edges": [
    {
      "edge_id": "channelizer:24->detector_24:0",
      "message_class": "channel_iq",
      "messages": 16,
      "bytes": 6710886,
      "aggregation_interval_ms": 200
    }
  ]
}
```

**Aggregation Strategy:**
- **Interval:** Configurable (e.g., 200ms)
- **Delivery:** Browser translates to motion, intensity, badges
- **No Per-Message Animation:** Prevents graph load from affecting UI

#### Metric Semantics and Availability

| Metric | Source | Units | Interval | Reset |
|--------|--------|-------|----------|-------|
| Message count | Counter | messages | Per-edge window | Generation |
| Byte throughput | Counter | bytes | Per-edge window | Generation |
| Queue depth | Gauge | messages | Snapshot | Real-time |
| Backpressure events | Counter | events | Generation | Generation |
| Processing latency | Histogram | nanoseconds | Per-node | Generation |

**Guarantee:** Every observed field is derived from receiver execution, never from truth/generator schedules.

### 3. WebSocket Streaming (RFC 6455)

#### Connection and Handshake

```
Browser initiates WebSocket upgrade:
GET /api/v1/fhss/events HTTP/1.1
Host: 127.0.0.1:8765
Origin: http://127.0.0.1:8765

Server validates:
- Origin must exactly match configured loopback endpoint
- Subprotocol offer rejected (not needed for JSON events)
- Extension offers declined by omission

Result: RFC 6455 framing over HTTP/1.1 upgrade
```

#### Event Stream Coherence

```
Publisher Ingress (Non-blocking)
    ├─ Monotonic sequence number
    ├─ RFC 3339 timestamp
    ├─ Graph generation/run epoch
    ├─ Configuration revision/ETag
    ├─ Semantic identifiers (job/correlation)
    └─ Bounded queue (4096 events, 8 MiB, 120 seconds)

Coalescence Strategy:
    ├─ Duplicate events evicted first
    ├─ Coalescible metrics coalesced deterministically
    ├─ Terminal/config/job transitions retain FIFO
    └─ Queue overflow requires client resync

Browser Consumption:
    ├─ Resume: contiguous range in same epoch
    ├─ Resync: fetch coherent HTTP snapshot
    ├─ Fallback: bounded HTTP polling + capped exponential backoff
    └─ Validation: ignore duplicates, schema validation
```

#### Reconnection and Resilience

- **Connection Loss:** Browser detects timeout, queues fallback polling
- **Resync Trigger:** Sequence gap, epoch change, or schema change
- **Snapshot Fetch:** Single HTTP GET `/api/v1/fhss/snapshot`
- **Retention Bounds:** 4,096 events, 8 MiB, 120 seconds
- **Overflow Handling:** Critical transitions retained; coalescible events evicted

---

## FHSS-Specific Features

### 1. Detector Bank Abstraction

The Phase 2 receiver contains a 64-channel detector bank that requires special presentation:

```
Channelizer (1 node)
    ├─ Output ports 0–63 (exact)
    └─ 64 unique edges to detector inputs 0

Detector Bank (64 nodes)
    ├─ Input port 0 per detector (fixed)
    ├─ Output port 0 per detector (fixed)
    └─ Deterministic structure (node type, parent, config)

Merge (1 node)
    ├─ Input ports 1–64 (exact)
    └─ 64 unique edges from detector outputs 0
```

**Presentation Adaptation:**
- Group recognition uses structural evidence, not string prefixes
- Bundle edges preserve canonical port numbers (`out-24`, `in-25`)
- Expanded view maintains `parentId` hierarchy
- Heatmap shows real-time channel activity (64-cell matrix)

### 2. Expected vs. Observed Truth Separation

```typescript
interface FHSSExpectedTruth {
  // Generator configuration and scheduled messages
  preamble_schedule: PulseSchedule[];
  message_definitions: MessageDefinition[];
  message_transmission_schedule: TransmissionSchedule[];
  center_frequencies: number[];
  // NEVER passed to receiver execution
}

interface FHSSReceiverObservation {
  // Receiver-derived measurements
  detected_pulses: PulseDetection[];
  decoded_messages: DecodedMessage[];
  channel_observations: ChannelMetrics[];
  decoder_path_metrics: ViterbiMetrics[];
  // Includes confidence inputs, not inferred margins
  // Null availability indicates no observation, never fallback
}

interface FHSSComparisonResult {
  // Receiver vs. expected alignment (metadata only)
  expected_timing_ns: number;
  observed_timing_ns: number;
  timing_error_ns: number;
  expected_channel: number;
  observed_channel: number;
}
```

**Enforcement:**
- Binary IQ receiver export contains zero truth fields
- UI displays expected and observed as independent toggles
- Negative/no-message fixtures produce zero fabricated detections

### 3. Investigation Artifacts and SigMF Integrity

#### Export Structure

```
investigation_bundle_<correlation-id>/
├── sigmf_meta.json                 # SigMF metadata (RFC 6024)
├── sigmf_data.sigmf-data          # Raw IQ samples (optional)
├── truth.json                       # Expected generator state
├── receiver_observations.json       # Receiver-derived data
├── comparison_results.json          # Expected vs. observed
├── receiver_config.json             # Minimal IQ-only config
├── manifest.json                    # Hashes and correlations
└── operation_log.jsonl              # Operator actions
```

#### SigMF Compliance

```json
{
  "global": {
    "core:datatype": "cf32_le",
    "core:sample_rate": 30000000.0,
    "core:center_frequency": 0.0,
    "core:sha512": "deadbeef...",
    "core:version": "1.2.6"
  },
  "captures": [
    {
      "core:sample_start": 0,
      "core:timestamp": "2026-07-24T12:34:56Z"
    }
  ],
  "annotations": [
    {
      "core:sample_start": 325000,
      "core:sample_count": 6500,
      "core:description": "Preamble window"
    }
  ]
}
```

**Guarantees:**
- Hash matches exact byte content
- Datatype consistent with sample interpretation
- No in-JSON binary data
- Metadata-only exports labeled explicitly

---

## Implementation Phases

### Phase Dependency Graph

```
Phase 1 (Safe FHSS shell)
    ↓
Phase 2 (Receiver-minimal config)
    ↓
Phase 3 (Real binary-IQ runtime) ──→ Phase 6 (WebSocket streaming)
    ↓                                ↓
Phase 4 (Receiver observations) ──→ Phase 8 (Qualification)
    ↓
Phase 5 (FHSS jobs/controls) ──→ Phase 7 (SigMF artifacts)
    ↓
Convergence at Phase 8
```

**Phase Blocking Rules:**
- Operator example must run from documentation
- Receiver never receives generator truth
- UI claims match actual data source
- Start/stop state matches executor reality
- No unauthenticated non-loopback listener
- All required tests wired and passing
- No skipped/self-referential tests

### Phase Summary Table

| Phase | Focus | Key Deliverables | Duration |
|-------|-------|-----------------|----------|
| 1 | Safe HTTP shell | Boost.Beast, rate limits, CSP, OpenAPI | Week 1 |
| 2 | Config authority | FHSSDashboardConfigurationPolicy, RFC 6902 | Week 2 |
| 3 | Real runtime | RuntimeOwner, transactional build/start/stop | Week 3 |
| 4 | Receiver data | Truthful observations, spectrum, no placebos | Week 4 |
| 5 | Job control | IQ generation, replay, idempotency, bounds | Week 5 |
| 6 | WebSocket | RFC 6455 streaming, reconnect, resync | Week 6 |
| 7 | Artifacts | SigMF export, validation, replay verification | Week 7 |
| 8 | Qualification | Accessibility, security, soak, installed-tree | Week 8 |

---

## Quality and Compliance Standards

### 1. Protocol and Standards Conformance

| Standard | Compliance Scope |
|----------|-----------------|
| RFC 9110 (HTTP Semantics) | Correct methods, status codes, media types, conditional requests |
| RFC 9112 (HTTP/1.1) | Unambiguous framing, conflict rejection, transfer-length validation |
| RFC 9457 (Problem Details) | Consistent `application/problem+json` errors |
| RFC 6455 (WebSocket) | Handshake, masking, fragmentation, ping/pong, close, origin validation |
| RFC 6901/6902 (JSON Pointer/Patch) | Canonical addressing, atomic operations, canonical paths |
| RFC 3339 (Timestamps) | UTC timestamps with Internet profile |
| OpenAPI 3.1.2 | Machine-readable contract with pinned version |
| JSON Schema Draft 2020-12 | Request/response/event/config schemas |
| WCAG 2.2 AA | Keyboard operation, focus, labels, live status, contrast, reflow |
| NIST SP 800-218 SSDF 1.1 | Security requirements, threat-oriented verification |
| OWASP ASVS 5.0 + API Top 10 | Access control, resource limits, input validation, security headers |
| SigMF 1.2.6 | IQ metadata, datatype/sample-rate description, integrity hashes |

### 2. Testing and Verification Strategy

#### Multi-Agent Workflow

1. **Orchestrator**
   - Audits repository state before each phase
   - Creates file-level acceptance checklists
   - Assigns phase to implementer
   - Assigns result to independent verifier
   - Routes all findings (including medium/low) for disposition
   - Repeats until blocking/high findings resolved

2. **Implementer**
   - Audits before editing
   - Implements only authorized scope
   - Preserves truth/IQ/result separation
   - Adds production-facing tests
   - Builds with C++26 and runs focused tests
   - Reports changed files, results, limitations

3. **Verifier** (Independent)
   - Cannot edit implementation files
   - Verifies each criterion directly (code/test/API/browser/artifact)
   - Runs operator workflow as external user
   - Confirms receiver never receives truth
   - Checks UI labels against actual data sources
   - Reports findings with file/line references

#### Common Quality Gates (Every Phase)

✓ C++26 build (dashboard enabled and disabled)  
✓ Focused unit + API-contract + integration tests  
✓ Browser/operator example  
✓ All prior phases remain green  
✓ Full libgraph/libdsp/DSP regression suites  
✓ JSON Schema/OpenAPI validation  
✓ `git diff --check` (no trailing whitespace)  
✓ No blocking/high-severity findings  

#### Graduated Test Intensity

| Phase | Test Scope |
|-------|-----------|
| 1 | Socket, API contract, ASan/UBSan parser tests |
| 2 | Golden derivation tests, atomic patch tests |
| 3 | Lifecycle state machine, TSAN concurrency tests |
| 4 | Observability golden fixtures, spectrum validation |
| 5 | Idempotency, cancellation, timeout tests |
| 6 | RFC 6455 protocol tests, reconnect/resync, TSAN streaming |
| 7 | Artifact integrity, fuzz targets (HTTP/JSON/Patch/SigMF) |
| 8 | Accessibility automation, soak, installed-tree qualification |

### 3. Security Model

#### Authentication and Authorization

**Local-Only Binding:**
- No authentication required (trusted workstation)
- Loopback-only deployment (IPv4 127.x.x.x or IPv6 ::1)
- Single-origin policy for WebSocket upgrade

**Content Security Policy:**
```
default-src 'none'
script-src 'self' '<hash>' '<hash>'
style-src 'self' '<hash>' '<hash>'
connect-src 'self' ws://127.0.0.1:port
frame-ancestors 'none'
```

#### Input Validation and Limits

```
Request parsing:
  - Max headers: 16 KiB
  - Max body: 64 MiB (tunable per route)
  - Timeout: 30 seconds
  
JSON parsing:
  - Max depth: 64 levels
  - Max members: 10,000
  - Decimal precision: 128 bits
  - No duplicate keys allowed

JSON Pointer/Patch:
  - Max path depth: 20 components
  - Atomic failure semantics
  - Type-safe path validation
```

#### Deployment Hardening (Phase 8)

**Non-Loopback Deployment Requires:**
- TLS with valid certificate
- Basic authentication or OAuth2
- CSRF token validation
- Rate limiting with Retry-After
- Logging and audit trail
- Vulnerability scanning

**Current Phase 8 Scope:** Local-only profile only; security prerequisites documented for future non-loopback use.

---

## Key Architectural Decisions and Trade-offs

### Decision 1: React Flow over Cytoscape.js

**Decision:** Adopt React Flow + ELK.js for modernized FHSS dashboard

**Alternatives:**
- Cytoscape.js: Raw topology, suitable for large-scale analysis
- Custom WebGL: Maximum performance, highest development cost

**Rationale:**
- GraphX nodes need rich HTML content (metrics, diagnostics, controls)
- Explicit port handles map naturally to GraphX port abstractions
- Typed node components enable type-safe FHSS-specific extensions
- Established TypeScript integration and accessibility roadmap

**Trade-off:** Slightly lower performance on massive graphs (75 nodes acceptable), better developer experience and maintainability.

### Decision 2: One Implementation In-Place (Not Legacy Mode)

**Decision:** Replace prototype frontend entirely; serve one implementation at `/`

**Alternatives:**
- Dual-mode with UI selector
- Legacy and v2 routes (`/legacy`, `/v2`)
- Compatibility adapter layer

**Rationale:**
- Reduces testing surface (one implementation)
- Eliminates complexity of feature parity verification
- Source control + qualified release artifacts enable rollback
- Clearer ownership and responsibility

**Trade-off:** No simultaneous A/B testing; must capture baseline before changing source.

### Decision 3: Separate Configuration Policy (FHSS-Specific)

**Decision:** Extract FHSSDashboardConfigurationPolicy from generic document service

**Alternatives:**
- Embed derivation in generic service
- Move all policy into frontend

**Rationale:**
- Clearly separates domain logic from infrastructure
- Testable independently
- Enables reuse for CLI tools
- Prevents truth leakage through generic service

**Trade-off:** Adds one new module; slightly more code organization overhead.

### Decision 4: Boost.Beast for HTTP/WebSocket

**Decision:** Replace hand-written POSIX parser with maintained Boost.Beast library

**Alternatives:**
- Maintain custom parser
- Use alternative (cpp-httplib, pistache)

**Rationale:**
- RFC 9110/9112 compliance
- WebSocket support in same library
- Established, maintained, audited
- Async I/O with deadline/strand safety

**Trade-off:** New dependency (acceptable; Boost is established in ecosystem).

### Decision 5: Aggregated Metrics (Not Per-Message Animation)

**Decision:** Emit aggregated edge activity over time windows; browser translates to motion

**Alternatives:**
- Stream every GraphX message
- Literal animation of each transfer

**Rationale:**
- Graph execution not affected by UI responsiveness
- Bounded memory and CPU in browser
- Deterministic animation from aggregates
- Cleaner separation of concerns

**Trade-off:** Loses sub-millisecond granularity; acceptable for operational dashboard.

### Decision 6: Synthetic-Only Evidence Boundary

**Decision:** No HWIL, conducted RF, OTA, or production RF claims in Phase 1–7

**Alternatives:**
- Plan for RF testing now
- Claim production readiness prematurely

**Rationale:**
- Facilities/equipment not available
- Focus on software correctness first
- Avoid qualification inflation
- Honest labeling for operator

**Trade-off:** Defers RF validation; positions explicitly for future extension.

---

## Deployment Model and Operational Boundaries

### Single-Operator Loopback Deployment

```
Workstation
├── graphx-dsp-fhss-demo executable
├── FHSS dashboard web assets
└── Browser (Firefox/Chrome/Safari)
    │
    └─► http://127.0.0.1:8765/
        └─► WebSocket: ws://127.0.0.1:8765/api/v1/fhss/events
```

**Security Properties:**
- Only operator machine can connect (loopback binding)
- No network exposure
- No authentication required (trusted workstation)
- Single Origin enforced on WebSocket upgrade

### Build Artifacts

```
CMake build directory:
├── bin/graphx-dsp-fhss-demo     # Executable
├── share/graphx/fhss-dashboard/ # Static assets + OpenAPI
└── lib/                          # Dashboard server library

Installed tree (system):
├── /usr/local/bin/graphx-dsp-fhss-demo
└── /usr/local/share/graphx/fhss-dashboard/
```

### Rollback Strategy

1. **Baseline Capture (Before V2 Implementation):**
   - Hash prototype HTML/CSS/JS
   - Record API schema + behavior
   - Store in git history

2. **Rollback Mechanism:**
   - Revert git commit for entire phase
   - Or install prior release artifact
   - No "legacy mode" in executable

3. **Release Artifact Qualification:**
   - Each phase gets qualified build
   - Stored as git tag or release binary
   - Reproducible from source + build instructions

---

## Testing and Verification Framework

### Test Categories and Ownership

#### Unit Tests (Implementer)

- Configuration derivation
- Port address mapping
- Metric aggregation
- JSON Pointer/Patch parsing
- WebSocket framing
- SigMF validation

#### API Contract Tests (Implementer + Verifier)

- Schema validation (OpenAPI/JSON Schema)
- HTTP status codes (correct, not just 200)
- Conditional request semantics (If-Match, 412, 428)
- Error response format (RFC 9457)
- Content-Type and encoding

#### Integration Tests (Verifier)

- HTTP request/response round-trips
- Configuration mutation + verification
- Lifecycle transitions (build → start → stop → rebuild)
- WebSocket reconnect scenarios
- Graceful shutdown

#### Browser/Accessibility Tests (Verifier)

- Keyboard navigation (Tab, Enter, Arrow keys)
- Focus visibility and focus trap
- Screen-reader announcement (ARIA labels)
- Reduced-motion compliance
- Reflow at 320px CSS width

#### Operator Acceptance (Verifier)

- Run documented operator workflow
- Generate synthetic IQ
- Launch dashboard
- Exercise all Phase-specific features
- Export and validate artifacts
- Record operator report

### Sanitizer and Concurrency Coverage

**Mandatory Gates:**
- Phase 1–3: AddressSanitizer (parser, server, request handling)
- Phase 3+: ThreadSanitizer (lifecycle, metrics, event publishing)
- Phase 5+: Memory profiling (job queues, retention)
- Phase 7+: Fuzz targets (HTTP input, JSON Pointer, SigMF import)

**Coverage Tools:**
- Clang AddressSanitizer: Memory safety
- ThreadSanitizer: Data races, deadlocks
- UBSanitizer: Undefined behavior
- libFuzzer: Guided input mutations

### Regression Test Matrix

Every phase must pass:

```
Platforms:
  ├─ x86_64 Linux (CI)
  ├─ x86_64 macOS (CI)
  └─ ARM64 macOS (manual)

Build configs:
  ├─ C++26 Release
  ├─ C++26 Debug (ASan/UBSan)
  └─ C++26 Debug (ThreadSanitizer)

Test scopes:
  ├─ Focused: Phase-specific features
  ├─ Full regression: All libgraph/libdsp/DSP examples
  ├─ Dashboard-disabled build
  └─ Installed-tree smoke tests
```

---

## Recommendations and Observations

### 1. Architecture Maturity Assessment

**Strengths:**
- ✅ Clear compatibility-first policy prevents fragmentation
- ✅ Strict truth isolation rules prevent subtle bugs
- ✅ Multi-agent workflow ensures independent verification
- ✅ Phased delivery with blocking rules prevents hidden debt
- ✅ Comprehensive standards citation provides objective acceptance criteria

**Gaps Identified:**
- ⚠️ No explicit guidance on configuration storage persistence (in-memory vs. durable)
- ⚠️ FHSS policy extraction timing unclear (Phase 2 or later?)
- ⚠️ Detector bank group detection requires implementation evidence before acceptance
- ⚠️ No specified upgrade path for schema changes within Phase 1 constraints

### 2. Phase 1 Priority Actions

For immediate Phase 1 implementation:

1. **Replace HTTP Parser**
   - Integrate Boost.Beast into build system
   - Implement loopback-only binding validation
   - Add request/response limit enforcement

2. **Security Baseline**
   - Add Content Security Policy headers
   - Implement origin validation
   - Add RFC 9457 error responses

3. **Schema Foundation**
   - Pinned OpenAPI 3.1.2 document
   - JSON Schema for Phase 1 responses
   - Validation tests against schema

4. **Operator Integration**
   - Create `examples/DSP/dashboard/operator/` directory
   - Implement `fhss_dashboard_operator.py` with health/readiness checks
   - Document `prepare`, `serve`, `exercise`, `verify`, `report` commands

### 3. Configuration Authority (Phase 2) Considerations

**Derivation Logic Traceability:**
- Create golden test fixtures that validate preamble → frequency mapping
- Store expected outputs independently (not in production code)
- Generate via standalone reference tool

**Minimal Receiver Config:**
- Document exactly which fields are required (preamble_pulses only?)
- Validate that serialized export contains zero truth
- Add AST-based checking if JSON isn't sufficient

### 4. Runtime Ownership (Phase 3) Critical Aspects

**Executor Thread Lifecycle:**
- Document stop-token semantics and timeout values
- Ensure join() doesn't deadlock if thread creation failed
- Add RAII wrapper to prevent orphaned threads

**Generation Binding:**
- Attach generation ID to all metrics/diagnostics at ingress
- Prove in tests that metrics cannot cross generations
- Version snapshot structure if generation-specific fields added

### 5. WebSocket Streaming (Phase 6) Complexity

**Origin Validation:**
- Exact string match (not prefix or subdomain)
- Consider IPv6 zone ID edge cases
- Document allowlist for multi-address deployments

**Reconnect Resilience:**
- Test browser tab sleep/wake scenarios
- Validate resync fetches coherent snapshot (not intermediate state)
- Measure reconnect latency impact on operator UX

### 6. FHSS Job Controller (Phase 5) Semantics

**Idempotency Boundaries:**
- Document replay semantics for duplicate submissions
- Decide: new execution or cached result?
- Handle partial failures in job generation

**Bounded Execution:**
- Processing timeout (30 seconds suggested)
- Queue depth limits (to prevent memory explosion)
- Orphaned temporary artifact cleanup on process death

### 7. Investigation Artifacts (Phase 7) Validation

**Bundle Integrity Chain:**
- Manifest contains hashes of all members
- Each member hash independently verifiable
- Manifest itself is signed or hashed

**Replay Determinism:**
- Export → re-import → replay must produce identical receiver results
- Use canonical order for JSON serialization
- Document any randomness (e.g., receiver thread scheduling) explicitly

### 8. Operator Qualification (Phase 8) Evidence

**Installed-Tree Validation:**
- No access to source build directory
- No build-time dependencies present
- Test from multiple user accounts (permissions)

**Longevity Soak:**
- 24–48 hour continuous operation
- Measure memory/handle/thread growth
- Monitor for event queue saturation

**Release Documentation:**
- Operator guide (how to use dashboard)
- Troubleshooting guide (common issues)
- API compatibility policy (versioning)
- Synthetic-only qualification statement

### 9. Future Generalization (Post-Phase 8)

**Candidates for Extraction (Not Now):**
- HTTP server library abstraction (separate from Boost.Beast specifics)
- Document/revision service (generic CRUD, policy-agnostic)
- Runtime owner pattern (generalizable to other graph types)
- Event publisher (could serve multiple consumers)

**Extraction Criteria:**
- ✅ FHSS dashboard proven and shipped
- ✅ Second use case identified (not hypothetical)
- ✅ Interface and contract stabilized through real use
- ✅ No feature creep to support speculative use cases

**Recommendation:** Do not generalize prematurely. Re-evaluate after FHSS Phase 8 completion with fresh data.

---

## Compliance Checklist

### Standards and Policies

- [ ] RFC 9110 HTTP Semantics compliance verified
- [ ] RFC 6455 WebSocket protocol implementation audited
- [ ] RFC 6902 JSON Patch atomic semantics proven
- [ ] RFC 3339 timestamps used throughout
- [ ] OpenAPI 3.1.2 schema machine-readable
- [ ] JSON Schema Draft 2020-12 validation in place
- [ ] WCAG 2.2 AA keyboard accessibility tested
- [ ] NIST SP 800-218 SSDF 1.1 requirements documented
- [ ] OWASP ASVS 5.0 local threat model assessed
- [ ] SigMF 1.2.6 artifact integrity validated

### Security and Deployment

- [ ] Loopback-only binding enforced and tested
- [ ] Origin validation for WebSocket upgrade
- [ ] Content Security Policy with no unsafe-inline
- [ ] Rate limiting and request size limits documented
- [ ] Authenticated before any non-loopback exposure
- [ ] TLS required for networked deployment
- [ ] No production RF claims in synthetic phases
- [ ] All artifacts labeled "synthetic evidence"

### Quality and Testing

- [ ] C++26 build passes with/without dashboard
- [ ] ASan/UBSan tests for parser and server
- [ ] ThreadSanitizer for concurrent components
- [ ] All unit tests wired and passing (not skipped)
- [ ] API contract tests validate every response
- [ ] Operator acceptance workflow documented
- [ ] Installed-tree smoke tests passing
- [ ] `git diff --check` clean (no trailing whitespace)

### Documentation

- [ ] OpenAPI schema up-to-date
- [ ] JSON Schema files for all request/response bodies
- [ ] Operator guide with screenshots/walkthrough
- [ ] Troubleshooting guide for common issues
- [ ] API compatibility policy documented
- [ ] Build and deployment instructions clear
- [ ] Architecture decision log (this report)
- [ ] Synthetic-only qualification statement explicit

---

## Conclusion

The FHSS Dashboard V2 architecture represents a thoughtful modernization that prioritizes compatibility, clarity, and correctness over premature generalization. The phased implementation approach with multi-agent verification ensures that each increment is complete, testable, and operationally valid before moving forward.

### Key Success Factors

1. **Rigid Phase Blocking:** No phase proceeds if operator workflow cannot run from documentation
2. **Truth Isolation Enforcement:** Every test and export validates that receiver never receives generator truth
3. **Independent Verification:** Verifier cannot edit implementation; findings routed back for disposition
4. **Synthetic Evidence Honesty:** All claims and UI labels explicitly mark evidence as synthetic-only
5. **Standards-Based Acceptance:** Protocol compliance, schema validation, and security gates provide objective pass/fail criteria

### Next Steps

1. Approve Phase 1 baseline (HTTP replacement, loopback binding, CSP, OpenAPI)
2. Assign orchestrator, implementer, verifier roles
3. Capture prototype behavior baseline before code changes
4. Execute Phase 1 with focused tests + operator workflow
5. Independent verification of Phase 1 acceptance criteria
6. Gate-pass authorization before Phase 2 proceeds

The architecture is ready for implementation. Success depends on discipline in following the phase-blocking rules and maintaining the synthetic-only evidence boundary throughout all 8 phases.

---

## Appendix A: Glossary

| Term | Definition |
|------|-----------|
| **Bundle Edge** | Presentation-only edge representing multiple authoritative edges in collapsed view |
| **Coalescence** | Merging duplicate/similar events deterministically for efficient event transport |
| **ELK.js** | Eclipse Layout Kernel; used for deterministic layered graph layout |
| **Generation ID** | Unique identifier for a graph/configuration/runtime state combination |
| **GraphX Port** | Named input or output connection point on a node (numeric ID + metadata) |
| **Idempotency Key** | Client-provided identifier ensuring duplicate requests produce same result |
| **Loopback Address** | Local-only network interface (127.x.x.x for IPv4, ::1 for IPv6) |
| **Minimal Config** | Receiver configuration containing only receiver-necessary fields (no truth) |
| **Originality** | Proof that generator schedules never appear in receiver execution path |
| **RFC 6902 Patch** | Standardized JSON mutation operations (add, remove, replace, copy, move, test) |
| **SigMF** | Signal Metadata Format; standardized IQ metadata with integrity hashes |
| **Synthetic Evidence** | Derived from simulation/IQ generation; never from hardware/RF |
| **Verifier** | Independent agent who cannot edit implementation but must verify acceptance criteria |

---

## Appendix B: Phase 1 Acceptance Checklist

### HTTP Server Implementation

- [ ] Boost.Beast integrated and compiling
- [ ] Loopback-only binding honored (IPv4/IPv6, reject 0.0.0.0)
- [ ] Request/response limits enforced and documented
- [ ] Graceful shutdown (signal handling, connection draining)
- [ ] Deterministic port release (no TIME_WAIT issues)

### Security

- [ ] CSP headers present, no unsafe-inline
- [ ] RFC 9457 problem+json errors returned
- [ ] Allow header on 405 Method Not Allowed
- [ ] Path traversal rejected (canonical containment)
- [ ] Symlink following tested and rejected

### API Contract

- [ ] OpenAPI 3.1.2 document (machine-readable)
- [ ] JSON Schema for all responses
- [ ] Health and readiness endpoints working
- [ ] Version discovery endpoint present
- [ ] FHSS-only routes (no generic endpoints leaking)

### Operator Integration

- [ ] `examples/DSP/dashboard/operator/fhss_dashboard_operator.py` created
- [ ] `prepare` command works
- [ ] `serve` command launches and prints URL
- [ ] `exercise` validates Phase 1 features
- [ ] `verify` validates schema and rejects traversal
- [ ] `report` writes machine-readable output
- [ ] `cleanup` removes temp data

### Testing

- [ ] Unit tests for parser, rate limits, path containment
- [ ] ASan/UBSan parser tests passing
- [ ] API contract tests validating responses
- [ ] Malformed/oversized/slow request tests
- [ ] Installed-tree launch works without source assets
- [ ] Dashboard-disabled build excludes all FHSS deliverables

### Verification

- [ ] Independent verifier runs operator workflow as external user
- [ ] Packet inspection proves loopback-only binding
- [ ] All blocking criteria passed (no medium/low workarounds)
- [ ] No unresolved findings

---

**Report Completed:** 2026-07-24  
**Classification:** Architecture Analysis  
**Status:** Ready for Phase 1 Implementation Review
