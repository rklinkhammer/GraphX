# GraphX dashboard Phase 1 operator test

This procedure qualifies the native, generic GraphX dashboard from a fresh
clone. Docker is not required. Do not use the legacy FHSS dashboard container:
the generic topology view is served only by `graphx-dashboard`.

## 1. Fresh-clone native build

Prerequisites are the normal GraphX native build dependencies documented by
the repository. Node.js is not required to configure, compile, install, launch,
or use the checked-in dashboard. Node.js is needed only by maintainers who
rebuild or test the checked-in frontend assets.

```bash
git clone <graphx-repository-url> GraphX
cd GraphX
cmake --preset ninja-debug
cmake --build --preset build-debug -j 8
```

The configure output must identify the repository C++26 mode
(`-std=c++2c` with the current AppleClang toolchain). The `graph` target treats
project warnings as errors. A successful ordinary build uses the checked-in
files below and does not run npm or download frontend packages:

```text
libgraph/resources/web/index.html
libgraph/resources/web/assets/graphx-dashboard.css
libgraph/resources/web/assets/graphx-dashboard.js
```

Install into a repository-local prefix:

```bash
cmake --install build-ninja/ninja-debug \
  --prefix "$PWD/build-ninja/ninja-debug/phase1-install"
find build-ninja/ninja-debug/phase1-install/share/graphx/dashboard \
  -type f -print | sort
```

Expected installed dashboard inventory:

```text
.../share/graphx/dashboard/assets/graphx-dashboard.css
.../share/graphx/dashboard/assets/graphx-dashboard.js
.../share/graphx/dashboard/index.html
```

The installed dashboard has no Node.js runtime dependency and makes no CDN or
other runtime code request.

## 2. Source-tree and installed launch

Use a different loopback port for each process:

```bash
./build-ninja/ninja-debug/graphx-dashboard \
  --graph libgraph/test/config/topologies/minimal_graph.json \
  --port 8080
```

In another terminal, launch the installed executable:

```bash
./build-ninja/ninja-debug/phase1-install/bin/graphx-dashboard \
  --graph libgraph/test/config/topologies/minimal_graph.json \
  --port 8081
```

Open `http://127.0.0.1:8080/` and `http://127.0.0.1:8081/`. Both pages must
show the same management header, execution controls, Topology and
Nodes & parameters views, inspector, and asset inventory. Initial executor
state must be `CONFIGURED`; merely loading either page must not initialize
providers, construct nodes, or execute the graph.

Confirm static resource status and MIME types:

```bash
curl -sS -D - -o /dev/null \
  http://127.0.0.1:8080/assets/graphx-dashboard.js
curl -sS -D - -o /dev/null \
  http://127.0.0.1:8080/assets/graphx-dashboard.css
curl -sS -D - -o /dev/null \
  http://127.0.0.1:8081/assets/graphx-dashboard.js
```

Expected MIME types are `application/javascript; charset=utf-8` and
`text/css; charset=utf-8`. A missing asset returns `404`; a non-GET method on
an asset returns `405`.

## 3. Representative graph matrix

Stop the current process with Ctrl-C before reusing its port. Launch each graph
with the same source-tree command, changing only `--graph` and `--port`.

| Graph | Path | Authoritative nodes | Authoritative edges | Exact sample endpoint |
|---|---|---:|---:|---|
| Minimal | `libgraph/test/config/topologies/minimal_graph.json` | 2 | 1 | `source_1` index `0` → `sink_1` index `0` |
| Complex network | `libgraph/test/config/topologies/complex_network.json` | 9 | 9 | `source_1` name `Data` → `merge_1` name `In0` |
| SAR fanout | `examples/SAR/config/sar_stripmap_fanout.json` | 21 | 23 | `fanout` index `0` → `split_tile0` index `0` |
| FHSS acceptance graph | `libdsp/config/fhss_phase2_binary_iq_receiver.json` | 75 | 137 | `channelizer` index `k` → `detector_k` index `0`; `detector_k` index `0` → `merge` index `k+1`, for `k=0..63` |

Record the source counts directly before opening the browser:

```bash
jq '{nodes:(.nodes|length), edges:(.edges|length)}' \
  libgraph/test/config/topologies/minimal_graph.json
jq '{nodes:(.nodes|length), edges:(.edges|length)}' \
  libgraph/test/config/topologies/complex_network.json
jq '{nodes:(.nodes|length), edges:(.edges|length)}' \
  examples/SAR/config/sar_stripmap_fanout.json
jq '{nodes:(.nodes|length), edges:(.edges|length)}' \
  libdsp/config/fhss_phase2_binary_iq_receiver.json
```

For each graph, record the `Authoritative inventory` text in the page and
count the rendered nodes/edges in browser developer tools:

