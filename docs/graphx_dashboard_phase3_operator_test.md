# GraphX dashboard Phase 3 operator test

This procedure qualifies the Phase 3 semantic topology and operator-usability
paths in the one native generic GraphX dashboard. It begins from a fresh clone,
uses source-tree and installed `graphx-dashboard`, and records objective
keyboard, focus, semantic, preference, reflow, network, console, and visual
evidence. Docker is optional. Do not use the legacy FHSS dashboard container.

This is a focused human review against the listed WCAG 2.2 Level AA criteria;
it is not formal accessibility certification. All FHSS and SAR data used here
is synthetic or checked-in recorded test data. This procedure makes no live-RF,
OTA, conducted-RF, HWIL, or hardware-qualification claim.

## 1. Fresh-clone native build and install

Install a compatible host C++ compiler, CMake, Ninja, Node.js/npm, Firefox,
`jq`, and the normal GraphX dependencies. Do not install an obsolete exact
Node patch version. Node is needed only to maintain/test checked-in frontend
assets; the ordinary C++ build, install, and installed dashboard need no Node
runtime or network download.

```bash
git clone <graphx-repository-url> GraphX
cd GraphX
cmake --preset ninja-debug
cmake --build --preset build-debug -j 8
ctest --test-dir build-ninja/ninja-debug --output-on-failure
cmake --install build-ninja/ninja-debug \
  --prefix "$PWD/build-ninja/ninja-debug/phase3-install"
```

The configure/build must use the repository C++26 mode and project
warnings-as-errors policy. Use repository-local build, install, and evidence
paths. Do not use `/private/tmp` or a virtual environment.

Expected sole dashboard inventory:

```text
build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/index.html
build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/assets/graphx-dashboard.css
build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/assets/graphx-dashboard.js
```

Verify source/install byte identity:

```bash
shasum -a 256 \
  libgraph/resources/web/index.html \
  libgraph/resources/web/assets/graphx-dashboard.css \
  libgraph/resources/web/assets/graphx-dashboard.js \
  build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/index.html \
  build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/assets/graphx-dashboard.css \
  build-ninja/ninja-debug/phase3-install/share/graphx/dashboard/assets/graphx-dashboard.js
```

Each installed hash must equal its source counterpart. No runtime request may
load a CDN, package registry, or alternate dashboard root.

## 2. Launch source and installed dashboards

Use distinct loopback ports. Each dashboard remains running, so launch these
commands in two separate terminals before opening a third terminal for test
commands:

```bash
./build-ninja/ninja-debug/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --port 8080

./build-ninja/ninja-debug/phase3-install/bin/graphx-dashboard \
  --graph libgraph/test/config/topologies/generic_grouped_split_merge.json \
  --port 8081
```

Open both pages in Firefox. Expected before and after page load:

- executor state is `CONFIGURED`;
- one `GET /api/v1/graph` supplies canvas, semantic hierarchy, search, and
  inspectors;
- no PATCH, lifecycle POST, group request, preference request, cookie, or
  background synchronization occurs; and
- the page exposes `Execution control`, `Dashboard views and local
  preferences`, active primary view, and `Inspector` in that logical order.

## 3. Independent semantic inventory matrix

Record direct source counts first:

```bash
jq '{nodes:(.nodes|length),edges:(.edges|length)}' \
  libgraph/test/config/topologies/minimal_graph.json \
  libgraph/test/config/topologies/generic_nested_semantic.json \
  libgraph/test/config/topologies/complex_network.json \
  examples/SAR/config/sar_stripmap_fanout.json \
  libdsp/config/fhss_phase2_binary_iq_receiver.json
```

Launch each graph with the same command and choose **Semantic topology**.
Do not use the canvas for this section.

| Graph | Path | Semantic nodes | Semantic edges | Exact sample |
|---|---|---:|---:|---|
| Minimal | `libgraph/test/config/topologies/minimal_graph.json` | 2 | 1 | `source_1` index `0` → `sink_1` index `0` |
| Generic nested | `libgraph/test/config/topologies/generic_nested_semantic.json` | 4 | 3 | `source` name `Data` → `transform_a` index `0`; `transform_a` index `0` → `transform_b` name `Input` |
| Complex | `libgraph/test/config/topologies/complex_network.json` | 9 | 9 | `source_1` name `Data` → `merge_1` name `In0` |
| SAR | `examples/SAR/config/sar_stripmap_fanout.json` | 21 | 23 | `fanout` index `0` → `split_tile0` index `0` |
| FHSS | `libdsp/config/fhss_phase2_binary_iq_receiver.json` | 75 | 137 | `channelizer` index `63` → `detector_63` index `0`; `detector_63` index `0` → `merge` index `64` |

