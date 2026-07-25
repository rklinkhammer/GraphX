# FHSS Dashboard V2 — Phase 1 Multi-Agent Orchestration

**Phase:** 1 — Secure FHSS Dashboard Walking Skeleton  
**Date:** 2026-07-24  
**Status:** Ready for Orchestrator Preparation  
**Duration Estimate:** 1 week  
**Workflow:** Orchestrator → Implementer → Verifier → Approval

---

## Phase 1 Objective

Deliver a trustworthy, externally testable FHSS dashboard baseline with:

- **Safe HTTP infrastructure** (RFC 9110/9112 compliant via Boost.Beast)
- **Loopback-only security** (IPv4/IPv6 local binding only)
- **Request/response bounds** (prevent DoS, resource exhaustion)
- **Production-facing tests** (ASan/UBSan parser and server tests)
- **Machine-readable contracts** (OpenAPI 3.1.2 + JSON Schema)
- **Operator acceptance workflow** (documented, runnable Python tool)
- **Installed-tree qualification** (dashboard assets installed separately from build artifacts)

**Out of Scope (Later Phases):**
- Configuration derivation policy (Phase 2)
- Real runtime execution (Phase 3)
- Receiver observations/metrics (Phase 4)
- WebSocket streaming (Phase 6)

---

## Multi-Agent Workflow

### Role Definitions

| Role | Responsibility | Scope | Authority |
|------|-----------------|-------|-----------|
| **Orchestrator** | Prepare, audit, assign, coordinate, route findings, gate approval | Pre-implementation, post-verification, decision-making | Phase gate authority |
| **Implementer** | Execute assigned work, add tests, create operator bundle, report results | Implementation only; immediate enabling changes permitted | Code changes within scope |
| **Verifier** (Independent) | Verify independently; cannot edit implementation | Verification and acceptance testing only | Quality gate authority |

---

## Orchestrator Preparation (Pre-Implementation)

**Duration:** 30 minutes  
**Responsible:** Orchestration Agent  
**Output:** Implementation brief + acceptance checklist

### 1. Audit Current Repository State

- [ ] Verify GraphX main branch is clean (no uncommitted changes)
- [ ] Confirm current git commit hash and branch name
- [ ] List all modified files if any
- [ ] Verify Phase 1 does not depend on uncommitted work from other phases
- [ ] Confirm no active dashboard code already in progress

**Deliverable:** Repository snapshot (commit hash, branch, file inventory)

### 2. Preserve Baseline for Rollback

- [ ] Hash entire `examples/DSP/dashboard/` directory (future rollback reference)
- [ ] Document current `EmbeddedDashboardServer` implementation (copy to archive)
- [ ] Save current OpenAPI schema (if any) for baseline comparison
- [ ] Store operator baseline behavior (API responses, error messages)

**Deliverable:** Baseline hash file (for future rollback validation)

### 3. Analyze Scope and Dependencies

- [ ] Verify Boost.Beast is available or can be added to build system
- [ ] Check if any existing code conflicts with proposed HTTP server
- [ ] Identify all current dashboard-related files
- [ ] Map existing test structure for new dashboard tests
- [ ] Confirm no CMakeLists circular dependencies

**Deliverable:** Scope map (affected files, dependencies, constraints)

### 4. Create File-Level Acceptance Checklist

Create `plan/Phase1_Acceptance_Checklist.md` with:
- [ ] Boost.Beast integration (list files to modify/create)
- [ ] Loopback binding validation (list test cases)
- [ ] Request/response limits (list enforced limits)
- [ ] Path containment (list test vectors)
- [ ] Security headers (list headers, test validation)
- [ ] OpenAPI schema (schema file path, test coverage)
- [ ] JSON Schema files (list all schemas needed)
- [ ] Operator tool commands (list all commands)
- [ ] Test suites (list unit/integration tests)
- [ ] Installed-tree workflow (step-by-step validation)

**Deliverable:** Detailed file-level checklist

### 5. Create Standards Requirements Map

Document which Phase 1 acceptance criteria map to which standards:

```
Phase 1 Gate → Standards Coverage

HTTP Protocol:
  - Request/response limits → RFC 9110 sect. 5 (Message Control)
  - Error format → RFC 9457 (Problem Details)
  - Status codes → RFC 9110 sect. 15 (Status Codes)
  
Security:
  - CSP headers → OWASP ASVS 5.0 sect. 3.5 (Output Encoding)
  - Path containment → OWASP ASVS 5.0 sect. 5.3 (File Upload)
  - Loopback binding → NIST SP 800-218 sect. 3.1 (Source Control)

Testing:
  - ASan/UBSan → NIST SP 800-218 sect. 3.3 (Build Security)
  - Operator workflow → OWASP ASVS 5.0 sect. 1.1 (Secure Arch)
```

**Deliverable:** Standards compliance map

### 6. Assign Implementer and Verifier

- [ ] Select implementer agent (fresh, not involved in analysis)
- [ ] Select independent verifier agent (cannot be same agent as implementer)
- [ ] Confirm both agents have access to workspace
- [ ] Brief both on multi-agent workflow rules

**Deliverable:** Agent assignments + workflow acknowledgment

### 7. Record Pre-Implementation State

Create `plan/Phase1_PreImplementation_Snapshot.json`:
```json
{
  "phase": 1,
  "orchestrator_date": "2026-07-24T...",
  "repository_state": {
    "commit": "abc123...",
    "branch": "main",
    "status": "clean"
  },
  "baseline_hashes": {
    "dashboard_dir": "sha256:...",
    "http_server_files": "sha256:..."
  },
  "scope_checklist": {
    "file_level_items": 47,
    "standards_mapped": 12,
    "acceptance_criteria": 24
  },
  "agent_assignments": {
    "implementer": "...",
    "verifier": "..."
  }
}
```

**Deliverable:** Pre-implementation snapshot JSON

---

## Implementer Assignment (Implementation Phase)

**Duration:** 1 week  
**Responsible:** Implementer Agent  
**Constraints:** Must not edit verifier workflows; must provide evidence for all changes

### Implementer Responsibilities

1. **Audit Before Editing**
   - Review orchestrator's checklist and baseline
   - Understand phase scope and boundaries
   - Identify reusable existing code

2. **Implement Phase 1 Scope Only**
   - No Phase 2 configuration logic
   - No Phase 3 runtime owner
   - No Phase 4+ metrics collection
   - Changes must enable Phase 1 acceptance only

3. **Preserve Truth Isolation**
   - Verify receiver graph never receives generator truth
   - No FHSS message definitions in receiver config
   - No hidden truth side channels in server responses

