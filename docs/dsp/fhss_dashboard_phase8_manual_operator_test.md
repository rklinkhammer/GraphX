# FHSS dashboard Phase 8 operator qualification

## Fresh-clone prerequisite

Run this qualification only from a newly downloaded clone of
`https://github.com/rklinkhammer/GraphX.git`. Record `git rev-parse HEAD` and
require a clean `git status --short` before building. Do not reuse a developer
checkout, build tree, installed dependencies, CMake cache, or prior evidence.
Required packages must already be installed on the host; stop if one is
missing rather than provisioning it under a temporary path.

```sh
export GRAPHX_OPERATOR_ROOT="$PWD/.graphx-operator"
export GRAPHX_CONTRACT_PYTHON="$PWD/.venv-dashboard-contracts/bin/python"
export GRAPHX_AXE_ROOT="$PWD/.host-packages/axe-core-4.12.1"
mkdir -p "$GRAPHX_OPERATOR_ROOT"
test -x "$GRAPHX_CONTRACT_PYTHON"
test -f "$GRAPHX_AXE_ROOT/axe-core-4.12.1.tgz"
test -f "$GRAPHX_AXE_ROOT/package/axe.min.js"
```

## Qualification boundary

Dashboard scope: FHSS-specific  
Input evidence: synthetic IQ only  
HWIL/conducted/OTA evidence: unavailable and deferred  
Production RF qualification: NOT QUALIFIED

This procedure qualifies the loopback-only local operator profile. It neither
uses nor claims hardware, conducted-RF, over-the-air, live-RF, or production-RF
evidence. Receiver execution receives binary IQ and receiver-minimal
configuration only; synthetic generator truth remains in separate evaluator
artifacts.

## Clean installed-tree workflow

From the repository root, use a new prefix and output directory. The configured
contract interpreter must contain the exact locked dependencies from
`examples/DSP/dashboard/api/requirements-contracts.lock`.

```sh
cmake --install build-ninja/ninja-debug \
  --prefix ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install
cd ${GRAPHX_OPERATOR_ROOT}
"$GRAPHX_CONTRACT_PYTHON" \
  ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py \
  exercise --phase 8 \
  --build-dir ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install \
  --output-dir ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-evidence
"$GRAPHX_CONTRACT_PYTHON" \
  ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py \
  verify --phase 8 \
  --output-dir ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-evidence
```

Expected output is `PASS`; `phase8-report.json` must say `PASS` and
`final_verified`. The workflow generates deterministic synthetic IQ with the
canonical generator, drives lifecycle and reconnect behavior, exports both
reference and copied-IQ bundles, imports and replays them, compares
receiver-derived semantic hashes, drives maintained Firefox with WebDriver
BiDi, runs four production-input fuzz surfaces, and executes a bounded soak
covering 64 HTTP requests, eight WebSocket reconnects, two deterministic
build/start/stop lifecycle cycles, two export/validate/replay cycles, clean
shutdown, and mandatory RSS/thread/handle measurements. It records browser,
soak, fuzz/security, bundle, and failure
evidence by hash. Any `PARTIAL`, missing evidence, failed check, source-tree
path in the installed command list, surviving owned process, or hash mismatch
is failure.

Run focused and complete enabled regressions, sanitizer, and concurrency lanes
with CTest `--output-junit`. Create one
`graphx.fhss.dashboard.phase8_lane_evidence.v1` manifest per lane, naming the
exact profile and command and binding the JUnit file by SHA-256. The completion
tool derives counts from JUnit and rejects failed, skipped, empty, mismatched,
or wrongly profiled lanes. It accepts no caller-provided PASS/count flags.

After a human has completed every manual check below, copy
`phase8-manual-wcag.template.json` to the evidence directory and replace every
placeholder with observed data. The shipped template is deliberately invalid
and failing; automation and agents must not fill or attest it.

For every manual item, list only relative evidence paths beneath the directory
containing the manual record and provide the exact SHA-256 for each path in that
item's `evidence_sha256` object. Evidence must be a genuine, existing regular
file; absolute paths, traversal, symlinks, directories, the manual record itself,
duplicate paths within an item, and changed or hash-mismatched artifacts are
rejected. One genuine artifact may support multiple related items when each
item independently declares its hash.