Objective checks for every row:

1. The semantic count reads `Showing N of N authoritative nodes and E of E
   authoritative edges` with the table values above.
2. Each node identity appears once as a primary semantic node record. Each
   edge identity appears once as a primary table row.
3. Node type, connected input/output ports, and complete direct/transitive
   group path are visible text.
4. Every edge row includes full stable identity, source node, source port kind
   and value, target node, and target port kind and value.
5. Clearing search restores the exact complete inventory and stable order.
6. Canvas collapse or isolation does not remove any semantic record. Semantic
   group disclosure is independent and can reveal a canvas-hidden member.
7. Invalid presentation metadata reports the stable diagnostic and shows a
   flat stable-identity semantic/raw fallback without partial grouping.

For the generic nested fixture, verify the stable semantic order
`transform_a`, `transform_b`, `sink`, `source` and group paths
`pipeline/processing`, `pipeline/processing`, `pipeline`, `pipeline`. Group
summaries must report exact direct/transitive, descendant, internal,
hidden/crossing, and current bundle counts. Separately verify all nine full
edge tuples from `complex_network.json`; do not substitute the Phase 2 grouped
fixture for this complex row. For FHSS,
the authored `detector-bank` semantic branch remains reachable when 64 nodes
are canvas-hidden and both 64-member bundles are collapsed.

## 4. Complete keyboard and focus table

Perform this table without a mouse in both source and installed generic pages.
Record the focused element's accessible name after every row.

| Keys | Starting control | Objective result |
|---|---|---|
| `Tab` | browser content start | Focus lands first on **Skip to dashboard view controls** with a visible outline. |
| `Enter` | skip link | Focus moves to the dashboard view-control region; no request is issued. |
| `Tab` / `Shift+Tab` | page controls | Stable logical order traverses execution controls, view/reset controls, active view, inspector, and footer without a trap. |
| `Enter` or `Space` | view/mode/reset/lifecycle button | The named operation activates exactly once; focus remains visible. |
| `Enter` or `Space` | native semantic group summary | Only semantic disclosure toggles; canvas collapse and requests do not change. |
| `Enter` or `Space` | semantic node/edge selection | Exact authoritative identity becomes selected and the shared inspector updates. |
| `Enter` or `Space` | semantic group action | Inspect, canvas collapse/expand, or isolate performs only the action named by the control. |
| `Enter` or `Space` | breadcrumb, minimap, canvas node/edge, bundle/member | Existing Phase 2 operation remains keyboard operable and updates the same inspector model. |
| Arrow keys | focused minimap control | Viewport pans in the named direction; `+`/`-` zoom; no graph request occurs. |
| `Tab` / `Shift+Tab` | open parameter dialog | Focus cycles only among textarea, Save, and Cancel. |
| `Escape` | open parameter dialog | Dialog closes without saving and focus returns to its invoking control. |
| `Escape` | elsewhere | No edit is discarded and no lifecycle or graph request occurs. |

There are no positive `tabindex` values and no custom ARIA tree/treegrid arrow
contract. Native list, details/summary, button, table, heading, and region
semantics are intentional.

Refresh/focus checks:

- Select and focus a semantic node; edit its parameters and Save. The one
  PATCH is followed by one graph refresh. If the identity survives, selection,
  inspector identity, and semantic focus return to that stable identity.
- In a controlled fixture that removes the selected identity on refresh,
  selection clears, the polite status names the removed identity, and focus
  moves to the active-view heading. Focus must not remain on detached DOM.
- Mode, view, semantic disclosure, collapse, and isolation retain focus unless
  the focused control is removed; then use the deterministic active heading.

## 5. Screen-reader-oriented and non-color observations

Using Firefox Accessibility Inspector, record role/name/state for:

- execution region/state and every lifecycle button;
- dashboard views and browser-local reset;
- semantic region, search, group disclosure, node selection, edge table, and
  contained overflow region;
- shared inspector and parameter dialog; and
- one invalid warning and one polite completion/fallback status.

Expected observations:

- landmark names are unique and DOM IDs are unique;
- native disclosure exposes expanded/collapsed state;
- selection exposes `aria-pressed` plus visible `Selected authoritative ...`
  text;
- group summary visibly states canvas collapsed/expanded and
  contains-selection state;