4. **Add Production-Facing Tests**
   - Unit tests for HTTP parser, limits, path containment
   - Integration tests for API contract, installed-tree
   - ASan/UBSan tests (not just test-only sanitizer code)
   - Operator acceptance workflow

5. **Create Operator Bundle**
   - Python tool with all 6 commands
   - README with documented examples
   - Expected output baselines

6. **Document Changes**
   - List all modified/created files
   - Record known limitations
   - List any standards intentionally deferred

### Implementer Deliverables

```
deliverables/
├── IMPLEMENTATION_REPORT.md
│   ├── Files changed (with line counts)
│   ├── Tests added (with coverage)
│   ├── Build commands and results
│   ├── Operator workflow test results
│   ├── Known limitations
│   └── Standards compliance status
├── changed_files_list.txt (for verifier audit)
├── test_results.json
├── operator_workflow_evidence/
│   ├── screenshots
│   └── API responses (sample)
└── build_log.txt (clean build, no warnings)
```

### Implementer Acceptance Gates (Pre-Verification)

Implementer must verify locally before submitting:

- [ ] C++26 build succeeds (dashboard ON and OFF)
- [ ] No compiler warnings (treat as errors)
- [ ] All unit tests passing
- [ ] ASan/UBSan tests passing (no issues)
- [ ] Operator workflow runs end-to-end
- [ ] Installed-tree launch succeeds
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] IMPLEMENTATION_REPORT.md complete

**Stop here. Do not proceed to verifier if any gate fails.**



---

## Implementer Technical Requirements

**This section is the specification given to the Implementer Agent after orchestrator preparation.**

### 1. HTTP Server Architecture (Replace Hand-Written Parser)

**Current State:** Ad hoc POSIX socket loop with hand-written HTTP parser  
**Target:** Boost.Beast (RFC 9110/9112 compliant, asynchronous)

#### Tasks

1. **Build Integration**
   - Add Boost.Beast to CMakeLists.txt (minimum version specified)
   - Verify compilation with C++26 and target compiler (AppleClang/GCC/Clang)
   - Add Boost.Asio strand-based concurrency safety
   - Define FHSS_DASHBOARD build option (default ON; allow disable)

2. **Loopback-Only Binding**
   ```cpp
   // MUST validate Options::host
   - If host is "localhost": bind to 127.0.0.1 (IPv4) and ::1 (IPv6)
   - If host is IPv4 address: MUST be 127.x.x.x
   - If host is IPv6 address: MUST be ::1
   - Reject 0.0.0.0, ::/0, and any public addresses
   - Return error message with binding policy
   ```

3. **Request/Response Limits**
   - Max request headers: 16 KiB
   - Max request body (default): 64 MiB (per-route configurable)
   - Max response body: 256 MiB
   - Request read timeout: 30 seconds
   - Request write timeout: 30 seconds
   - Idle connection timeout: 120 seconds
   - Total operation timeout: 300 seconds
   - Max concurrent connections: 8 (configurable)

4. **HTTP Protocol Compliance**
   - Reject ambiguous or conflicting transfer-length headers
   - Implement conditional request support (If-Match, If-Modified-Since)
   - Return correct status codes (200, 204, 400, 405, 412, 428, 429, etc.)
   - Include Allow header on 405 Method Not Allowed
   - Support HTTP/1.1 Keep-Alive (with limits)
   - Graceful connection draining on shutdown

5. **Graceful Shutdown**
   - Signal handler for SIGTERM/SIGINT
   - Mark server as "not ready" immediately
   - Drain existing connections (graceful close)
   - Join executor threads within timeout
   - Release listening socket deterministically

### 1. HTTP Server Architecture (Replace Hand-Written Parser)

**Current State:** Ad hoc POSIX socket loop with hand-written HTTP parser  
**Target:** Boost.Beast (RFC 9110/9112 compliant, asynchronous)

#### Tasks

1. **Build Integration**
   - Add Boost.Beast to CMakeLists.txt (minimum version specified)
   - Verify compilation with C++26 and target compiler (AppleClang/GCC/Clang)
   - Add Boost.Asio strand-based concurrency safety
   - Define FHSS_DASHBOARD build option (default ON; allow disable)

2. **Loopback-Only Binding**
   ```cpp
   // MUST validate Options::host
   - If host is "localhost": bind to 127.0.0.1 (IPv4) and ::1 (IPv6)
   - If host is IPv4 address: MUST be 127.x.x.x
   - If host is IPv6 address: MUST be ::1
   - Reject 0.0.0.0, ::/0, and any public addresses
   - Return error message with binding policy
   ```

3. **Request/Response Limits**
   - Max request headers: 16 KiB
   - Max request body (default): 64 MiB (per-route configurable)
   - Max response body: 256 MiB
   - Request read timeout: 30 seconds
   - Request write timeout: 30 seconds
   - Idle connection timeout: 120 seconds
   - Total operation timeout: 300 seconds
   - Max concurrent connections: 8 (configurable)

4. **HTTP Protocol Compliance**
   - Reject ambiguous or conflicting transfer-length headers
   - Implement conditional request support (If-Match, If-Modified-Since)
   - Return correct status codes (200, 204, 400, 405, 412, 428, 429, etc.)
   - Include Allow header on 405 Method Not Allowed
   - Support HTTP/1.1 Keep-Alive (with limits)
   - Graceful connection draining on shutdown

5. **Graceful Shutdown**
   - Signal handler for SIGTERM/SIGINT
   - Mark server as "not ready" immediately
   - Drain existing connections (graceful close)
   - Join executor threads within timeout
   - Release listening socket deterministically

### 2. Static Asset Containment

**Current Issue:** String-prefix-based path ancestry (vulnerable to traversal)  
**Target:** Component-aware canonical containment

#### Tasks

1. **Path Validation**
   ```cpp
   // Safe containment check
   - Resolve request path to canonical absolute path
   - Resolve asset root to canonical absolute path
   - Verify requested path is descendant of asset root
   - Reject symlinks (use realpath() or equivalent)
   - Reject /../ sequences
   - Reject .. path components
   - Test: /assets/../../../etc/passwd → REJECT
   - Test: /assets/../../config.yaml → REJECT
   ```

2. **File System Safety**
   - Verify requested resource is regular file (not directory, socket, FIFO)
   - Use platform canonical-path functions (realpath, std::filesystem::canonical)
   - Document any edge cases (case-insensitive filesystems, symlinks)
   - Add negative tests for traversal and sibling-prefix escapes