Create the final report only after all source, installed, lane, browser,
security/fuzz, complete-soak, schema/package inventory, and human evidence is
available:

```sh
"$GRAPHX_CONTRACT_PYTHON" \
  ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install/share/graphx/fhss-dashboard/operator/phase8_completion_report.py \
  --source-operator-report SOURCE/phase8-report.json \
  --installed-operator-report INSTALLED/phase8-report.json \
  --browser-evidence SOURCE/phase8-browser-accessibility.json \
  --installed-browser-evidence INSTALLED/phase8-browser-accessibility.json \
  --source-accessibility-engine-evidence EVIDENCE/source-axe.json \
  --installed-accessibility-engine-evidence EVIDENCE/installed-axe.json \
  --security-fuzz-evidence SOURCE/phase8-fuzz-security.json \
  --installed-security-fuzz-evidence INSTALLED/phase8-fuzz-security.json \
  --soak-evidence SOURCE/phase8-soak.json \
  --installed-soak-evidence INSTALLED/phase8-soak.json \
  --source-schema-inventory SOURCE/phase8-schema-inventory.json \
  --installed-schema-inventory INSTALLED/phase8-schema-inventory.json \
  --source-package-manifest SOURCE/phase8-package-manifest.json \
  --installed-package-manifest INSTALLED/phase8-package-manifest.json \
  --manual-wcag-evidence EVIDENCE/phase8-manual-wcag.json \
  --focused-evidence EVIDENCE/focused.json \
  --full-evidence EVIDENCE/full.json \
  --sanitizer-evidence EVIDENCE/sanitizer.json \
  --concurrency-evidence EVIDENCE/concurrency.json \
  --signed-off-by OPERATOR_ID \
  --output ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-evidence/phase8-completion.json
```

The tool authoritatively validates both operator reports and all evidence
schemas, derives JUnit counts, validates every required result/profile, checks
source/install package and schema identity, verifies every retained fuzz and
manual evidence file, and hashes every input into the signed report. It refuses
missing, failing, skipped, stale, divergent, malformed, or hash-mismatched
evidence and writes a detached SHA-256 file.

There is intentionally no completed human record or recognized accessibility-
engine report in the repository. The reviewed `axe-core@4.12.1` distribution
must already be installed on the host at `GRAPHX_AXE_ROOT`. If it is
missing, stop and have it installed before qualification:

```sh
shasum -a 256 \
  "$GRAPHX_AXE_ROOT/axe-core-4.12.1.tgz" \
  "$GRAPHX_AXE_ROOT/package/axe.min.js" \
  "$GRAPHX_AXE_ROOT/package/LICENSE" \
  "$GRAPHX_AXE_ROOT/package/LICENSE-3RD-PARTY.txt"
```

Required SHA-256 values, in command order, are
`4341a01268b5ecbea826f3c7a7d1d69280a2cab3484c93e1bf4c9554460c6ca0`,
`66a8aaa95a8b044a7fd74a5435873bf04ff65a1ca75567c921b7509742085a14`,
`af175b9d96ee93c21a036152e1b905b0b95304d4ae8c2c921c7609100ba8df7e`,
and `4f8563870d0fca38bbc3e00b6f670cb7fa9f380ba9f26a7f7d1184a6b18b1653`.
The pinned npm integrity is
`sha512-s7iGf5GaVMxEG0ENN9x+xTr7GFZCb1ZP/1uATUpCEK2X78nDB3RwbtFCo9pGAf9ru+VwoQ464DkaLEeRM08wJA==`.

With each independently launched source and installed dashboard URL, run the
corresponding installed-or-source `phase8_axe_scan.py`:

```sh
"$GRAPHX_CONTRACT_PYTHON" \
  PATH_TO_OPERATOR/phase8_axe_scan.py \
  --dashboard-url http://127.0.0.1:PORT \
  --axe-tarball "$GRAPHX_AXE_ROOT/axe-core-4.12.1.tgz" \
  --axe-package-root "$GRAPHX_AXE_ROOT/package" \
  --output-dir EVIDENCE --label source
```

