# FHSS dashboard Phase 1/2 operator

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
  --phase 2 --build-dir build-ninja/ninja-debug \
  --output-dir /path/to/new/operator-output
.venv-dashboard-contracts/bin/python \
  examples/DSP/dashboard/operator/fhss_dashboard_operator.py verify \
  --phase 2 \
  --output-dir /path/to/new/operator-output
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
the operator ownership marker.

Phase 2 validates strong-ETag JSON Patch concurrency, validation-only
immutability, atomic patch failure, independent preamble active-set derivation,
truth-free binary-IQ receiver projection, and the absence of Phase 3 runtime
controls. Reports contain SHA-256 hashes of the authoritative, validation,
applied, and receiver-graph payloads. Use `--phase 1` to retain the Phase 1
transport/read-only evidence lane.

The production server applies separate timing limits: activity on a partial
request resets the idle timer, `read_timeout` is an absolute header/body read
budget, `write_timeout` bounds response writing, and `total_request_timeout`
bounds the complete request. Deadline failures use RFC 9457 status 408 bodies.
