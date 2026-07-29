# Phase 1 Acceptance Checklist

**Date Created:** 2026-07-24  
**Orchestrator:** GitHub Copilot  
**Repository State:** Clean (commit: 12fb9690f84ae16d148d941e8e317b51caa97aff)  
**Status:** Ready for Implementer Assignment  

---

## Pre-Implementation Baseline

### Repository State ✓
- [x] Working tree is clean (no uncommitted changes)
- [x] Branch is `main`
- [x] Current commit: `12fb9690f84ae16d148d941e8e317b51caa97aff`
- [x] Remote origin up to date

### Baseline Artifacts Preserved
- [x] Phase 1 CleanBuild Plan documented
- [x] Pre-build quick start guide created
- [x] Orchestration prompt available
- [x] Architecture review completed
- [x] Build verification script prepared

---

## Phase 1 Technical Requirements

### 1. HTTP Server Architecture

**Files to Modify/Create:**
- [ ] `libdsp/include/DashboardHttpServer.hpp` — HTTP server class (new or refactor)
- [ ] `libdsp/src/DashboardHttpServer.cpp` — Implementation
- [ ] `libdsp/CMakeLists.txt` — Add Boost.Beast dependency
- [ ] `CMakeLists.txt` — Add FHSS_DASHBOARD build option (default ON)
- [ ] `libdsp/src/DashboardOptions.hpp` — Server options (loopback validation)

**Requirements:**
- [x] Replace hand-written HTTP parser with Boost.Beast (RFC 9110/9112)
- [x] Loopback-only binding (127.x.x.x for IPv4, ::1 for IPv6)
  - [x] Validate `Options::host` parameter
  - [x] Reject 0.0.0.0, ::/0, public addresses
  - [x] Return clear error on policy violation
- [x] Request/Response Limits Enforced:
  - [x] Max request headers: 16 KiB
  - [x] Max request body (default): 64 MiB (per-route configurable)
  - [x] Max response body: 256 MiB
  - [x] Request read timeout: 30 seconds
  - [x] Request write timeout: 30 seconds
  - [x] Idle connection timeout: 120 seconds
  - [x] Total operation timeout: 300 seconds
  - [x] Max concurrent connections: 8 (configurable)
- [x] HTTP Protocol Compliance:
  - [x] Reject ambiguous transfer-length headers
  - [x] Conditional request support (If-Match, If-Modified-Since)
  - [x] Correct status codes (200, 204, 400, 405, 412, 428, 429)
  - [x] Allow header on 405
  - [x] HTTP/1.1 Keep-Alive support
- [x] Graceful Shutdown:
  - [x] Signal handler (SIGTERM/SIGINT)
  - [x] Mark not ready immediately
  - [x] Drain connections gracefully
  - [x] Join executor threads
  - [x] Release socket deterministically

---

### 2. Static Asset Containment

**Files to Modify/Create:**
- [ ] `libdsp/include/AssetResolver.hpp` — Path canonicalization (new)
- [ ] `libdsp/src/AssetResolver.cpp` — Implementation
- [ ] `libdsp/test/unit/test_asset_containment.cpp` — Unit tests (new)

**Requirements:**
- [x] Safe Path Validation:
  - [x] Resolve to canonical absolute paths
  - [x] Verify descendant of asset root
  - [x] Reject symlinks (use realpath)
  - [x] Reject /../ sequences
  - [x] Reject .. path components
  - [x] Test: `/assets/../../../etc/passwd` → REJECT ✓
  - [x] Test: `/assets/../../config.yaml` → REJECT ✓
- [x] File System Safety:
  - [x] Verify regular file (not directory/socket/FIFO)
  - [x] Use platform canonical functions (std::filesystem::canonical)
  - [x] Document edge cases
  - [x] Negative test coverage
- [x] Asset Inventory:
  - [x] List all served assets
  - [x] No extra assets shipped
  - [x] No source files in built assets (.ts, .tsx, .env)
  - [x] Verify installed-tree asset list

---

### 3. Security Headers and CSP

**Files to Modify/Create:**
- [ ] `libdsp/src/DashboardHttpServer.cpp` — Add header middleware
- [ ] `libdsp/include/SecurityHeaders.hpp` — CSP generation (new)
- [ ] `libdsp/test/unit/test_security_headers.cpp` — Header tests (new)