3. **Asset Inventory**
   - List all served assets (index.html, CSS, JS, favicon)
   - Verify no extra assets shipped
   - Verify no source files (*.ts, *.tsx, .env) in built assets
   - Check installed-tree asset list

### 3. Security Headers and Content Security Policy

#### Tasks

1. **HTTP Security Headers (All Responses)**
   ```http
   Content-Security-Policy: default-src 'none'; script-src 'self' '<hash1>' '<hash2>'; style-src 'self' '<hash3>'; connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'
   X-Content-Type-Options: nosniff
   X-Frame-Options: DENY
   X-XSS-Protection: 1; mode=block
   Referrer-Policy: no-referrer
   Permissions-Policy: camera=(), microphone=(), geolocation=()
   ```

2. **CSP Nonce/Hash Strategy**
   - Generate nonce per response (non-static)
   - OR build-time hash of inline script/style (if any)
   - Document chosen strategy
   - Test: inline script in index.html must be hashed or nonce'd
   - Verify no unsafe-eval, unsafe-inline without hashes/nonces

3. **Error Handling**
   - Never expose source paths in error messages
   - Never leak file system structure
   - Return generic "Not Found" for missing assets

### 4. OpenAPI and JSON Schema

#### Tasks

1. **Create OpenAPI 3.1.2 Document**
   - File: `docs/api/fhss-dashboard-v1.openapi.yaml`
   - Define all Phase 1 routes:
     - `GET /`
     - `GET /healthz`
     - `GET /readyz`
     - `GET /version`
     - `GET /api/v1/fhss/snapshot` (placeholder, returns 200)
     - `GET /api/v1/fhss/graph` (placeholder, returns empty graph)
   - Document request/response schemas for each route
   - Include error responses (400, 404, 405, 429, 500)

2. **JSON Schema Files**
   - Schema files in `docs/api/schemas/`
   - `healthz-response.json`
   - `readyz-response.json`
   - `version-response.json`
   - `snapshot-response.json` (Phase 1: minimal)
   - `graph-response.json` (Phase 1: minimal)
   - `problem-response.json` (RFC 9457 error format)

3. **Response Validation**
   - Add test that validates all responses against schema
   - Use external JSON Schema validator (e.g., nlohmann/json-schema)
   - Fail build if response does not validate

### 5. Error Response Format (RFC 9457)

#### Tasks

1. **Problem Details Structure**
   ```json
   {
     "type": "about:blank",
     "status": 405,
     "title": "Method Not Allowed",
     "detail": "POST is not allowed on /api/v1/fhss/snapshot",
     "instance": "/api/v1/fhss/snapshot"
   }
   ```

2. **Implementation**
   - All error responses use `application/problem+json` media type
   - HTTP status code matches `status` field
   - Descriptive `detail` field (no source paths)
   - Include `instance` (request URI) for debugging

3. **Testing**
   - Test 400 Bad Request (malformed JSON)
   - Test 405 Method Not Allowed
   - Test 429 Too Many Requests (if rate limiting active)
   - Test 500 Internal Server Error (generic, no details leaked)

### 6. Dashboard Assets Build and Installation

#### Tasks

1. **Web Asset Build** (using existing toolchain)
   - Compile TypeScript/React to JavaScript
   - Generate CSS with hashes for CSP
   - Minify and version outputs (e.g., `dashboard-<hash>.js`)
   - Output to `examples/DSP/dashboard/dist/`

2. **CMake Integration**
   - `FHSS_DASHBOARD` option (default ON)
   - When ON: build dashboard, include in install
   - When OFF: skip dashboard build entirely
   - Verify OFF build contains no `/dashboard/` code

3. **Installation**
   - Install assets to `${CMAKE_INSTALL_PREFIX}/share/graphx/fhss-dashboard/`
   - Install OpenAPI schema to same location
   - Create symlink from `/` to `fhss-dashboard/index.html` (or use hard path)

4. **Verification**
   - Installed-tree test: launch from fresh install, no source directory
   - Verify no build directory reference in installed executable
   - Verify dashboard-disabled build excludes all assets

### 7. Application Routes and Behavior

#### Tasks

1. **GET / (Serve Dashboard)**
   - Returns `index.html` with CSP headers
   - Content-Type: `text/html; charset=utf-8`
   - ETag based on dashboard version/hash

2. **GET /healthz (Liveness)**
   - Returns 200 OK
   - Body: `{"status": "alive"}`
   - Always succeeds (server process running)

3. **GET /readyz (Readiness)**
   - Returns 200 OK when server ready to accept work
   - Returns 503 Service Unavailable when not ready
   - Body: `{"ready": true/false, "reason": "..."}`

4. **GET /version (Metadata)**
   - Returns GraphX version, build date, commit hash
   - Include dashboard version
   - Body: `{"graphx_version": "...", "commit": "...", "dashboard": "..."}`

5. **Placeholder Routes (Will Be Implemented in Later Phases)**
   - `GET /api/v1/fhss/snapshot` → returns empty or minimal placeholder
   - `GET /api/v1/fhss/graph` → returns minimal graph structure
   - Document these as "not available in Phase 1"
   - Return `501 Not Implemented` or placeholder success

6. **Not Found (404)**
   - All other paths return 404
   - Return RFC 9457 error response
   - Do not serve static assets (only index.html served at `/`)

### 8. Operator Integration Tool

**Location:** `examples/DSP/dashboard/operator/`

#### Tasks

1. **Create Directory Structure**
   ```
   examples/DSP/dashboard/operator/
   ├── fhss_dashboard_operator.py      # Main orchestrator
   ├── README.md                        # Usage and command reference
   ├── requirements.txt                 # Python dependencies (requests, pytest)
   ├── scenarios/                       # Test inputs (if any for Phase 1)
   └── expected/                        # Expected outputs/baselines
   ```