- exact identifiers and numeric/named ports are visible and included in
  selection names;
- dirty/clean and execution states are literal text, not color alone;
- warning/status borders, labels, and text remain understandable in grayscale
  and forced-colors mode; and
- no unsupported edit/reorder/connect ARIA semantics are exposed.

## 6. Local presentation preferences and request isolation

The only storage key is `graphx.dashboard.presentation`. Inspect it in Firefox
Storage after changing grouped/raw mode, a canvas collapse, semantic disclosure,
and viewport:

```json
{
  "schema": 1,
  "graph_signature": "sha256:<lowercase-hex>",
  "mode": "grouped",
  "collapsed_group_ids": ["group-id"],
  "semantic_expanded_group_ids": ["group-id"],
  "viewport": {"x": 0, "y": 0, "zoom": 1}
}
```

Reload and confirm valid state restores. Change only `node_config`; the
structural signature remains stable and valid preferences survive. Launch a
different graph; the stale signature is rejected visibly and deterministic
defaults apply. Where practical, replace the stored value with `{`, an unknown
schema, unknown group, `NaN`-equivalent invalid type, or zoom outside
`[0.1,4]`; reload must remain usable and show one concise fallback status.

Choose **Reset view preferences**. The storage record is removed, authored
collapse/default semantic disclosure and deterministic fit/reset viewport
apply immediately, and the record remains absent until a later operator view
interaction. A denied/unavailable/quota-failed local storage operation is
non-fatal and visibly reported.

Network evidence must show that mode, zoom, viewport, collapse, semantic
disclosure, selection, focus, reset, and isolation issue no PATCH, execution,
preference, group, export, or new endpoint request. A node save body contains
only `{"node_config":...}`. No preference appears in graph JSON, PATCH bodies,
lifecycle requests, cookies, or server state.

## 7. Reduced motion, 320-pixel reflow, 200% zoom, and text spacing

In Firefox Responsive Design Mode set a 320 CSS-pixel viewport. In Semantic
topology, objectively confirm:

- header, execution state/actions, view/reset, search, counts, hierarchy,
  warnings, inspector/editor, revision/dirty state, and footer remain visible
  and operable;
- the document has no page-level horizontal scroll;
- the exact edge table alone uses its labelled, focusable contained overflow
  region and full identities remain available; and
- controls remain at least 24 by 24 CSS pixels or meet the inline/spacing
  exception, with focus not obscured.

Set Firefox page zoom to 200% at a normal desktop window and repeat. Then apply
these temporary author styles and repeat at desktop and narrow width:

```css
* { line-height: 1.5 !important; letter-spacing: .12em !important;
    word-spacing: .16em !important; }
p { margin-bottom: 2em !important; }
```

No label, control, state, identity, port, warning, or inspector action may be
clipped or hidden. The two-dimensional canvas may retain contained pan/zoom;
it may not block access to Semantic topology.

Enable OS/Firefox reduced motion and confirm dashboard-controlled transition
and animation duration becomes effectively zero, smooth scrolling is absent,
and all state remains available as text/static form. Disable it and confirm no
motion is required to understand selection, status, warning, or execution.

## 8. Reproducible browser evidence

Maintainers use the pinned lockfile without adding a dependency:

```bash
cd libgraph/web
npm ci --ignore-scripts
npm run typecheck
npm test
npm run build
npm run check:assets
cd ../..
```

Run Phase 3 Firefox evidence for source and installed executables. The harness
derives node/edge identities and exact port tuples directly from graph JSON,
checks keyboard/focus/local-request isolation, both reduced-motion system
values, 320-pixel reflow, exact 200% Firefox page zoom, text spacing, console
errors, and captures desktop/narrow/zoom screenshots. The headless harness
installs a temporary test-only Firefox extension, calls Firefox
`tabs.setZoom(2)`, awaits its acknowledgement, verifies a 1.9–2.1
DPR/inner-width scale, and asserts that no CSS `zoom` simulation is present.
For headed manual review, use Firefox's Command/Control-plus page-zoom shortcut
until Firefox reports 200%:

```bash
cd libgraph/web
npm run test:browser:phase3 -- \
  --dashboard ../../build-ninja/ninja-debug/graphx-dashboard \
  --graph ../../libgraph/test/config/topologies/generic_nested_semantic.json \
  --port 8092 \
  --screenshot ../../build-ninja/ninja-debug/phase3-evidence/source-generic-nested.png

npm run test:browser:phase3 -- \
  --dashboard ../../build-ninja/ninja-debug/phase3-install/bin/graphx-dashboard \
  --graph ../../libgraph/test/config/topologies/complex_network.json \
  --port 8093 \
  --screenshot ../../build-ninja/ninja-debug/phase3-evidence/installed-complex.png
cd ../..
```

