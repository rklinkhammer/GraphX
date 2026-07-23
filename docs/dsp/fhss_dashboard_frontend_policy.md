# FHSS Dashboard Frontend Build and Qualification Policy

This policy is normative for the dashboard-modernization initiative. `V2` is a
project label only. There is one dashboard at `/` and one application API
namespace, `/api/v1/fhss`.

## Toolchain lock

The selected build-time baseline is recorded by
`examples/DSP/dashboard/frontend/package.json`, `package-lock.json`, and
`toolchain.json`. Exact direct versions and every resolved transitive package
must be integrity-bound in the lockfile. `THIRD_PARTY_NOTICES.md` records the
reviewed license inventory. `node_modules` is never a repository input. The
compiled Phase 1 `dashboard/dist` inventory is the sole source-tree frontend;
the prototype is not retained as a fallback.

Provision from a controlled cache with:

```sh
cd examples/DSP/dashboard/frontend
npm ci --ignore-scripts --offline
```

CMake reads `toolchain.json` and fails configuration unless the executables
selected by `GRAPHX_DASHBOARD_NODE_EXECUTABLE` and
`GRAPHX_DASHBOARD_NPM_CLI` report the exact pinned versions. The explicitly
named `GRAPHX_DASHBOARD_ALLOW_UNPINNED_TOOLCHAIN` option is for local developer
experimentation only. It writes a mismatch record and is never acceptable as
release or installed-tree qualification evidence.

Populate the controlled cache only in an authorized dependency-acquisition
environment. CI and ordinary builds use `npm ci`, never lockfile mutation.
Dashboard-disabled configuration and builds must not invoke Node, npm, or a
network. Dashboard-enabled CMake builds invoke the frozen offline installation
and production build as a build-time-only step. The deployed target does not
require Node.js.

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