Repeat with the installed operator and `--label installed`. The runner verifies
the distribution and license hashes before loading `axe.min.js`, uses maintained
Firefox WebDriver BiDi, requests the pinned WCAG 2.0/2.1/2.2 A/AA and best-
practice tags, and saves the untouched raw `axe.run` result separately from its
summary. The BiDi evidence client uses a finite 4 MiB receive limit: this was
reviewed against the observed 1,295,262-byte axe-core result frame and leaves
bounded headroom for the full unmodified report. This qualification-only limit
does not alter the dashboard's 256 KiB WebSocket message limits. If a future
BiDi result exceeds the reviewed allowance, close code 1009 produces an
explicit size-limit diagnostic instead of an unexplained transport failure.
The completion gate re-parses the raw tool identity, version, target,
tags, violations, and impacts; caller-provided counts cannot override it.
The wrapper and final completion evidence preserve raw-derived counts and exact
rule identifiers, impacts, node counts, and targets for both `violations` and
`incomplete`. Critical or serious incomplete findings block completion unless
`axe_incomplete_resolutions` contains one exact resolution for each finding.
That resolution must name the source or installed tree, reproduce the finding
identity exactly, cite a passing manual item with verified evidence, record a
`PASS` result, and include meaningful human adjudication notes. Duplicate,
partial, relabelled, or fabricated resolutions fail closed. Moderate and minor
incomplete findings do not require this override but remain retained in the
completion report for operator review.
The built-in browser contract audit is useful automation but is not represented
as an axe/WCAG-engine scan.
Phase 8 final sign-off remains **BLOCKED pending those source/installed engine
reports and a human operator's execution and attestation** of the following
checklist.

## Manual WCAG 2.2 AA record

Record tester, UTC time, Firefox version, host, viewport, result, evidence path,
and evidence SHA-256 for every item. Automated checks complement rather than
replace this list. Do not create or reuse placeholder evidence merely to satisfy
the schema; the record is a human attestation to artifacts actually observed.

- Keyboard only: the skip link appears on focus; every control is reachable and
  operable; focus never becomes trapped or lost.
- Tabs: Left/Right/Up/Down, Home, and End move and select the correct tab;
  exactly one tab is in the tab sequence.
- Focus: the violet three-pixel indicator is visible on every interactive
  element and dynamic refresh never moves focus.
- Names and structure: page language/title, one `h1`, landmarks, heading order,
  labels, control groups, and tab/panel relationships are announced correctly.
- Status and errors: transport status is a polite live region; command and
  validation failures remain visible text, identify the failed operation, and
  do not rely on color.
- Contrast/non-color cues: text and focus meet AA; badges include state text;
  expected, observed, and comparison evidence use both text labels and color.
- Reduced motion: with the OS/browser preference enabled, no meaningful motion
  or smooth scrolling remains.
- Zoom/reflow: at 200% zoom and a 320 CSS-pixel viewport, content reflows with
  no page-level horizontal scroll or loss of controls; intentionally tabular
  data may scroll inside its labeled region.
- Visualization alternatives: spectrum, heatmap, timeline, and receiver result
  values remain available as text or table semantics.

No serious or critical finding may be waived. Record lesser findings in the
completion evidence and resolve required failures before sign-off.

## Troubleshooting and cleanup

- Missing Firefox: set `GRAPHX_FIREFOX_BINARY` to a maintained Firefox binary.
- Missing validator modules: reprovision the locked contract environment; do
  not fall back to the subset validator.
- Port failure: confirm loopback is available. Non-loopback substitution is
  forbidden.
- Resource-soak failure: inspect `phase8-soak.json`. On supported macOS, real
  libproc RSS/thread and `lsof` handle probes are mandatory. Load, eight
  reconnects, two rebuild/start/stop cycles, two export/validate/replays, and a
  clean child shutdown must all pass within their explicit bounds.
- Fuzz failure: inspect `phase8-fuzz-security.json` and
  `phase8-fuzz-corpus/`. Every surface must produce reachable production-parser
  responses (including a real WebSocket 101); transport failure cannot pass.
  Each response-coverage seed is persisted and hash-bound.
- Stale output: choose a new empty directory. The operator intentionally
  refuses pre-existing nonempty evidence.

Cleanup only operator-owned output:

```sh
"$GRAPHX_CONTRACT_PYTHON" \
  ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-install/share/graphx/fhss-dashboard/operator/fhss_dashboard_operator.py \
  cleanup --phase 8 --output-dir ${GRAPHX_OPERATOR_ROOT}/graphx-dashboard-phase8-evidence
```
