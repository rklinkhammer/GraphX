# FHSS dashboard Phase 1/2/3/4/5 operator

Phase 4 manual browser/API validation is documented in
`docs/dsp/fhss_dashboard_phase4_manual_operator_test.md`.
Phase 5 message-job validation is documented in
`docs/dsp/fhss_dashboard_phase5_manual_operator_test.md`.

The operator exercises the production `graphx-dsp-fhss-demo` executable on an
ephemeral loopback port. All scenario data is synthetic. There is no HWIL,
conducted-RF, or OTA evidence, and this workflow does not qualify RF behavior.

## Provision authoritative validators

Plain system Python is not sufficient unless it already contains the locked
dependencies. From the repository root, create a dedicated environment:

```sh
python3 examples/DSP/dashboard/api/provision_contract_validators.py \
  --venv .venv-dashboard-contracts
```

The provisioner installs every version pinned in
`examples/DSP/dashboard/api/requirements-contracts.lock`, including
`openapi-spec-validator==0.9.0` and `jsonschema==4.26.0`. For an offline or
controlled build, pre-populate a reviewed wheelhouse and disable index access:

```sh
python3 examples/DSP/dashboard/api/provision_contract_validators.py \
  --venv .venv-dashboard-contracts --wheelhouse /path/to/wheelhouse
```

The lock pins the complete resolved environment. It does not contain wheel
hashes because wheels differ by supported Python/platform; security-sensitive
offline builds should use an access-controlled wheelhouse with independently
recorded artifact hashes.

Use that interpreter for both authoritative contract validation and operator
execution:

```sh
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/api/validate_contracts.py
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py exercise \
  --phase 4 --build-dir build-ninja/ninja-debug \
  --output-dir /path/to/new/operator-output
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 4 \
  --output-dir /path/to/new/operator-output
```

## Phase 1 walking-skeleton workflow

From the repository root, run the documented Phase 1 exercise with explicit
build and operator-owned output directories:

```sh
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py exercise \
  --phase 1 --build-dir build-ninja/ninja-debug \
  --output-dir /path/to/new/phase1-operator-output
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 1 --output-dir /path/to/new/phase1-operator-output
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py report \
  --phase 1 --output-dir /path/to/new/phase1-operator-output
```

For interactive inspection, replace `exercise` with `serve` and keep the same
arguments. When finished, terminate the server and remove only operator-owned
artifacts with:

```sh
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py cleanup \
  --phase 1 --output-dir /path/to/new/phase1-operator-output
```

Configure the CTest contract and operator lanes with the same interpreter:

```sh
cmake -S . -B build-ninja/ninja-debug -G Ninja \
  -DGRAPHX_BUILD_WEB_DASHBOARD=ON \
  -DGRAPHX_DASHBOARD_CONTRACT_PYTHON="$PWD/.venv-dashboard-contracts/bin/python"
```

The contract lane uses OpenAPI Spec Validator 0.9.0 for OpenAPI 3.1.2 semantic
validation and JSON Schema 4.26.0's `Draft202012Validator` for metaschema and
instance validation. Missing authoritative dependencies are a hard failure;
the repository's small pinned-subset audit helper is not an authoritative
standards validator.

`serve` keeps the dashboard available for manual browser inspection. `report`
prints the machine-readable report. `cleanup` deletes only artifacts carrying
the operator ownership marker. In Phase 4, `serve --case
clean|impaired|negative` loads a stable receiver-only case and persists its
served-state identity. `record-screenshot --case CASE --path FILE.png` binds a
complete PNG to that state; `verify --require-screenshots` requires all three
case hashes.

For Phase 4, `exercise` and the source/installed CTest lanes deliberately
produce `PARTIAL` / `partial_pre_browser`, not a completed pass. After genuine
clean, impaired, and negative browser captures are recorded, the report remains
`PARTIAL` / `captures_complete_unverified`. Only a successful
`verify --require-screenshots` promotes it to `PASS` / `final_verified` after
reverifying non-uniform image content and the
served URL, case, generation, run epoch, observation identity, state hash,
configuration hash, and IQ hash bindings.

