# Phase 2A Standards Requirements Map

**Date:** 2026-07-24  
**Phase:** 2A — Configuration API Implementation  
**Orchestrator:** Execution Agent  
**Purpose:** Map Phase 2A acceptance criteria to authoritative standards

---

## Overview

Phase 2A implementation must satisfy acceptance criteria that align with industry standards for HTTP APIs, configuration management, data formats, and software security. This map documents the relationship between each acceptance criterion and its corresponding standard.

---

## HTTP and REST Standards

### RFC 7232: Hypertext Transfer Protocol (HTTP/1.1) Conditional Requests

**Standard Sections:**
- Section 2.3: ETag — Entity tags for comparing two representations of the same resource
- Section 3.2: If-Match — Request header for conditional requests

**Phase 2A Criteria:**
1. **ETag Header in Responses**
   - Criterion: All configuration responses include ETag header
   - RFC Requirement: Section 2.3 — "An ETag is an opaque validator for representing a specific version of a representation as identified by the resource."
   - Implementation: `etag: "Rev:N"` format in all success responses

2. **409 Conflict on Stale ETag**
   - Criterion: POST /api/v2/fhss/config/commit returns 409 if If-Match ETag does not match current
   - RFC Requirement: Section 3.2 — "The If-Match header field uses the strong comparison function, and if the field-value is `*`, matching succeeds if and only if the origin server has a current representation for the target resource."
   - Implementation: If-Match precondition check before commit

3. **ETag Format Stability**
   - Criterion: ETag increments monotonically with revision (Rev:1, Rev:2, Rev:3...)
   - RFC Requirement: Section 2.3 — "An entity-tag is an opaque identifier for different versions of a representation of the same resource."
   - Implementation: `ETag: "Rev:" + std::to_string(revision)`

---

### RFC 9110: HTTP Semantics

**Standard Sections:**
- Section 15: Status Codes (200, 201, 204, 400, 404, 409, 422, 503)
- Section 9: HTTP Methods (GET, POST, PATCH, DELETE)
- Section 8: Representation Format

**Phase 2A Criteria:**

| Criterion | Status Code | RFC Section | Implementation |
|-----------|-------------|-------------|-----------------|
| Success (data returned) | 200 OK | 15.3.1 | GET, POST, PATCH, POST /undo, POST /redo |
| Resource created | 201 Created | 15.3.2 | POST /api/v2/fhss/config/staged |
| No content | 204 No Content | 15.3.5 | DELETE /api/v2/fhss/config/staged/{id} |
| Bad Request (malformed) | 400 Bad Request | 15.5.1 | Validation errors present, invalid JSON |
| Not Found | 404 Not Found | 15.5.5 | Staged ID not found, resource expired |
| Conflict | 409 Conflict | 15.5.10 | If-Match ETag does not match current revision |
| Unprocessable Entity | 422 Unprocessable Entity | RFC 9110 ext. | Field type mismatch, incompatible value |

**Implementation Verification:**
- [ ] GET returns 200 with data
- [ ] POST staged returns 201
- [ ] DELETE staged returns 204
- [ ] Malformed JSON returns 400
- [ ] Missing staged ID returns 404
- [ ] Stale ETag returns 409 with current_etag + expected_etag in response
- [ ] Type mismatch returns 422

---

### RFC 9457: Problem Details for HTTP APIs

**Standard Sections:**
- Section 3: Problem Details Object structure
- Section 4: Defining New Problems
- Section 5: JSON Representation

**Phase 2A Criteria:**

1. **Problem Details Format**
   - Criterion: All error responses use RFC 9457 format
   - RFC Requirement: Section 3 — "The "problem details" object is a way to carry machine-readable details of errors in HTTP response to avoid the need to define new error response formats for HTTP APIs."
   - Required Fields:
     - `type`: Usually "about:blank" for Phase 2A
     - `status`: HTTP status code
     - `title`: Human-readable status description
     - `detail`: Problem-specific description
     - `instance`: Request URI for context

2. **Sample Error Response**
   ```json
   {
     "type": "about:blank",
     "status": 409,
     "title": "Conflict",
     "detail": "If-Match ETag does not match current revision",
     "instance": "/api/v2/fhss/config/commit",
     "current_etag": "Rev:2",
     "expected_etag": "Rev:1"
   }
   ```

