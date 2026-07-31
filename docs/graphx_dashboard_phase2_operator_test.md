# GraphX dashboard Phase 2 operator test

This procedure qualifies the native generic dashboard hierarchy layer from a
fresh clone. It covers source-tree and installed operation, nested generic
groups, the authored FHSS detector group, invalid and over-bound metadata, and
the execution-isolation contract. Docker and the legacy FHSS dashboard
container are not used.

## 1. Fresh-clone native build and install

Use the normal GraphX native prerequisites documented by the repository.
Node.js is not required to configure, compile, install, launch, or operate the
checked-in dashboard.

```bash
git clone <graphx-repository-url> GraphX
cd GraphX
cmake --preset ninja-debug
cmake --build --preset build-debug -j 8
ctest --test-dir build-ninja/ninja-debug --output-on-failure
cmake --install build-ninja/ninja-debug \
  --prefix "$PWD/build-ninja/ninja-debug/phase2-install"
```

The native build must use the repository C++26 mode and warnings-as-errors
policy. For an ASan/UBSan qualification on Darwin, run with
`ASAN_OPTIONS=detect_leaks=0`: Apple’s sanitizer runtime does not support
LeakSanitizer, so `detect_leaks=1` is an explicitly unsupported facet rather
than a passing leak check. The install must contain exactly one dashboard
entry point and its self-hosted asset inventory:

```text
build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/index.html
build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/assets/graphx-dashboard.css
build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/assets/graphx-dashboard.js
```

Verify source/install byte identity:

```bash
shasum -a 256 \
  libgraph/resources/web/index.html \
  libgraph/resources/web/assets/graphx-dashboard.css \
  libgraph/resources/web/assets/graphx-dashboard.js \
  build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/index.html \
  build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/assets/graphx-dashboard.css \
  build-ninja/ninja-debug/phase2-install/share/graphx/dashboard/assets/graphx-dashboard.js
```

The corresponding source and installed hashes must match. In an incomplete
installed resource root, a missing `index.html` makes `/` return `503
ui_unavailable`, while a missing JS or CSS child returns `404`. Neither case
may borrow a missing file from the source tree.

## 2. Source-tree and installed launch

Launch the generic fixture from the source build:

```bash
./build-ninja/ninja-debug/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --port 8080
```

Launch the same document from the installed executable on another port:

```bash
./build-ninja/ninja-debug/phase2-install/bin/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --port 8081
```

Open `http://127.0.0.1:8080/` and `http://127.0.0.1:8081/` in Firefox. Merely
loading either page must leave the executor `CONFIGURED`. There must be one
initial `GET /api/v1/graph`, no group/topology mutation request, and no
execution request.

Before browser inspection, record the authoritative counts:

```bash
jq '{nodes:(.nodes|length), edges:(.edges|length),
     groups:(.presentation.groups|length)}' \
  libgraph/test/config/topologies/generic_grouped_split_merge.json
```

The result is 9 nodes, 9 edges, and 4 authored groups.

## 3. Generic nested-group acceptance

The initial grouped view has the `parallel-stage` group collapsed by default.
Record these exact values:

| Item | Expected |
|---|---:|
| Authoritative nodes / edges | 9 / 9 |
| Visible authoritative nodes | 7 |
| Visible presentation groups | 4 |
| Hidden authoritative nodes / edges | 2 / 4 |
| Visible edges, including bundles | 7 |
| Bundles | 2 |
| Members in each bundle | 2 |

The group forest is `pipeline` with children `inputs`, `outputs`, and
`parallel-stage`. The four generic authored layout modes are `layered`,
`grid`, `fanin`, and `fanout`, respectively.

Perform the following in both the source and installed pages:

1. Select `interior_1`, collapse `parallel-stage`, and confirm the
   authoritative node selection remains in the inspector while the collapsed
   group indicates that it contains the selection.
2. Select each bundle. Its inspector must list both authoritative member
   edges with exact node IDs and named port values. Choose a member and confirm
   that exact edge becomes the authoritative selection.