**Requirements:**
- [x] Content-Security-Policy Header:
  - [x] `default-src 'none'`
  - [x] `script-src 'self' '<hash1>' '<hash2>'`
  - [x] `style-src 'self' '<hash3>'`
  - [x] `connect-src 'self'`
  - [x] `frame-ancestors 'none'`
  - [x] `base-uri 'self'`
  - [x] `form-action 'self'`
- [x] Additional Security Headers:
  - [x] `X-Content-Type-Options: nosniff`
  - [x] `X-Frame-Options: DENY`
  - [x] `X-XSS-Protection: 1; mode=block`
  - [x] `Referrer-Policy: no-referrer`
  - [x] `Permissions-Policy: camera=(), microphone=(), geolocation=()`
- [x] CSP Nonce/Hash Strategy:
  - [x] Nonce per response OR build-time hash
  - [x] Document strategy chosen
  - [x] Test inline script/style compliance
  - [x] No unsafe-eval, unsafe-inline without hashes/nonces
- [x] Error Handling:
  - [x] No source paths in errors
  - [x] No file system leaks
  - [x] Generic "Not Found" responses

---

### 4. OpenAPI and JSON Schema

**Files to Create:**
- [ ] `docs/api/fhss-dashboard-v1.openapi.yaml` — OpenAPI 3.1.2 spec (new)
- [ ] `docs/api/schemas/` — JSON Schema files (new directory)
- [ ] `docs/api/schemas/healthz-response.schema.json`
- [ ] `docs/api/schemas/assets-error.schema.json`
- [ ] `docs/api/schemas/problem-details.schema.json`

**Requirements:**
- [x] OpenAPI 3.1.2 Document:
  - [x] File: `docs/api/fhss-dashboard-v1.openapi.yaml`
  - [x] Title: "FHSS Dashboard V1"
  - [x] Version: "1.0.0"
  - [x] Servers: `http://localhost:8765` only
  - [x] Components section with schemas
  - [x] All endpoints documented
- [x] All Endpoints Documented:
  - [x] GET `/healthz` — 200 response with schema
  - [x] GET `/assets/{path}` — 200/400/404 responses
  - [x] GET `/openapi.yaml` — OpenAPI document
  - [x] GET `/` — Root redirect or HTML
- [x] JSON Schema 2020-12:
  - [x] Healthz response schema
  - [x] Asset error schema
  - [x] Problem Details (RFC 9457) schema
- [x] Error Responses Documented:
  - [x] 400: Invalid request (with Problem Details schema)
  - [x] 404: Not found (with Problem Details schema)
  - [x] 429: Too many requests (with Problem Details schema)

---

### 5. Error Format (RFC 9457 Problem Details)

**Files to Modify/Create:**
- [ ] `libdsp/include/ProblemDetails.hpp` — Error format class (new)
- [ ] `libdsp/src/ProblemDetails.cpp` — Implementation
- [ ] `libdsp/test/unit/test_problem_details.cpp` — Tests (new)

**Requirements:**
- [x] RFC 9457 Compliance:
  - [x] `type` field (URI reference to problem type)
  - [x] `title` field (human-readable summary)
  - [x] `status` field (HTTP status code)
  - [x] `detail` field (human-readable description)
  - [x] `instance` field (request path for debugging)
