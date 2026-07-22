# Phase 8 dashboard extraction recommendation

Do not extract a generic dashboard framework yet. The FHSS vertical slice has
proven useful seams—HTTP server, versioned document store, runtime owner,
atomic snapshots, and ordered events—but one domain is insufficient evidence
that their present APIs are domain-neutral.

Candidate future boundaries are: a loopback HTTP/WebSocket transport with
budgets and origin policy; a versioned JSON document store with Patch/Pointer
concurrency; an executor lifecycle owner; immutable generation/run snapshots;
and an epoch/sequence event publisher. FHSS configuration projection, receiver
observations, jobs, SigMF bundles, schemas, and UI remain domain components.

Extraction should wait for a second dashboard with independently expressed
requirements and contract tests. Prerequisites are stable ownership and error
models, domain-free schemas, explicit cancellation/backpressure contracts,
host portability evidence, accessibility primitives, security review, and
benchmarks showing no lifecycle or streaming regression. Primary risks are a
premature abstraction that leaks FHSS semantics, weakened safety bounds,
schema/version coupling, and duplicated runtime ownership. This document is a
proposal only; Phase 8 performs no generic extraction.