3. Expand `parallel-stage`. Confirm 9 authoritative nodes, 4 presentation
   groups, 9 exact authoritative edges, and no bundles.
4. Repeat collapse/expand twice. IDs, counts, member order, and selection must
   remain stable.
5. Isolate `parallel-stage`; confirm breadcrumbs show
   `Processing pipeline / Parallel stage`. Use Parent, then All topology.
6. Switch to **Raw topology**. Confirm exactly 9 rendered nodes and 9 rendered
   edges, no group nodes or bundles, and exact named ports from the source
   JSON. Switch back to **Grouped topology**.
7. Confirm the labeled minimap is present and tracks grouped, expanded,
   isolated, and raw views.
8. Use Tab plus Enter or Space for mode, collapse, isolation, breadcrumb,
   node, edge, and bundle actions. Focus must remain visible.
9. Edit `source_1.message_count`, save, and confirm the only mutation is
   `PATCH /api/v1/nodes/source_1` with body `{"node_config":...}` followed by
   one graph refresh. The top-level `presentation` object must remain
   byte-for-byte equivalent as JSON, and local collapse/isolation state must
   not appear in the PATCH.

The independent oracle is
`libgraph/test/config/topologies/generic_grouped_split_merge.oracle.json`.
For a fully collapsed `pipeline`, all 9 edges are internal authoritative
membership and no self-bundle is rendered.

## 4. FHSS authored-group acceptance

Stop the generic processes before reusing their ports. Launch the source and
installed executables with:

```bash
./build-ninja/ninja-debug/graphx-dashboard \
  --graph libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --port 8082

./build-ninja/ninja-debug/phase2-install/bin/graphx-dashboard \
  --graph libdsp/config/fhss_phase2_binary_iq_receiver.json \
  --port 8083
```

The source JSON is authoritative. Check it directly:

```bash
jq '{nodes:(.nodes|length), edges:(.edges|length),
     group_id:.presentation.groups[0].id,
     members:(.presentation.groups[0].members|length)}' \
  libdsp/config/fhss_phase2_binary_iq_receiver.json
```

Expected raw topology is 75 nodes and 137 edges. The one
`detector-bank` group contains exactly `detector_0` through `detector_63` and
is collapsed by default.

In each page, confirm the collapsed view has 11 visible authoritative nodes,
1 presentation group, 64 hidden nodes, 128 hidden authoritative edges, 11
visible edges, and 2 bundles of 64 members each. Inspect at least these exact
tuples:

| `k` | Inbound | Outbound |
|---:|---|---|
| 0 | `channelizer` index 0 → `detector_0` index 0 | `detector_0` index 0 → `merge` index 1 |
| 31 | `channelizer` index 31 → `detector_31` index 0 | `detector_31` index 0 → `merge` index 32 |
| 63 | `channelizer` index 63 → `detector_63` index 0 | `detector_63` index 0 → `merge` index 64 |

Expand the group and confirm 75 visible authoritative nodes, 1 expanded
presentation group, 137 authoritative edges, and zero bundles. Raw mode must
render exactly 75 nodes and 137 edges. Collapse again and confirm the same two
64-member bundle identities and member ordering.

Generator reproducibility is checked by hashing the topology, running:

```bash
python3 examples/DSP/tools/generate_fhss_phase2_topology.py
```

and hashing it again. The SHA-256 value must not change.

## 5. Invalid, cyclic, unknown-member, and bound behavior

Repeat a source-tree launch with each fixture:

```text
libgraph/test/config/topologies/generic_grouped_invalid_overlap.json
libgraph/test/config/topologies/generic_grouped_invalid_cycle.json
libgraph/test/config/topologies/generic_grouped_invalid_unknown_member.json
libgraph/test/config/topologies/generic_grouped_over_group_bound.json
```