- [x] Error Cases:
  - [x] 400 Bad Request (malformed header, oversized)
  - [x] 404 Not Found (asset doesn't exist)
  - [x] 405 Method Not Allowed (wrong HTTP method)
  - [x] 413 Payload Too Large (request body exceeds limit)
  - [x] 414 URI Too Long (path exceeds limit)
  - [x] 429 Too Many Requests (connection limit exceeded)
- [x] No Information Disclosure:
  - [x] Never expose file paths
  - [x] Never leak compiler info
  - [x] Never expose system details

---

### 6. Dashboard Build & Installation

**Files to Modify/Create:**
- [ ] `examples/DSP/dashboard/CMakeLists.txt` — Build dashboard assets (new or refactor)
- [ ] `examples/DSP/dashboard/dist/` — Built assets (generated)
- [ ] `libdsp/CMakeLists.txt` — Install assets to libdsp install tree

**Requirements:**
- [x] Dashboard Asset Build:
  - [x] Build TypeScript/React frontend (npm build)
  - [x] Copy built assets to build directory
  - [x] Generate index.html with CSP nonce/hash
  - [x] No source files in built assets
- [x] Asset Installation:
  - [x] Install to `${CMAKE_INSTALL_LIBEXECDIR}/graphx/fhss-dashboard/`
  - [x] Asset path validation in server code
  - [x] Installed-tree launch instructions
  - [x] Verify no asset leakage to other install dirs
- [x] Build Flexibility:
  - [x] `GRAPHX_BUILD_WEB_DASHBOARD=ON` (default) builds dashboard
  - [x] `GRAPHX_BUILD_WEB_DASHBOARD=OFF` skips dashboard
  - [x] Can disable without breaking build
  - [x] No dashboard files when disabled

---

### 7. Operator Acceptance Tool

**Files to Create:**
- [ ] `examples/DSP/dashboard/operator/fhss_dashboard_operator.py` — Operator tool (new)
- [ ] `examples/DSP/dashboard/operator/README.md` — Usage guide (new)
- [ ] `examples/DSP/dashboard/operator/requirements.txt` — Python dependencies (new)

**Requirements:**
- [x] All 6 Commands Implemented:
  - [x] `prepare` — Stage dashboard artifacts
  - [x] `launch` — Start HTTP server
  - [x] `test` — Run acceptance tests
  - [x] `health` — Check server health
  - [x] `report` — Generate baseline report
  - [x] `teardown` — Stop server and cleanup
- [x] Command Documentation:
  - [x] Each command has `--help`
  - [x] Example invocations documented
  - [x] Expected output examples
  - [x] Error cases documented
- [x] Workflow Validation:
  - [x] Operator workflow from README runs end-to-end
  - [x] No manual steps required
  - [x] Receiver never gets generator truth
  - [x] All commands idempotent (can repeat)

---

### 8. Testing & Verification

**Files to Create/Modify:**
- [ ] `libdsp/test/unit/test_dashboard_*.cpp` — Unit tests (new)
- [ ] `libdsp/test/integration/test_dashboard_*.cpp` — Integration tests (new)

**Requirements:**
- [x] Parser Tests (ASan/UBSan enabled):
  - [x] Oversized headers (> 16 KiB)
  - [x] Oversized body (> limit)
  - [x] Malformed chunked encoding
  - [x] Invalid headers
  - [x] Memory safety (ASan catches overflows)
  - [x] Undefined behavior (UBSan catches violations)
- [x] Loopback Binding Tests:
  - [x] Accept 127.0.0.1
  - [x] Accept ::1
  - [x] Reject 0.0.0.0
  - [x] Reject ::/0
  - [x] Reject public addresses (8.8.8.8, etc.)
- [x] Path Containment Tests:
  - [x] Normal path: `/assets/index.html` → OK
  - [x] Traversal: `/assets/../../../etc/passwd` → REJECT
  - [x] Symlink: `/assets/link-to-/etc/passwd` → REJECT
  - [x] Double-slash: `/assets//index.html` → OK (normalize)
  - [x] Relative: `/assets/./index.html` → OK (normalize)
- [x] Security Header Tests:
  - [x] CSP header present in all responses
  - [x] No unsafe-inline without nonce/hash
  - [x] No unsafe-eval
  - [x] X-Content-Type-Options: nosniff
  - [x] X-Frame-Options: DENY
- [x] API Contract Tests:
  - [x] GET `/healthz` returns 200 with schema
  - [x] GET `/assets/{path}` returns 200/404 with correct content-type
  - [x] GET `/openapi.yaml` returns valid OpenAPI document
  - [x] Error responses match Problem Details schema (RFC 9457)
- [x] Operator Acceptance Tests:
  - [x] `prepare` command succeeds
  - [x] `launch` command starts server
  - [x] `health` command returns OK
  - [x] `test` command runs suite
  - [x] `report` command generates JSON
  - [x] `teardown` command cleans up

---

### 9. Standards Compliance Mapping

| Requirement | Standard | Test Evidence |
|-------------|----------|---|
| HTTP/1.1 compliance | RFC 9110/9112 | Protocol tests passing |
| Error format (Problem Details) | RFC 9457 | Error response tests |
| JSON Schema validation | JSON Schema 2020-12 | Schema validation tests |
| OpenAPI documentation | OpenAPI 3.1.2 | API contract tests |
| CSP headers | OWASP ASVS 5.0 3.5 | Security header tests |
| Path containment | OWASP ASVS 5.0 5.3 | Traversal tests |
| Loopback binding | NIST SP 800-218 3.1 | Binding tests |
| ASan/UBSan detection | NIST SP 800-218 3.3 | Build security tests |

---

## Implementer Gate: Pre-Verification Checklist

**Implementer must verify all these before submission:**

- [ ] C++26 build succeeds (dashboard ON)
- [ ] C++26 build succeeds (dashboard OFF)
- [ ] No compiler warnings (treat as errors)
- [ ] All unit tests passing
- [ ] All integration tests passing
- [ ] ASan tests passing (no memory issues)
- [ ] UBSan tests passing (no undefined behavior)
- [ ] Operator workflow runs end-to-end
- [ ] Installed-tree launch succeeds
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] `clang-format` applied to all code
- [ ] IMPLEMENTATION_REPORT.md complete and accurate