2. **Implement fhss_dashboard_operator.py Commands**

   **`prepare`** — Validate environment
   - Check graphx-dsp-fhss-demo binary exists
   - Check dashboard assets installed
   - Return success or error

   **`serve`** — Launch dashboard server
   - Start executable on ephemeral loopback port
   - Poll /healthz until ready
   - Print URL: `http://127.0.0.1:NNNN`
   - Return port or PID for subsequent commands

   **`exercise`** — Run Phase 1 workflow
   - Fetch `/healthz` → validate response
   - Fetch `/readyz` → validate response
   - Fetch `/version` → validate response
   - Attempt path traversal: `/assets/../../../etc/passwd` → 404
   - Attempt oversized request → 413 Payload Too Large (or timeout)
   - Attempt slow/incomplete request → timeout
   - Attempt malformed JSON → 400 Bad Request
   - Record test results

   **`verify`** — Validate contract and artifacts
   - Validate `/api/v1/fhss/snapshot` response against JSON Schema
   - Validate OpenAPI schema file presence
   - Check asset file count and names
   - Verify no source files in assets

   **`report`** — Generate machine-readable output
   - Output JSON with:
     - `phase_id`: "1"
     - `source_revision`: git commit hash
     - `build_profile`: "Debug" or "Release"
     - `compiler`: "AppleClang 15.0.0" or similar
     - `platform`: "macOS x86_64" or similar
     - `commands_run`: list of operator commands executed
     - `test_results`: pass/fail for each test
     - `api_version`: "/api/v1/fhss"
     - `asset_hashes`: SHA-256 of key files
     - `timestamp`: ISO 8601 timestamp

   **`cleanup`** — Stop server and remove temp data
   - Kill executable process
   - Remove temporary directories created by tool
   - Do NOT remove installed assets or build artifacts

3. **Python Requirements**
   - `requests` library for HTTP calls
   - Optional: `jsonschema` for schema validation
   - Python 3.9+

4. **README Documentation**
   - Command reference with examples
   - Expected output for each command
   - Troubleshooting (can't bind port, permission denied, etc.)
   - How to interpret report JSON

### 9. Testing Strategy

#### Unit Tests (C++)

1. **HTTP Parser/Server Tests**
   ```cpp
   TEST(HTTPServer, LoopbackBindingIPv4Accepted)
   TEST(HTTPServer, LoopbackBindingIPv6Accepted)
   TEST(HTTPServer, PublicBindingRejected)
   TEST(HTTPServer, AnyAddressBindingRejected)
   ```

2. **Request Limit Tests**
   ```cpp
   TEST(HTTPServer, OversizedHeadersRejected)
   TEST(HTTPServer, OversizedBodyRejected)
   TEST(HTTPServer, SlowClientTimeout)
   TEST(HTTPServer, IncompleteRequestTimeout)
   ```

3. **Path Traversal Tests**
   ```cpp
   TEST(AssetServer, TraversalParentDirRejected)
   TEST(AssetServer, TraversalSymlinkRejected)
   TEST(AssetServer, TraversalAbsolutePathRejected)
   TEST(AssetServer, ValidAssetServed)
   ```

4. **Response Format Tests**
   ```cpp
   TEST(HTTPServer, MethodNotAllowedHasAllow)
   TEST(HTTPServer, ErrorResponseIsProblemJSON)
   TEST(HTTPServer, HealthzResponseValid)
   TEST(HTTPServer, VersionResponseValid)
   ```

#### Integration Tests

1. **API Contract Tests**
   - Run operator `exercise` command
   - Validate response schemas against OpenAPI/JSON Schema
   - Verify HTTP status codes match contract

2. **Installed-Tree Tests**
   - Build and install to temporary directory
   - Launch from fresh install (no source directory in PATH)
   - Execute `serve` + `exercise` from installed location
   - Verify results

#### Sanitizer Coverage

1. **AddressSanitizer (ASan)**
   - Compile with `-fsanitize=address`
   - Run server tests with ASan enabled
   - Verify no memory leaks, use-after-free, buffer overflows

2. **UndefinedBehaviorSanitizer (UBSan)**
   - Compile with `-fsanitize=undefined`
   - Run parser tests with UBSan enabled
   - Verify no signed overflow, type mismatch, etc.

3. **Integration**
   - CI build with ASan/UBSan enabled
   - Fail build if any sanitizer issues found

---

## Verifier Assignment (Verification Phase)

**Duration:** 2–3 days  
**Responsible:** Independent Verifier Agent  
**Constraints:** Cannot edit implementation files; must verify independently with direct evidence

### Verifier Responsibilities

1. **Independent Verification (No Implementation Changes)**
   - Verifier may NOT edit source code, tests, or operator tool
   - Verifier may NOT approve partial implementations with workarounds
   - Verifier must verify each criterion with direct code/test/API evidence

2. **Verify Each Acceptance Criterion**
   - Check HTTP server implementation directly
   - Run tests and verify results (not trust console output)
   - Inspect actual API responses
   - Validate file artifacts exist and contain correct content

3. **Run Operator Workflow as External User**
   - Treat operator tool as black box (no insider knowledge)
   - Execute from documented README instructions
   - Do not skip steps or use shortcuts
   - Record actual behavior vs. documented expectations

4. **Confirm Truth Isolation**
   - Audit code paths to verify receiver never receives generator truth
   - Check binary-IQ receiver configuration exports
   - Verify no hidden truth side channels in server responses

5. **Assess Security and Deployment**
   - Verify loopback-only binding with packet inspection or socket introspection
   - Check CSP headers on actual responses
   - Test path traversal vectors directly
   - Validate installed-tree launch works without source directory

6. **Report Findings**
   - Document all findings with file and line references
   - Classify severity: blocking, high, medium, low
   - Provide explicit PASS/FAIL for each acceptance criterion
   - Return FAIL if test is skipped, unwired, or self-referential

### Verifier Deliverables

```
verification/
├── VERIFICATION_REPORT.md
│   ├── Criterion verification (pass/fail with evidence)
│   ├── Security assessment
│   ├── Test coverage analysis
│   ├── Operator workflow results
│   ├── Blocking findings (if any)
│   ├── High/medium/low findings
│   └── Overall PASS/FAIL recommendation
├── evidence/
│   ├── api_responses.json (sample calls and actual responses)
│   ├── test_output.txt (full test run)
│   ├── packet_trace.pcap (loopback binding proof)
│   ├── operator_workflow_log.txt
│   └── screenshots/
└── independent_test_run.json
```

### Verifier Acceptance Gates

Verifier must validate each criterion independently:

#### HTTP Server Implementation

- [ ] Boost.Beast integration verified by code inspection
- [ ] Loopback-only binding tested with packet inspection (proof: no non-loopback listener)
- [ ] Request/response limits enforced (tested with oversized/slow requests)
- [ ] Graceful shutdown tested (signal handling, connection draining)
- [ ] Deterministic port release verified (no TIME_WAIT blockage)

#### Security

- [ ] CSP headers present on all responses (check actual HTTP response headers)
- [ ] No unsafe-inline or unsafe-eval without hashes/nonces
- [ ] Path traversal vectors tested and rejected (test `/assets/../../../etc/passwd`)
- [ ] RFC 9457 error responses consistently formatted
- [ ] No source paths or file system structure exposed in errors

#### API Contract

- [ ] OpenAPI schema file exists and is valid YAML
- [ ] JSON Schema files exist for all routes
- [ ] Response validation tests pass (actual responses match schema)
- [ ] Health/readiness/version endpoints working as documented
- [ ] FHSS-only routes (verify no generic endpoints leaking)

#### Operator Integration

- [ ] README documentation is clear and complete
- [ ] All commands work as documented: `prepare`, `serve`, `exercise`, `verify`, `report`, `cleanup`
- [ ] Operator tool runs from scratch as external user (no inside knowledge)
- [ ] Report JSON is valid, complete, and machine-readable
- [ ] Operator can launch dashboard and exercise Phase 1 features

#### Testing and Quality

- [ ] C++26 build passes with dashboard enabled
- [ ] C++26 build passes with dashboard disabled
- [ ] ASan/UBSan tests passing (no memory/undefined behavior issues)
- [ ] All unit tests wired (not skipped)
- [ ] All integration tests passing
- [ ] Installed-tree launch succeeds (no source directory needed)
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] No compiler warnings

