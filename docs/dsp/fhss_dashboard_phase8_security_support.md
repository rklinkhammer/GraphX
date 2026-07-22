# FHSS dashboard Phase 8 security and support policy

## Supported profile

The supported application is the FHSS-specific dashboard built as C++26 and
served by `graphx-dsp-fhss-demo` on `127.0.0.1` or `::1`. Qualification covers
macOS with maintained Firefox WebDriver BiDi in the repository's available host
profile. Other hosts/browsers are unqualified until the same installed-tree,
browser, sanitizer, concurrency, soak, and regression evidence passes there.

The API major is `/api/v1`. Additive optional response fields are compatible;
removing or changing a documented field, status, method, media type, or semantic
meaning requires a new major version. Every JSON object is governed by the
pinned OpenAPI document or a JSON Schema in `dashboard/api/schemas`. The schema
inventory is exactly the sorted `*.schema.json` set validated and hashed by the
operator plus the operator-report, Phase 8 completion-report, lane-evidence,
manual-WCAG, and accessibility-engine schemas. Source and installed inventories meta-schema validate
and hash every schema and must match exactly. New
schemas must be installed, documented, authoritatively validated, and hashed.
Recognized accessibility automation is pinned to npm `axe-core@4.12.1`; the
completion gate verifies the npm integrity, tarball, `axe.min.js`, MPL-2.0
license, third-party license, raw axe tool/version identity, requested WCAG
tags, and derived violation-impact counts. Raw output and the summary wrapper
remain separate hash-bound evidence. Both violations and incomplete results keep
their raw-derived rule identifiers, impacts, node counts, and DOM targets in the
wrapper and completion report. Critical or serious incomplete findings fail
closed unless an exact, unique human resolution binds the finding to a passing
manual item, hash-verified genuine evidence, a `PASS` adjudication, and meaningful
notes; moderate and minor incomplete findings remain retained for review.
Manual artifacts must be confined relative non-symlink regular files whose
declared SHA-256 values match. Unpinned, malformed, count-disagreeing, relabelled,
missing, escaped, self-referential, or mutated records fail closed.

## ASVS-informed local threat review

| Surface | Threat | Enforced local control | Residual severity |
|---|---|---|---|
| HTTP/JSON | ambiguity, truncation, oversize, slow client | strict framing/media type, byte/deadline limits, RFC 9457 failure | low |
| Pointer/Patch | escape, root/array confusion, stale write | RFC 6901 normalization, atomic RFC 6902, strong If-Match | low |
| WebSocket | cross-origin, malformed/fragment flood, replay gaps | exact loopback Host/Origin, frame/message/rate/lifetime bounds, resync | low |
| Browser DOM | script/markup injection | external values use `textContent`/safe attributes; no dynamic HTML sinks | low |
| Artifact/SigMF | traversal, sibling prefix, links, special files, tamper | approved-root canonical containment, no-follow checks, schema and hashes | low |
| Lifecycle/export | races, cancellation, partial commits, exhaustion | serialized ownership, bounded queues/timeouts, staging plus atomic rename | low |
| Shutdown/reconnect | orphan thread/process, stale state | joinable ownership, bounded stop, epoch/sequence reconciliation | low |

There are no unresolved blocking or high-severity findings in the local profile.
The production response-coverage smoke sends bounded mutations to the real raw
HTTP/JSON, Patch/Pointer, RFC 6455 decoder, and investigation/SigMF endpoints;
independent byte oracles prevent the production implementation being its only
oracle. Novel response-coverage seeds are persisted as bounded corpus bytes and
individually hash-bound. Unreachable targets and protocol failures cannot pass.
On the current Apple
CommandLineTools profile, `-fsanitize=fuzzer` is accepted but the fuzzer runtime
archive is unavailable; therefore compiler-instrumented libFuzzer is not
claimed. ASan/UBSan and TSan/concurrency remain separate required lanes.

Any non-loopback deployment is unsupported and requires a new threat model plus
authentication, authorization, TLS, strict origin policy, CSRF protection,
request/rate/resource limits, audit logging, secret management, secure headers,
deployment isolation, update policy, and independent penetration testing. Phase
8 adds no such mode and makes no production-readiness claim.