Expected diagnostics, in the same order, are
`overlapping_group_member`, `group_parent_cycle`, `unknown_group_member`, and
`group_count_bound`. Each page must show one deterministic diagnostic, no
partial group or bundle, and an expandable **Exact authoritative raw
topology** section. The raw counts must still equal the source JSON.

For each invalid page, record executor state before and after page load:

```bash
curl -sS http://127.0.0.1:PORT/api/v1/execution/state
```

State remains `CONFIGURED`, revision is unchanged, and no PATCH or execution
request is issued. Valid, absent, and invalid `presentation` metadata are
execution-neutral; only the frontend presentation adapter interprets groups.
There is no group REST endpoint.

## 6. Screenshots, console, network, and read-only evidence

For generic and FHSS graphs in both source and installed modes, record:

- graph path, launch mode, port, Firefox version, and screenshot filename;
- authoritative, visible, hidden, group, edge, and bundle counts;
- inspected bundle member counts and exact sample port tuples;
- executor state and coordinator revision before and after page load;
- console error count; and
- all unexpected network requests or mutations.

Capture full-page screenshots of the initial collapsed view, expanded view,
raw view, isolation/breadcrumb state, bundle inspector, and each invalid
diagnostic. Open Firefox developer tools before reload, preserve the Console
and Network logs, and expect zero console errors.

The canvas is read-only. Verify there is no add, delete, rename, retype,
connect, reconnect, edge-delete, group-edit, or save-layout operation.
Collapse, expansion, layout, minimap, selection, raw mode, and isolation must
not change graph JSON, revision, dirty state, or executor state.

## 7. Maintainer frontend and browser checks

Maintainers rebuilding the checked-in assets use the pinned lockfile:

```bash
cd libgraph/web
npm ci --ignore-scripts
npm run typecheck
npm test
npm run build
npm run check:assets
cd ../..
```

Run the real Firefox hierarchy harness for each source and installed scenario:

```bash
cd libgraph/web
npm run test:browser:phase2 -- \
  --dashboard ../../build-ninja/ninja-debug/graphx-dashboard \
  --graph ../../libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --port 8090 --scenario generic \
  --screenshot ../../build-ninja/ninja-debug/phase2-evidence/source-generic.png
```

Repeat with `--scenario fhss` and the FHSS graph, then with
`--scenario invalid` and an invalid fixture. Repeat the critical generic and
FHSS cases using the installed executable. Use a unique loopback port for
every process.

## 8. Troubleshooting

- **Grouping is rejected:** read the stable diagnostic and expand the exact
  raw topology. Correct the authored metadata; the dashboard will not infer or
  partially apply groups.
- **A bound is reported:** keep the raw authoritative document for
  inspection. Do not raise a limit or edit topology merely to force layout.
- **A bundle count differs:** compare its complete sorted member list against
  source edge tuples. Internal edges must not appear as self-bundles.
- **Selection seems missing:** inspect the collapsed ancestor marker and
  shared inspector, then expand or enter Raw topology.
- **The minimap or compound layout is empty:** inspect the visible diagnostic,
  browser console, and asset hashes. Bound failures intentionally withhold
  expensive canvas layout while preserving semantic raw inspection.
- **The installed entry point `/` returns 503 `ui_unavailable`:** reinstall
  `index.html` into the prefix-local dashboard resource directory.
- **An installed JS/CSS child asset returns 404:** reinstall the missing child
  into that same prefix and verify the three-file inventory. Neither case
  borrows a missing file from source or another install root.
- **Topology fetch fails:** check
  `curl -sS http://127.0.0.1:PORT/api/v1/graph`.
- **Port binding fails:** select an unused port in `1..65535`; the server binds
  IPv4 loopback only.
- **Init reports a missing provider:** topology inspection remains available.
  Provider loading begins only after an explicit Init command.

This phase adds presentation-only hierarchy, deterministic compound layout,
bundles, navigation, and minimap behavior. It does not add semantic domain
views, runtime metric overlays, command overlays, or another management
architecture.