Phase 2 validates strong-ETag JSON Patch concurrency, validation-only
immutability, atomic patch failure, independent preamble active-set derivation,
truth-free binary-IQ receiver projection, and the absence of Phase 3 runtime
controls. Reports contain SHA-256 hashes of the authoritative, validation,
applied, and receiver-graph payloads. Use `--phase 1` to retain the Phase 1
transport/read-only evidence lane.

Phase 3 generates architecture-conformant IQ with the production generator,
keeps IQ/SigMF separate from truth, removes schedule and truth before receiver
execution, and runs two transactional runtime generations to natural terminal
completion. Rebuild is synchronous (HTTP 200 after publication), start is
accepted asynchronously (HTTP 202), and stop is synchronous (HTTP 200 after a
real join). The operator also uses a longer third fixture to observe running
state and nonzero traffic before issuing Stop. Stop is bounded at five seconds;
a non-cooperative `Consume()` produces HTTP 504 while the runtime retains the
live thread instead of detaching it. The lane requires generation-attributed
traffic and safe rollback when a malformed receiver configuration fails to
rebuild.

Phase 4 adds separate expected-truth, receiver-observation, evaluator-
comparison, receiver-spectrum, provenance, and bounded-history contracts. The
clean case requires full independent timing/channel/decoded-value agreement and
terminal preamble/message assembly. The impaired case records seed 404, 750 Hz
CFO, 18 dB Eb/N0, and a deterministic receiver-measured delta from the clean
baseline. The negative case must complete with zero detections, no preamble
lock, and no message completion; malformed IQ must fail without a fabricated
pulse. Live receiver cases contain IQ and receiver configuration only—truth and
schedule files are removed before execution. This remains synthetic software
evidence: HWIL and production-RF qualification are unavailable.

For the clean replay, the operator copies the receiver's explicit 64-channel,
7.5 MHz-spaced IQ offset map into the generator-only schedule and adds one
6,500-sample pulse slot before every message. This causal warm-up makes the
first word independently decodable without exposing the map, message schedule,
or truth to the receiver. Runtime processing is bounded at 30 seconds, while
Stop keeps its separate five-second join bound. A processing timeout without a
graph completion signal is reported as `execution_failed` rather than
`execution_completed`.

The Overview schedule and timeline use the visualization contract's configured
message start plus the architecture's 6,500-sample pulse period; they never
derive decoder-like values. The receiver spectrum selector is bounded to
physical channels 0–63. It defaults to the first receiver-observed physical
channel. With no observed channel it is disabled, requests omit the channel,
and the API returns null `channel_index`, `no_candidate_detected`, and empty
bins; channel 0 is never a fallback.
Spectrum samples come from a bounded highest-energy receiver window, with
the capture's global sample anchor adjusted to that window; generator truth is
not consulted.

The observation contract keeps `terminal_result` (graph execution lifecycle)
separate from `receiver_message_result` (terminal receiver message status,
accepted flag, and decoded-pulse count). In the negative case the assembler is
truthfully available with its missing-preamble rejection, while the receiver
message result is not accepted and contains zero decoded pulses.

Phase 5 adds production FHSS job resources and complete-message Step,
Continue, Cancel, and Reset controls. It generates IQ with the canonical
architecture parser/generator, writes IQ, truth, SigMF metadata, receiver
configuration, and hashes separately, and gives receiver execution only the IQ
path and receiver-minimal graph. The operator validates idempotency conflict,
queued and active cancellation, timeout, reset, and dashboard-free replay after
truth deletion. Use `exercise --phase 5` and `verify --phase 5`; the Phase 5
manual procedure contains the browser checklist. All evidence is synthetic and
there is no HWIL lane.

The production server applies separate timing limits: activity on a partial
request resets the idle timer, `read_timeout` is an absolute header/body read
budget, `write_timeout` bounds response writing, and `total_request_timeout`
bounds the complete request. Deadline failures use RFC 9457 status 408 bodies.
