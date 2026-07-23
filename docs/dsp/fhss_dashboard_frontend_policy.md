# FHSS Dashboard Frontend Build and Qualification Policy

This policy is normative for the dashboard-modernization initiative. `V2` is a
project label only. There is one dashboard at `/` and one application API
namespace, `/api/v1/fhss`.

## Host toolchain and dependency lock

The supported build-time range is recorded by
`examples/DSP/dashboard/frontend/package.json`, `package-lock.json`, and
`toolchain.json`. Node.js and npm are host-installed tools resolved from
`PATH`. GraphX accepts Node 24 through 26 and npm 11, records the versions used
by the build, and rejects unsupported major versions. Exact direct frontend
dependency versions and every resolved transitive package remain
integrity-bound in the lockfile. `THIRD_PARTY_NOTICES.md` records the reviewed
license inventory. `node_modules` is never a repository input. The compiled
Phase 1 `dashboard/dist` inventory is the sole source-tree frontend; the
prototype is not retained as a fallback.

GraphX does not download or provision Node.js, npm, or another package manager,
does not select executables from `/tmp`, `/private/tmp`, or another ephemeral
toolchain directory, and does not automatically install frontend dependencies
during CMake configuration or build. If a supported host tool or the locked
frontend dependencies are missing, configuration stops and asks the operator
to install them on the host or in the source workspace. There is no
unsupported-version override.

After installing the supported host tools, an operator installs the
repository-locked frontend dependencies explicitly:

```sh
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts --offline
```

Populate the controlled cache only in an authorized dependency-acquisition
environment. Dependency installation uses `npm ci`, never lockfile mutation.
Dashboard-disabled configuration and builds must not invoke Node, npm, or a
network. Dashboard-enabled CMake builds verify the host toolchain and installed
locked dependencies, then run only the production build. The deployed target
does not require Node.js.

## Release asset policy

- Each served file is at most 4 MiB, matching the server response bound.
- The complete installed frontend is at most 16 MiB.
- Production bundles are split before either bound is reached.
- HTML, scripts, styles, fonts, and workers are self-hosted; CDNs and runtime
  dependency downloads are forbidden.
- Release source maps are forbidden. An explicitly configured debug-only
  inventory may include them, but they are never installed in a release.
- CSP may not be weakened incidentally. A worker must be a self-hosted external
  file; adding `blob:` or another source requires a separately reviewed change.
- Required content types are HTML `text/html; charset=utf-8`, JavaScript and
  workers `text/javascript; charset=utf-8`, CSS `text/css; charset=utf-8`, JSON
  `application/json`, WOFF `font/woff`, WOFF2 `font/woff2`, TrueType
  `font/ttf`, and OpenType `font/otf`. Source maps, when permitted in a debug
  build, use `application/json`.

The recursive inventory rejects symlinks, special and unreadable files, path
escape, duplicate normalized paths, missing entrypoints, oversized files,
oversized totals, and release source maps. Source and clean installed frontend
inventories must agree exactly. Phase 0 directly checks the self-hosted/no-CDN
posture; detailed dynamic-code, worker, vulnerability, fuzz, and adversarial
analysis follows the security maturity roadmap.

## Identity boundary

Configuration topology uses stable node IDs from the graph document. An edge
identity is the canonical tuple
`source-node-id:source-port->target-node-id:target-port`; array positions are
not identities. This becomes the future visual identity. Runtime node, metric,
and diagnostic correlation may reference that identity only after Phase 3
defines and tests the mapping; Phase 0 enables no live overlays.

## Required gates

Every manual operator qualification starts from a newly downloaded fresh clone
of the repository and a new build directory. The operator records the clone
URL, revision, host Node/npm versions, and configuration command. Qualification
must not reuse another checkout's `node_modules`, compiled frontend output,
CMake cache, install prefix, or generated test artifacts. Fast developer
reruns may use an existing checkout, but they are not clean-build operator
evidence.

The canonical cross-host operator environment is
`containers/dashboard-operator`. Docker with Compose must already be installed
on the host. The versioned image owns its Linux, C++26, Node/npm, Python
contract, browser-support, and frontend dependencies; image construction is an
explicit operator action, never a CMake side effect. Docker publishes the
dashboard only on host loopback and stores generated evidence beneath the fresh
clone's `.graphx-operator` directory. Native macOS Metal qualification remains
a separate host lane.

Browser-operator scenarios written against the retired prototype DOM and its
standalone transport module remain visible as disabled CTest entries, but are
not evidence for the compiled application. Phase 1 instead qualifies the typed
domain, component, API-boundary, and transport tests; exact source/install
asset inventories; loopback smoke test; and external manual procedure. A later
phase must re-author each browser scenario against the compiled component
semantics before enabling it again.

Phase 0 runs authoritative contract/schema validation, its focused frontend
and C++ dashboard tests, source and clean-installed smoke checks, the
dashboard-disabled build, and `git diff --check`. Later phases add tests in
proportion to the surfaces they change. Full inherited operator/regression,
security, sanitizer, concurrency, soak, and human accessibility campaigns are
integration, roadmap, or release-candidate gates unless a focused failure
indicates shared-runtime risk.

After a later material UI change, a human reviews keyboard operation, focus,
motion, and 320 CSS-pixel reflow. The release candidate additionally requires complete
human WCAG evidence, reconnect and soak evidence, sanitizer/concurrency lanes,
clean-install qualification, and full enabled regressions. Automation must
leave missing human evidence fail-closed.

Qualification is synthetic-only. HWIL, conducted RF, OTA, live RF, and
production-RF qualification are unavailable and must not be claimed.