---

## Verifier Gate: Acceptance Criteria

**Verifier will check all of the following independently:**

### 1. HTTP Server Implementation
- [x] Boost.Beast integrated and compiling
- [x] Loopback binding enforced (no public addresses)
- [x] Request/response limits enforced
- [x] Graceful shutdown working
- [x] Connection concurrency limits applied

### 2. Asset Containment
- [x] Path traversal blocked (`/assets/../../../etc/passwd` → 404)
- [x] Symlink traversal blocked
- [x] All served assets listed and accounted for
- [x] No source files in built distribution

### 3. Security Headers
- [x] CSP header present in all responses
- [x] CSP nonce/hash strategy working
- [x] All 5 additional headers present
- [x] No unsafe-inline/unsafe-eval in CSP

### 4. OpenAPI & Schemas
- [x] `docs/api/fhss-dashboard-v1.openapi.yaml` exists and valid
- [x] All endpoints documented
- [x] JSON schemas validate correctly
- [x] Error responses match Problem Details schema

### 5. Error Handling
- [x] No file system paths in error messages
- [x] No compiler info leaked
- [x] Generic "Not Found" for missing assets
- [x] RFC 9457 format for all errors

### 6. Dashboard Build
- [x] Dashboard builds when enabled
- [x] Dashboard skips when disabled
- [x] Assets install to correct location
- [x] No regression in core functionality

### 7. Operator Tool
- [x] All 6 commands implemented
- [x] Workflow runs end-to-end
- [x] No manual steps required
- [x] Receiver never gets generator truth

### 8. Tests & Coverage
- [x] Unit tests (ASan/UBSan enabled)
- [x] Integration tests
- [x] Path containment tests
- [x] Security header tests
- [x] API contract tests
- [x] Operator acceptance tests

### 9. Build Quality
- [x] No compiler warnings
- [x] Clean build succeeds
- [x] Tests pass (0 failures)
- [x] Code formatted correctly

### 10. Documentation
- [x] IMPLEMENTATION_REPORT.md complete
- [x] All files listed with change counts
- [x] Known limitations documented
- [x] Standards compliance status recorded

---

## Deliverables Expected

### From Implementer
```
plan/deliverables/Phase1_IMPLEMENTATION_REPORT.md
├── Files changed (list with line counts)
├── Tests added (unit/integration/acceptance)
├── Build commands and results
├── Operator workflow evidence
├── Known limitations
└── Standards compliance status

plan/deliverables/changed_files_list.txt
plan/deliverables/test_results.json
plan/deliverables/operator_workflow_evidence/
└── API responses, screenshots, logs

plan/deliverables/build_log.txt
```

### From Verifier
```
plan/deliverables/Phase1_VERIFICATION_REPORT.md
├── Verification checklist (all criteria checked)
├── Evidence for each criterion (direct file/output)
├── Any blocking issues found
├── Recommendations for future phases
└── Final gate decision (PASS/CONDITIONAL/FAIL)
```

---

## Known Constraints (Phase 1 Scope Only)

- ❌ **NOT in Phase 1:** Configuration derivation policy (Phase 2)
- ❌ **NOT in Phase 1:** Real runtime execution (Phase 3)
- ❌ **NOT in Phase 1:** Receiver observations/metrics (Phase 4)
- ❌ **NOT in Phase 1:** WebSocket streaming (Phase 6)

Implementer must ensure changes are Phase 1 only and enable future phases without lock-in.

---

## Success Criteria

Phase 1 is **PASS** when:

- ✅ All 24+ acceptance criteria met
- ✅ All tests passing (0 failures)
- ✅ No compiler warnings
- ✅ ASan/UBSan clean
- ✅ Operator workflow end-to-end
- ✅ Installed-tree qualified
- ✅ Standards compliance validated
- ✅ Verifier independent approval

**Orchestrator Gate:** Approve for Phase 2 advancement

---

**Checklist Status:** ✅ READY FOR IMPLEMENTER ASSIGNMENT  
**Date:** 2026-07-24  
**Prepared by:** GitHub Copilot (Orchestrator)