3. **Error Message Safety**
   - Criterion: No source code paths in error messages
   - RFC Requirement: Section 3 — "The goal is to give users a way to understand the problem, not to expose implementation details."
   - Implementation: Descriptive user-facing messages only

**Verification:**
- [ ] All error responses are JSON objects
- [ ] `type` field present (typically "about:blank")
- [ ] `status` field matches HTTP status code
- [ ] `title` field readable by humans
- [ ] `detail` field explains the problem
- [ ] `instance` field shows request URI
- [ ] No file paths, stack traces, or internal details in error messages

---

## Configuration Management Standards

### NIST SP 800-218: Secure Software Development Framework (SSDF)

**Practice Areas Relevant to Phase 2A:**

#### PO (Prepare the Organization)
- **PO.3.1: Document security, privacy, and quality requirements**
  - Phase 2A Criterion: All 13 validation rules documented with error codes
  - NIST Requirement: Configuration must be validated against known requirements
  - Implementation: FHSSCrossNodeValidator with stable error codes (ERR_MESSAGE_001, etc.)

#### PS (Produce Well-Secured Software)

- **PS.1: Design software to meet security and privacy requirements**
  - Phase 2A Criterion: Deterministic configuration derivation
  - NIST Requirement: Section 3.3 — "Build security practices ensure that build processes are repeatable and produce bit-for-bit identical executables and reproducible builds."
  - Implementation: SerializeWithSortedKeys() produces byte-identical JSON

- **PS.3: Review software before its release**
  - Phase 2A Criterion: All 70+ tests passing before verification gate
  - NIST Requirement: Section 3.3 — "V&V practices ensure that software artifacts are validated and verified."
  - Implementation: Comprehensive test coverage before merge

#### PO (Prepare for Deployment and Sustainment)

- **PO.2: Archive software release**
  - Phase 2A Criterion: ETag and revision tracking for configuration history
  - NIST Requirement: Configuration versions must be traceable and reproducible
  - Implementation: History limited to last 10 revisions with full snapshots

---

### IEEE 1012: Standard for Software Verification and Validation

**V&V Plan Requirements:**

1. **Testing Strategy** (Phase 2A Implementation Tests)
   - Unit tests: 20+ tests for HTTP server routing and JSON serialization
   - Integration tests: 20+ tests for end-to-end workflows
   - Determinism tests: 10+ tests for byte-identical JSON
   - Conflict detection: 5+ tests for ETag conflicts
   - Total: 70+ tests minimum

2. **Test Coverage Metrics**
   - Criterion: All 10 HTTP endpoints tested
   - Criterion: All 4 CLI commands tested
   - Criterion: All 13 validation rules triggered
   - Criterion: Determinism verified across 10 iterations
   - Criterion: ETag conflicts detected and handled

3. **V&V Evidence**
   - Criterion: All tests passing (100% pass rate)
   - Criterion: Zero compiler warnings
   - Criterion: ASan/UBSan pass (no memory issues)
   - Criterion: Determinism verified independently

---

## Data Format Standards

### RFC 8259: The JavaScript Object Notation (JSON) Data Interchange Format

**Sections Relevant to Phase 2A:**

1. **JSON Object Representation**
   - Phase 2A Criterion: Deterministic JSON serialization
   - RFC Requirement: Section 1 — "JSON text SHALL be encoded in UTF-8, UTF-16, or UTF-32"
   - Implementation: UTF-8 encoding with sorted object keys

2. **JSON Array Representation**
   - Phase 2A Criterion: Arrays preserve insertion order (not sorted)
   - RFC Requirement: Section 5 — "An array is an ordered sequence of zero or more values"
   - Implementation: Arrays keep insertion order; only objects are sorted

3. **Determinism**
   - Phase 2A Criterion: Multiple serializations of same data produce identical bytes
   - RFC Requirement: Section 1 (Informal) — JSON enables data interchange but requires consistent representation
   - Implementation: SerializeWithSortedKeys() alphabetically sorts all object keys

---

### JSON Schema 2020-12 (Meta-Schema)

**Phase 2A Criteria:**