#### Regression and Blocking

- [ ] All prior phases remain passing (if any completed)
- [ ] Full libgraph/libdsp/DSP regression suites passing
- [ ] No blocking findings
- [ ] No high-severity findings unresolved

### Verifier Stop Rules

Verifier returns **FAIL** if:

- [ ] Operator workflow cannot run from documented README
- [ ] Receiver execution receives generator truth (confirmed by code audit)
- [ ] UI claims match actual data source
- [ ] Start/stop state disagrees with actual executor state
- [ ] Server exposes unauthenticated non-loopback listener
- [ ] New schema is undocumented or fails validation
- [ ] Required test is excluded, skipped, or self-referential
- [ ] Focused/full regressions fail
- [ ] `git diff --check` fails
- [ ] Any blocking or unresolved high-severity finding remains

---

## Phase 1 Acceptance Criteria (Verification Checklist)

### HTTP Server Implementation

- [ ] Boost.Beast integrated and compiling (C++26, all target platforms)
- [ ] Loopback-only binding enforced and tested (verified by packet inspection)
- [ ] Request/response limits enforced (documented limits met in testing)
- [ ] Graceful shutdown working (signal handling, connection draining tested)
- [ ] Deterministic port release (no TIME_WAIT blockage verified)

### Security

- [ ] CSP headers present on all responses (checked against actual HTTP responses)
- [ ] No unsafe-inline or unsafe-eval without hashes/nonces
- [ ] Path traversal tests pass (symlinks, `.., `/` rejected with direct testing)
- [ ] RFC 9457 error responses consistently formatted
- [ ] No source paths or file system structure exposed in errors

### API Contract

- [ ] OpenAPI 3.1.2 document complete and valid
- [ ] JSON Schema files valid and comprehensive
- [ ] Response validation tests pass (responses match schema)
- [ ] Health/readiness/version endpoints working
- [ ] FHSS-only routes (verify by inspection)

### Operator Integration

- [ ] `fhss_dashboard_operator.py` created and documented
- [ ] All commands working and tested by external user
- [ ] Operator workflow runs from documented README
- [ ] Report JSON machine-readable and complete
- [ ] Operator can launch dashboard and exercise Phase 1 features

### Testing

- [ ] C++26 build passes with dashboard enabled and disabled
- [ ] ASan/UBSan parser and server tests passing (no issues)
- [ ] All unit tests wired (not skipped)
- [ ] API contract tests validating responses
- [ ] Installed-tree smoke tests passing
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] No compiler warnings

### Verification (Independent)

- [ ] Verifier runs operator workflow as external user
- [ ] Packet inspection confirms loopback-only binding
- [ ] All acceptance criteria verified independently
- [ ] No blocking/high-severity findings
- [ ] Prior phases remain passing (if any)

---

## Implementer Deliverables (What to Submit)

### Code Changes

1. **HTTP Server**
   - `src/dsp/fhss/EmbeddedDashboardServer.cpp` (rewritten with Boost.Beast)
   - `include/dsp/fhss/EmbeddedDashboardServer.hpp` (updated interface)

2. **Configuration and Build**
   - `CMakeLists.txt` (add FHSS_DASHBOARD option, Boost.Beast)
   - `CMakePresets.json` (if needed for build options)

3. **Assets and Contracts**
   - `docs/api/fhss-dashboard-v1.openapi.yaml`
   - `docs/api/schemas/` (all JSON Schema files)
   - `examples/DSP/dashboard/dist/` (built dashboard)
   - `examples/DSP/dashboard/index.html` (basic SPA shell)

4. **Operator Tool**
   - `examples/DSP/dashboard/operator/fhss_dashboard_operator.py`
   - `examples/DSP/dashboard/operator/README.md`
   - `examples/DSP/dashboard/operator/requirements.txt`

### Tests

1. **Unit Tests**
   - `test/unit/fhss_dashboard/test_http_server_limits.cpp`
   - `test/unit/fhss_dashboard/test_path_containment.cpp`
   - `test/unit/fhss_dashboard/test_response_format.cpp`

2. **Integration Tests**
   - `test/integration/fhss_dashboard/test_api_contract.cpp`
   - `test/integration/fhss_dashboard/test_installed_tree.cpp`

### Implementation Report

**File:** `IMPLEMENTATION_REPORT.md`

```markdown
# Phase 1 Implementation Report

## Summary
- Implementation date: YYYY-MM-DD
- Duration: X hours
- Scope completed: Y/Y acceptance criteria

## Files Changed
- List of all modified/created files
- Line counts for significant changes

## Tests Added
- Unit test count and coverage
- Integration test count
- Sanitizer test status (ASan/UBSan)

## Build Results
- C++26 compilation: PASS/FAIL
- No warnings: PASS/FAIL
- Dashboard-enabled build: PASS/FAIL
- Dashboard-disabled build: PASS/FAIL

## Operator Workflow Results
- prepare: PASS/output
- serve: PASS/output
- exercise: PASS/results
- verify: PASS/results
- report: PASS/JSON output
- cleanup: PASS/output

## Known Limitations
- (List any intentional deferrals)

## Standards Compliance Status
- (List which standards met/deferred)
```

---

## Orchestrator Post-Verification (Decision and Gating)

**Duration:** 30 minutes  
**Responsible:** Orchestration Agent  
**Input:** Implementer report + Verifier report  
**Output:** PASS/FAIL decision, finding disposition plan

### 1. Review Verifier Report

- [ ] Read entire VERIFICATION_REPORT.md
- [ ] Check each criterion's PASS/FAIL status
- [ ] List all blocking findings
- [ ] List all high-severity findings
- [ ] List all medium/low findings

### 2. Route Findings to Implementer

For each finding:

- **Blocking:** Implementer must fix immediately
  - Blocked from Phase 2 until resolved
  - May require design change
  
- **High:** Implementer must fix before verification sign-off
  - Cannot proceed until resolved
  
- **Medium:** Implementer should fix if scope allows
  - Document if deferred to later phase
  
- **Low:** Document and track
  - May be addressed in later phases

### 3. Disposition Decision

Create `plan/Phase1_Disposition.md`:

```markdown
# Phase 1 Disposition

Date: YYYY-MM-DD

## Finding Summary
- Blocking: X
- High: X
- Medium: X
- Low: X

## Resolution Status

### Blocking Findings
1. [Finding] → [Status: Fixed/Escalated]
   - Evidence: [Link to verifier update]
   
### High Findings
1. [Finding] → [Status: Fixed/Deferred]
   - Evidence: [Link]

### Resolution Timeline
- Implementer rework: YYYY-MM-DD
- Verifier re-check: YYYY-MM-DD
```

### 4. Gate Decision

**Phase 1 is APPROVED when:**

- [ ] All blocking findings resolved (verifier confirms)
- [ ] All high-severity findings resolved
- [ ] Implementer acceptance gates passed locally
- [ ] Verifier acceptance gates passed independently
- [ ] Operator workflow runs end-to-end
- [ ] All standards compliance criteria met

**Phase 1 is BLOCKED when:**

- [ ] Any blocking finding unresolved
- [ ] Verifier finds test is skipped/unwired
- [ ] Receiver receives generator truth (by any path)
- [ ] UI claims don't match actual data source
- [ ] Loopback-only binding not enforced
- [ ] Operator workflow cannot run from README

### 5. Approval Record

Create `plan/Phase1_Approval.md`:

```markdown
# Phase 1 Approval Record

Date: YYYY-MM-DD  
Decision: APPROVED / BLOCKED

## Approval Chain
- Implementer: [Name/Agent] — READY
- Verifier: [Name/Agent] — PASS with X findings
- Orchestrator: [Name/Agent] — APPROVED

## Conditions
- All Phase 1 acceptance criteria: PASS
- No blocking findings: CONFIRMED
- Standards compliance: CONFIRMED
- Operator workflow: REPRODUCIBLE

## Next Phase Prerequisite
✅ Phase 1 complete. Approved to proceed to Phase 2.

Phase 2 start condition:
  - Same repository, main branch
  - Phase 1 changes committed and merged
  - Phase 2 agent assignment and orientation
```

### 6. Commit and Communicate

- [ ] Merge Phase 1 implementation to main
- [ ] Tag release (e.g., `phase-1-complete`)
- [ ] Record approval in repo
- [ ] Brief Phase 2 orchestrator
- [ ] Archive Phase 1 reports

---

## Quality Gates and Testing Strategy

### Implementer Pre-Submission Gates

Implementer must verify locally:

- [ ] C++26 build succeeds (both WITH and WITHOUT dashboard)
- [ ] No compiler warnings (treat as errors)
- [ ] All unit tests passing
- [ ] ASan/UBSan tests passing (no memory/undefined behavior issues)
- [ ] Operator workflow runs end-to-end
- [ ] Installed-tree launch succeeds (no source directory needed)
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] IMPLEMENTATION_REPORT.md complete and reviewed

### Verifier Independent Gates

Verifier must confirm independently:

- [ ] Code review of HTTP server implementation
- [ ] Security review of path containment and CSP
- [ ] API contract validation against OpenAPI/JSON Schema
- [ ] Operator workflow tested as external user
- [ ] Packet inspection of network binding
- [ ] Installed-tree validation (clean environment)
- [ ] All prior tests remain green (regression check)

### Orchestrator Approval Gates

Orchestrator must verify:

- [ ] Implementer gates all passed
- [ ] Verifier gates all passed
- [ ] All blocking findings resolved
- [ ] Standards compliance complete (or deferred with justification)
- [ ] Operator workflow reproducible from documentation
- [ ] No unresolved high-severity findings

---

## Phase 1 Completion Criteria

**Phase 1 is COMPLETE only when:**

1. ✅ Implementer: All local acceptance gates passed
2. ✅ Verifier: All independent verification gates passed
3. ✅ Operator: Workflow runs end-to-end from documentation
4. ✅ Tests: All unit/integration/sanitizer tests passing
5. ✅ Regression: Prior phases remain green (if any)
6. ✅ Standards: RFC 9110/9112/9457, CSP, loopback binding all met
7. ✅ Orchestrator: No blocking or unresolved high-severity findings
8. ✅ Approval: Phase 1 approval record signed and committed

---

## Multi-Agent Workflow Summary

### Orchestrator Flow

```
1. PREPARE
   ├─ Audit repository state
   ├─ Preserve baseline (hashes)
   ├─ Create checklist
   ├─ Map standards
   ├─ Assign implementer + verifier
   └─ Record snapshot

2. AWAIT IMPLEMENTATION
   └─ Implementer submits IMPLEMENTATION_REPORT.md

3. ASSIGN VERIFICATION
   └─ Verifier assigned to independent review

4. AWAIT VERIFICATION
   └─ Verifier submits VERIFICATION_REPORT.md

5. REVIEW & ROUTE FINDINGS
   ├─ Review blocking findings
   ├─ Route to implementer for disposition
   ├─ Coordinate rework if needed
   └─ Repeat until all blocking resolved

6. GATE DECISION
   ├─ All criteria met? → APPROVE
   └─ Unresolved findings? → BLOCK (with escalation)

7. COMMIT & COMMUNICATE
   ├─ Merge to main
   ├─ Tag release
   ├─ Record approval
   └─ Brief Phase 2 orchestrator
```

### Implementer Flow

```
1. RECEIVE ASSIGNMENT
   └─ Read implementation brief + checklist

2. IMPLEMENT PHASE 1
   ├─ Replace HTTP parser (Boost.Beast)
   ├─ Add loopback-only binding
   ├─ Implement request/response limits
   ├─ Add security headers + CSP
   ├─ Create OpenAPI + JSON Schema
   ├─ Build operator tool
   └─ Add comprehensive tests

3. VERIFY LOCALLY
   ├─ C++26 build (ON and OFF)
   ├─ ASan/UBSan tests
   ├─ Operator workflow
   ├─ Installed-tree launch
   └─ All gates PASS

4. SUBMIT REPORT
   └─ IMPLEMENTATION_REPORT.md + evidence

5. AWAIT VERIFICATION
   └─ Verifier reviews independently

6. RECEIVE FINDINGS
   ├─ Review all findings (blocking/high/medium/low)
   ├─ Fix blocking/high issues
   └─ Return updated evidence

7. COMPLETE & COMMIT
   └─ Merge to main after approval
```

### Verifier Flow

```
1. RECEIVE ASSIGNMENT
   └─ Read implementation brief + acceptors criteria

2. REVIEW IMPLEMENTATION REPORT
   └─ Understand what was delivered

3. VERIFY INDEPENDENTLY
   ├─ Code inspection (HTTP server, security)
   ├─ Run tests (unit, integration, installed-tree)
   ├─ Run operator workflow as external user
   ├─ Packet inspection (loopback binding)
   ├─ API response validation
   └─ Regression testing

4. DOCUMENT FINDINGS
   ├─ Each criterion: PASS or FAIL
   ├─ Evidence location (file:line)
   ├─ Severity (blocking/high/medium/low)
   ├─ Overall PASS or FAIL
   └─ VERIFICATION_REPORT.md

5. SUBMIT REPORT
   └─ Evidence + recommendations

6. AWAIT ORCHESTRATOR DECISION
   └─ If blocking findings → await implementer fixes
   └─ If all pass → approve Phase 1

7. SIGN OFF
   └─ Phase 1 approval record
```

---

## Known Limitations and Deferred Work

| Item | Phase | Reason |
|------|-------|--------|
| Configuration mutation (RFC 6902 patches) | 2 | Requires configuration authority first |
| Real runtime execution | 3 | Requires configuration baseline first |
| Metrics and telemetry | 4 | Depends on real runtime |
| WebSocket streaming | 6 | Requires event model design |
| Investigation artifacts (SigMF) | 7 | Depends on Phase 5+ job controller |
| Accessibility (WCAG 2.2 AA) | 8 | After all features complete |

---

## Orchestrator Phase 1 Initialization

---

## Model Assignment for Multi-Agent Workflow

**Recommended Model Assignments:**

| Role | Model | Rationale |
|------|-------|-----------|
| **Orchestrator** | Claude 3.5 Sonnet (Copilot) | Process design, checklist creation, findings routing (medium complexity) |
| **Implementer** | Claude 3.5 Sonnet (Copilot) | HTTP/RFC/security/API/testing breadth; Phase 1 foundation (very high complexity) |
| **Verifier** | Claude 3.5 Sonnet (Copilot) **[FRESH INSTANCE]** | Security expertise, adversarial testing, independent verification (high complexity) |

**Critical:** Verifier must be a **separate instance** from Implementer to preserve independence. Same model, different agent session.

### Agent Invocation with Model Specification

#### Orchestrator Phase Invocation

```python
runSubagent(
  agentName="Explore",
  model="Claude 3.5 Sonnet (Copilot)",
  description="Phase 1 Orchestrator - Preparation",
  prompt="""
  # Phase 1 Orchestrator Preparation Prompt
  
  [See "Orchestrator Preparation" section below]
  
  Execute the following checklist:
  1. Audit repository state (clean, no conflicts)
  2. Preserve baseline hashes (current dashboard code)
  3. Create Phase1_Acceptance_Checklist.md
  4. Create Phase1_PreImplementation_Snapshot.json
  5. Map standards compliance requirements
  6. Assign implementer and verifier agents (separate instances)
  7. Record pre-implementation snapshot
  
  Output: Orchestrator preparation complete. Ready to assign to Implementer.
  """
)
```

#### Implementer Phase Invocation

```python
runSubagent(
  agentName="[Implementation-focused agent]",
  model="Claude 3.5 Sonnet (Copilot)",
  description="Phase 1 Implementer - Development",
  prompt="""
  # Phase 1 Implementer Implementation Prompt
  
  [Use full "Implementer Technical Requirements" section from this prompt]
  
  You are the IMPLEMENTER. Your scope is Phase 1 only:
  - Replace HTTP parser with Boost.Beast
  - Implement loopback-only binding
  - Add request/response limits
  - Implement CSP headers + security
  - Create OpenAPI + JSON Schema contracts
  - Build operator tool (Python)
  - Add comprehensive tests (unit, integration, ASan/UBSan)
  
  Constraints:
  - Do NOT implement Phase 2+ configuration logic
  - Do NOT add runtime owner (Phase 3)
  - Do NOT add metrics collection (Phase 4)
  - Do NOT add WebSocket streaming (Phase 6)
  
  Deliverables:
  - IMPLEMENTATION_REPORT.md
  - All modified/created files
  - Test results
  - Operator workflow evidence
  
  Local acceptance gates (verify before submission):
  - C++26 build (ON and OFF): PASS
  - No compiler warnings: PASS
  - ASan/UBSan tests: PASS
  - Operator workflow end-to-end: PASS
  - Installed-tree launch: PASS
  - git diff --check: PASS
  """
)
```

#### Verifier Phase Invocation

```python
runSubagent(
  agentName="[Security-focused agent]",  # MUST be different instance from Implementer
  model="Claude 3.5 Sonnet (Copilot)",
  description="Phase 1 Verifier - Independent Verification",
  prompt="""
  # Phase 1 Verifier Independent Verification Prompt
  
  [See "Verifier Assignment" section from orchestration prompt]
  
  You are the VERIFIER. You CANNOT edit implementation files.
  
  Your role: Independent verification with direct evidence
  
  Verify each criterion independently (do NOT trust console output):
  
  HTTP Server:
    □ Loopback-only binding (packet inspection proof)
    □ Request/response limits enforced
    □ Graceful shutdown working
    
  Security:
    □ CSP headers on all responses (check actual HTTP responses)
    □ No unsafe-inline without hashes
    □ Path traversal rejected (/assets/../../../etc/passwd)
    □ RFC 9457 error format consistent
    
  API Contract:
    □ OpenAPI schema valid YAML
    □ JSON Schema files complete
    □ Response validation tests pass
    
  Operator Integration:
    □ All 6 commands work (prepare, serve, exercise, verify, report, cleanup)
    □ Run as external user from README (no insider knowledge)
    □ Report JSON valid and complete
    
  Testing & Quality:
    □ C++26 build (ON and OFF): PASS
    □ ASan/UBSan tests: no issues
    □ Installed-tree launch: PASS
    □ No compiler warnings
    
  Output: VERIFICATION_REPORT.md with:
    - Each criterion: PASS or FAIL with evidence
    - Blocking findings (must fix)
    - High findings (must fix)
    - Medium/low findings (document)
    - Overall recommendation: PASS/FAIL
  """
)
```

---

### Orchestrator To-Do List (Before Assigning Work)

**Run through this checklist before assigning to Implementer:**

- [ ] Run orchestrator preparation steps (see "Orchestrator Preparation" section)
- [ ] Create `plan/Phase1_Acceptance_Checklist.md`
- [ ] Create `plan/Phase1_PreImplementation_Snapshot.json`
- [ ] Hash existing dashboard code for baseline
- [ ] Select Implementer agent and invoke with **Claude 3.5 Sonnet (Copilot)** model
- [ ] Select Verifier agent (**SEPARATE INSTANCE**) and invoke with **Claude 3.5 Sonnet (Copilot)** model
- [ ] Brief both on multi-agent workflow and role separation
- [ ] Confirm both agents understand stop rules (no skipping phases)
- [ ] Confirm Implementer has commit access to repo
- [ ] Confirm Verifier will NOT edit implementation files
- [ ] Commit phase 1 orchestration plan to repo
- [ ] Notify stakeholders of Phase 1 timeline (1 week + 2-3 days verification)

---

## References

### Standards and Specifications

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110.html)
- [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112.html)
- [RFC 9457: Problem Details for HTTP APIs](https://www.rfc-editor.org/rfc/rfc9457.html)
- [RFC 6455: WebSocket Protocol](https://www.rfc-editor.org/rfc/rfc6455.html)
- [RFC 6901/6902: JSON Pointer and Patch](https://www.rfc-editor.org/rfc/rfc6901.html)
- [OpenAPI 3.1.2](https://spec.openapis.org/oas/v3.1.2.html)
- [JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12)
- [WCAG 2.2 AA](https://www.w3.org/TR/WCAG22/)

### Architecture and Planning

- [fhss_dashboard_report_v2.md](../../docs/fhss_dashboard_report_v2.md) — Full architecture review
- [fhss_dashboard_implementation_plan.md](../../docs/dsp/fhss_dashboard_implementation_plan.md) — Phase roadmap
- [fhss-dashboard-v2.md](../../docs/dsp/fhss-dashboard-v2.md) — V2 recommendation

### Tools and Libraries

- [Boost.Beast Documentation](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/index.html)
- [Boost.Asio Documentation](https://www.boost.org/doc/libs/latest/libs/asio/doc/html/index.html)
- [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Clang UBSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)

### Phase 1 Orchestration Artifacts (Create During Preparation)

- `plan/Phase1_Acceptance_Checklist.md` — File-level acceptance checklist
- `plan/Phase1_PreImplementation_Snapshot.json` — Repository baseline
- `plan/Phase1_Disposition.md` — Finding disposition record (after verification)
- `plan/Phase1_Approval.md` — Phase 1 approval record (after gating decision)

---

## Workflow Diagram (With Model Assignments)

```
┌─────────────────────────────────────────────────────────────┐
│ ORCHESTRATOR PREPARATION (30 min)                           │
│ Model: Claude 3.5 Sonnet (Copilot)                          │
├─────────────────────────────────────────────────────────────┤
│ 1. Audit repository state                                   │
│ 2. Preserve baseline hashes                                 │
│ 3. Create acceptance checklist                              │
│ 4. Map standards compliance                                 │
│ 5. Assign Implementer (new instance)                        │
│ 6. Assign Verifier (separate instance — CRITICAL)          │
│ 7. Record pre-implementation snapshot                        │
│                                                             │
│ Output: Orchestrator preparation complete                   │
└────────┬────────────────────────────────────────────────────┘
         │ (provide prepared brief to each agent)
         ├──────────────────────────┬──────────────────────────┐
         ▼                          ▼
┌──────────────────────────┐  ┌──────────────────────────────┐
│ IMPLEMENTER PHASE        │  │ VERIFIER PHASE               │
│ (1 week)                 │  │ (2-3 days)— INDEPENDENT     │
├──────────────────────────┤  ├──────────────────────────────┤
│ Model:                   │  │ Model:                       │
│ Claude 3.5 Sonnet        │  │ Claude 3.5 Sonnet            │
│ (Copilot)                │  │ (Copilot) — FRESH INSTANCE   │
├──────────────────────────┤  ├──────────────────────────────┤
│ 1. Replace HTTP parser   │  │ 1. Review implementation     │
│    (Boost.Beast)         │  │    (as external user)        │
│ 2. Loopback-only binding │  │ 2. Run tests independently   │
│ 3. Request/response      │  │ 3. Verify operator workflow  │
│    limits               │  │ 4. Packet inspection proof   │
│ 4. CSP headers +         │  │ 5. API response validation   │
│    security             │  │ 6. Installed-tree launch     │
│ 5. OpenAPI +             │  │ 7. Document findings         │
│    JSON Schema          │  │    (blocking/high/med/low)   │
│ 6. Operator tool         │  │ 8. Submit VERIFICATION_REPORT
│    (Python)             │  │                              │
│ 7. Comprehensive tests   │  │ Stop Rule: Return FAIL if    │
│ 8. Verify locally        │  │   - Operator can't run       │
│ 9. Submit IMPLEMENTATION │  │   - Receiver gets truth      │
│    _REPORT.md            │  │   - Tests are skipped        │
│                          │  │   - Blocking findings exist  │
└────────┬─────────────────┘  └──────────┬───────────────────┘
         │                              │
         └──────────────────┬───────────┘
                            ▼
        ┌─────────────────────────────────────┐
        │ ORCHESTRATOR DECISION & GATING      │
        │ (30 min)                            │
        ├─────────────────────────────────────┤
        │ 1. Review Verifier findings         │
        │ 2. Route blocking findings to       │
        │    implementer for rework           │
        │ 3. If blocking: repeat cycle        │
        │    (implementer → verifier)         │
        │ 4. If all pass: APPROVE Phase 1     │
        │ 5. Merge to main, tag release       │
        │ 6. Record approval                  │
        │ 7. Brief Phase 2 orchestrator       │
        └────────┬────────────────────────────┘
                 │
                 ▼
          ✅ PHASE 1 COMPLETE
          Ready for Phase 2
```

---

**Prompt Version:** 2.1 (Multi-Agent with Model Specifications)  
**Last Updated:** 2026-07-24  
**Status:** Ready for Orchestrator Execution with Model Assignments