Repeat the minimal, generic nested, complex, SAR, and FHSS matrices across
source/install as needed. Expected console error count is zero. Record browser
version, focused accessible name, semantic counts, screenshots, accessibility
observations, every non-GET request, and unexpected request count.

## 9. Focused human WCAG 2.2 AA worksheet

Record `PASS`, `FAIL`, or `N/A`, evidence path, and observation separately from
automated results:

| Criterion | Direct changed-path evidence | Status (PASS/FAIL/N/A) | Evidence path | Observation |
|---|---|---|---|---|
| 1.3.1 Info and Relationships | Native headings/lists/details/table/labels and inspector definitions expose structure. |  |  |  |
| 1.3.2 Meaningful Sequence | DOM and keyboard order follow execution → views → active primary view → inspector. |  |  |  |
| 1.4.1 Use of Color | Visible selected/state/warning/dirty text and non-color borders/outlines. |  |  |  |
| 1.4.3 Contrast (Minimum) | Measure normal/secondary/selected/warning/status text combinations. |  |  |  |
| 1.4.10 Reflow | 320 CSS pixels and 200% zoom retain content; only contained edge table/canvas overflow. |  |  |  |
| 1.4.11 Non-text Contrast | Measure focus, control borders, selected and warning boundaries. |  |  |  |
| 1.4.12 Text Spacing | Locked overrides do not clip/hide content or operation. |  |  |  |
| 2.1.1 Keyboard / 2.1.2 No Keyboard Trap | Complete keyboard table passes; only modal intentionally cycles until Escape/Cancel. |  |  |  |
| 2.4.3 Focus Order | Recorded focused-name sequence follows meaningful order. |  |  |  |
| 2.4.7 Focus Visible | Every focused control has a 3px non-color outline. |  |  |  |
| 2.4.11 Focus Not Obscured (Minimum) | Focused targets remain fully visible at narrow/zoom/modal states. |  |  |  |
| 2.5.3 Label in Name | Visible button text occurs in accessible names; exact IDs supplement it. |  |  |  |
| 2.5.8 Target Size (Minimum) | Measure changed targets or record applicable spacing/inline exception. |  |  |  |
| 3.2.1 On Focus / 3.2.2 On Input | Focus/input alone causes no navigation, PATCH, lifecycle, or graph change. |  |  |  |
| 3.3.1 Error Identification | Invalid JSON and failed PATCH are named in dialog alert/status. |  |  |  |
| 3.3.2 Labels or Instructions | Editor names node/type and requires a JSON object. |  |  |  |
| 3.3.3 Error Suggestion | Parse/type failure provides the corrective JSON-object requirement. |  |  |  |
| 4.1.2 Name, Role, Value | Accessibility Inspector results for controls, disclosure, selection, modal, and landmarks. |  |  |  |
| 4.1.3 Status Messages | Fetch/PATCH/removal/preference/command outcomes are concise status or alert messages. |  |  |  |

Automated tests supplement this worksheet; they do not prove conformance.

## 10. Troubleshooting

- **Preference fallback on every load:** confirm secure-context Web Crypto and
  browser local storage are available; inspect the exact schema/signature.
- **A semantic identity is missing:** compare primary records and exact full
  edge tuples against source JSON. Do not infer or repair topology.
- **Canvas layout fails:** use Semantic topology and the exact raw diagnostic;
  layout failure must not remove semantic access.
- **Focus disappears after refresh:** record the prior stable semantic key,
  selected identity, active heading, and DOM connectivity. Detached focus is a
  failure.
- **Page scrolls horizontally:** identify the overflowing reflowable element.
  Only the labelled edge table and contained 2-D canvas may overflow.
- **Installed behavior differs:** compare the three source/install hashes and
  reinstall into the same prefix; the server never borrows another root.
- **Browser harness cannot launch Firefox:** install a compatible host Firefox
  or set `GRAPHX_FIREFOX_EXECUTABLE`. Do not silently download another runtime.
- **Provider/init failure:** topology and semantic inspection must still work;
  page load does not initialize providers or execute the graph.

Phase 4 metric overlays, a command console, server preferences, graph export,
new APIs, CLI removal, and runtime subscriptions are intentionally absent.