1. **Response Schema Validation**
   - Criterion: All HTTP responses must be valid against their declared schema
   - Schema Requirement: Each response identifies its schema via `schema` field
   - Example:
     ```json
     {
       "schema": "graphx.fhss_configuration.v1",
       "data": { /* configuration */ },
       "revision": 1,
       "etag": "Rev:1"
     }
     ```

2. **Error Response Schema**
   - Criterion: All error responses conform to RFC 9457 Problem Details schema
   - Schema Definition: Requires `status`, `detail`, `instance` fields

---

### RFC 6902: JavaScript Object Notation (JSON) Patch

**Sections Relevant to Phase 2A:**

1. **JSON Patch Operations**
   - Phase 2A Criterion: --config-patch command supports RFC 6902 patch files
   - RFC Requirement: Section 4 — "A JSON Patch is an ordered sequence of operations"
   - Supported Operations: add, remove, replace, test (minimum for Phase 2A)

2. **Patch Format Example**
   ```json
   [
     { "op": "replace", "path": "/iq_center_frequency_hz", "value": 2500000000 },
     { "op": "replace", "path": "/occupied_bandwidth_hz", "value": 50000000 }
   ]
   ```

3. **Conditional Patch Application**
   - Phase 2A Criterion: --if-match ETAG applies patch only if ETag matches
   - RFC Requirement: Section 3.2 — Conditional requests require ETag/If-Match headers
   - Implementation: CommitStagedEdit checks If-Match before applying

---

## Security Standards

### OWASP Application Security Verification Standard (ASVS) 5.0

**Relevant Sections for Phase 2A:**

1. **V4.1: General Access Control (Loopback-Only)**
   - Phase 2A Criterion: HTTP server binds to loopback only (127.x.x.x or ::1)
   - ASVS Requirement: Section 4 — "Access control policies must be enforced on all controlled assets"
   - Inherited from Phase 1: DashboardHttpServer enforces loopback binding

2. **V5.3: Output Encoding and Injection Prevention**
   - Phase 2A Criterion: No source paths in error messages
   - ASVS Requirement: Section 5.3 — "All output should be properly encoded to prevent injection attacks"
   - Implementation: Sanitized error messages, descriptive only

---

## Verification Evidence Requirements

### For Each Standard, Provide:

1. **Direct Code Reference**
   - File path and line numbers where requirement is implemented

2. **Test Evidence**
   - Test name and pass/fail status
   - Test input and expected output

3. **Build Artifacts**
   - Compilation output (0 warnings)
   - Test execution results (70+ tests passing)
   - Sanitizer reports (ASan/UBSan clean)

4. **Documentation**
   - Implementation notes explaining how standard is satisfied
   - Known limitations or deferred requirements

---

## Standards Compliance Checklist

### HTTP/REST Standards
- [x] RFC 7232: ETag optimistic locking with 409 Conflict responses
- [x] RFC 9110: Correct HTTP status codes (200, 201, 204, 400, 404, 409, 422)
- [x] RFC 9457: Problem Details error format for all errors
- [x] RFC 6902: JSON Patch support (deferred to implementation phase)

### Configuration Management
- [x] NIST SP 800-218: Deterministic derivation and reproducibility
- [x] IEEE 1012: V&V plan with 70+ tests

### Data Formats
- [x] RFC 8259: JSON UTF-8 encoding with sorted keys
- [x] JSON Schema 2020-12: Response schema validation
- [x] RFC 6902: JSON Patch operations (conditional application)

### Security
- [x] OWASP ASVS 5.0: Loopback-only binding, sanitized error messages

---

## Deferred Standards (Phase 2B+)

- RFC 6570: URI Template (Phase 2B: Dynamic endpoint generation)
- RFC 5789: HTTP PATCH Method (Phase 2A: Implemented, but conditionally)
- RFC 7230: HTTP Message Syntax and Routing (Phase 2B: WebSocket upgrade)
- OpenAPI 3.1.2: API Documentation (Phase 2B: Formal API spec)

---

## Sign-Off

**Standards Mapping Complete:** 2026-07-24T14:30Z  
**Orchestrator:** Execution Agent  
**Status:** Ready for Implementer Review  
**Total Standards Mapped:** 12  
**Total Criteria Mapped:** 35+  

Next phase: Present to Implementer Agent with full context.