```javascript
({
  renderedNodes: document.querySelectorAll(".react-flow__node").length,
  renderedEdges: document.querySelectorAll(".react-flow__edge").length,
  renderedHandles: document.querySelectorAll(".react-flow__handle").length
})
```

Rendered node and edge counts must equal the table above. Select the sample
edge and record the edge inspector's source node, source port kind/value,
target node, and target port kind/value. For the FHSS graph, sample at least
`k=0`, `k=31`, and `k=63`; the exact expected merge targets are ports `1`,
`32`, and `64`. These expectations come from the graph JSON, not a
dashboard-specific rule.

## 4. Interaction and refresh checks

Perform the following in both source-tree and installed minimal dashboards:

1. In Topology, use Zoom in and Zoom out and confirm the viewport scale
   changes.
2. Drag blank canvas space and confirm the view pans.
3. Choose Fit to view and confirm both nodes are visible.
4. Pan or zoom, then choose Reset layout. Confirm the same stable left-to-right
   node order and exact edge reappear.
5. Select `source_1` on the canvas. Confirm the node inspector shows ID, type,
   exact output port, and configuration.
6. Select the edge. Confirm all four endpoint fields show index `0`.
7. Use Tab to focus `Nodes & parameters` and press Enter. Select the
   `source_1` row. Confirm it opens the same node inspector state.
8. Use Tab to focus a canvas node or edge and press Enter or Space. Confirm
   selection is reflected in the same inspector.
9. Choose Edit parameters for `source_1`, change `message_count` to `25`, and
   save.
10. Confirm the browser sends one
    `PATCH /api/v1/nodes/source_1`, then refreshes with
    `GET /api/v1/graph`. The table, canvas, inspector, revision/dirty summary,
    and selected node must all reflect the authoritative refreshed document.
11. Confirm the executor remains `CONFIGURED`, coordinator revision advances,
    and `configuration dirty` is reported until the operator explicitly
    configures the executor.
12. Use Clear selection and confirm the inspector truthfully returns to its
    no-selection state.

The canvas must not offer add/delete/rename/retype node controls, connect,
reconnect, or delete edge gestures, or a save-layout operation. Dragging nodes
must not change their positions. Pan, zoom, fit, reset, and selection must not
issue a PATCH. The only permitted Phase 1 graph mutation is the existing
node-configuration PATCH.

## 5. Evidence to record

For every graph and both minimal launch modes, record:

- graph path and loopback port;
- authoritative and rendered node/edge counts;
- exact inspected sample endpoint tuple;
- executor state before and after page load;
- screenshot file name;
- browser name/version;
- browser console error count; and
- unexpected network requests, if any.

Capture one full-page screenshot in the Topology view after Fit to view and
one after node selection. For large graphs, also capture a zoomed view showing
exact port labels. Open browser developer tools before reloading, enable
Preserve log, and filter Console to errors. Expected console error count is
zero. In Network, the initial topology load must contain one
`GET /api/v1/graph`; a successful node edit adds exactly one authoritative
graph refresh.

## 6. Maintainer frontend checks

These checks are for maintainers changing frontend sources. Use the compatible
Node.js/npm already installed on the host; do not impose a Node patch version
and do not install Node into the dashboard output.

```bash
cd libgraph/web
npm ci --ignore-scripts
npm run typecheck
npm test
npm run build
npm run check:assets
cd ../..
```

`check:assets` rebuilds from the pinned lockfile and fails if the two
checked-in assets change in name or SHA-256 content.

## 7. Troubleshooting

- **The page shows only the bootstrap title:** verify both asset URLs return
  `200` with the MIME types above, then inspect the browser console. Rebuild
  frontend assets only if you are a maintainer with the pinned npm
  dependencies.
- **Topology fetch failed:** check
  `curl -sS http://127.0.0.1:PORT/api/v1/graph`. The page deliberately shows a
  visible error instead of a blank canvas.
- **Topology cannot be drawn faithfully:** inspect every listed adapter
  diagnostic and the semantic raw-topology fallback. Correct the source graph;
  the dashboard will not repair, synthesize, drop, or reconnect malformed
  structure.
- **Layout fallback is reported:** the deterministic semantic grid remains
  available. Record the diagnostic and graph path; do not edit topology to
  make the display pass.
- **Counts differ:** compare node IDs and full edge endpoint tuples directly
  with the source JSON. Do not infer missing edges from application behavior.
- **An installed asset is missing:** rerun `cmake --install` with the
  repository-local prefix and confirm the three-file inventory in section 1.
- **Port binding fails:** choose an unused port in `1..65535`; the server binds
  IPv4 loopback only.
- **Init fails because a provider is missing:** topology inspection is still
  expected to work. Provider loading is operator-controlled and begins only
  after explicit Init.

This phase does not add hierarchy/grouping, collapsed bundles, a minimap,
persisted layout preferences, runtime metric overlays, the later metrics
subscriber bridge, or CLI removal. Those remain later-phase work.
