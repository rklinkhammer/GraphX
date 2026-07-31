import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { afterEach, describe, expect, test, vi } from "vitest";

import { adaptGraphDocument } from "./adapter";
import App, { BundleInspector, EdgeInspector } from "./App";
import { adaptPresentationGroups } from "./hierarchy";
import { layoutDisplayGraph } from "./layout";
import { projectPresentation } from "./presentation";
import empty from "./test/fixtures/empty.json";
import malformedBranches from "./test/fixtures/malformed_branches.json";
import malformed from "./test/fixtures/malformed.json";
import numericPorts from "./test/fixtures/numeric_ports.json";
import invalidGroups from "./test/fixtures/invalid_groups.json";

const sourceRoot = resolve(process.cwd(), "../..");
const groupedSplitMerge = JSON.parse(
  readFileSync(
    resolve(
      sourceRoot,
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    ),
    "utf8",
  ),
) as Record<string, unknown>;

const executionState = {
  state: "CONFIGURED",
  coordinator_revision: 0,
  configured_revision: 0,
  active_revision: null,
  graph_generation: 1,
  configuration_dirty: false,
};

function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function installApi(graph: unknown) {
  let graphFetches = 0;
  let patched = false;
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    if (url.endsWith("/api/v1/graph")) {
      graphFetches += 1;
      const document = structuredClone(graph) as {
        nodes?: Array<{ id?: string; node_config?: Record<string, unknown> }>;
      };
      if (patched && document.nodes?.[0]) {
        document.nodes[0].node_config = { updated: true };
      }
      return jsonResponse({ success: true, data: document });
    }
    if (url.endsWith("/api/v1/execution/state")) {
      return jsonResponse({
        success: true,
        data: {
          ...executionState,
          coordinator_revision: patched ? 1 : 0,
          configuration_dirty: patched,
        },
      });
    }
    if (init?.method === "PATCH" && url.includes("/api/v1/nodes/")) {
      patched = true;
      return jsonResponse({ success: true, data: {} });
    }
    if (init?.method === "POST" && url.includes("/api/v1/execution/")) {
      return jsonResponse({ success: true, message: "accepted" });
    }
    return jsonResponse(
      { success: false, message: `unexpected test request ${url}` },
      404,
    );
  });
  vi.stubGlobal("fetch", fetchMock);
  return { fetchMock, getGraphFetches: () => graphFetches };
}

function expectExactRawHandles(
  container: HTMLElement,
  graphDocument: unknown,
): void {
  const model = adaptGraphDocument(graphDocument);
  for (const node of model.nodes) {
    const element = [...container.querySelectorAll(".react-flow__node")].find(
      (candidate) => candidate.getAttribute("data-id") === node.id,
    );
    expect(element, `missing canvas node ${node.id}`).not.toBeNull();
    const actual = [...element!.querySelectorAll("[data-handleid]")]
      .map((handle) => handle.getAttribute("data-handleid"))
      .sort();
    const expected = [...node.inputPorts, ...node.outputPorts]
      .map((port) => port.id)
      .sort();
    expect(actual).toEqual(expected);
  }
  expect(
    container.querySelector('[data-handleid^="presentation-boundary-"]'),
  ).toBeNull();
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("generic dashboard components", () => {
  test("renders exact node, edge, and handle counts from one initial graph fetch", async () => {
    const api = installApi(numericPorts);
    const { container } = render(<App />);

    expect((await screen.findByTestId("topology-counts")).textContent).toContain(
      "2 nodes, 1 edges",
    );
    await waitFor(() =>
      expect(screen.getAllByTestId("graph-node-card")).toHaveLength(2),
    );
    expect(screen.getAllByTestId("input-port")).toHaveLength(1);
    expect(screen.getAllByTestId("output-port")).toHaveLength(1);
    await waitFor(() =>
      expect(screen.getByTestId("canvas-edge-count").textContent).toContain(
        "1 edges",
      ),
    );
    await waitFor(() =>
      expect(container.querySelectorAll(".react-flow__minimap-node")).toHaveLength(
        2,
      ),
    );
    expectExactRawHandles(container, numericPorts);
    expect(api.getGraphFetches()).toBe(1);
    expect(
      (screen.getByRole("button", { name: "Zoom in topology" }) as HTMLButtonElement)
        .disabled,
    ).toBe(false);
    expect(
      (screen.getByRole("button", { name: "Zoom out topology" }) as HTMLButtonElement)
        .disabled,
    ).toBe(false);
    expect(
      (screen.getByRole("button", { name: "Fit topology to view" }) as HTMLButtonElement)
        .disabled,
    ).toBe(false);
    expect(
      screen.getByRole("button", {
        name: "Reset deterministic topology layout",
      }),
    ).toHaveProperty("disabled", false);
    for (const forbidden of ["Add node", "Delete node", "Connect edge", "Reconnect edge"]) {
      expect(screen.queryByRole("button", { name: forbidden })).toBeNull();
    }
  });

  test("synchronizes canvas/table selection and shows exact edge inspector fields", async () => {
    installApi(numericPorts);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");

    const sourceCanvasNode = await waitFor(() => {
      const node = container.querySelector(
        '.react-flow__node[data-id="numeric-source"]',
      );
      expect(node).not.toBeNull();
      return node as HTMLElement;
    });
    fireEvent.click(sourceCanvasNode);
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "numeric-source",
    );

    cleanup();
    const edgeModel = adaptGraphDocument(numericPorts).edges[0];
    render(<EdgeInspector edge={edgeModel} />);
    const inspector = screen.getByTestId("edge-inspector");
    expect(inspector.textContent).toContain("numeric-source");
    expect(inspector.textContent).toContain("index 0");
    expect(inspector.textContent).toContain("numeric-sink");
    expect(inspector.textContent).toContain("index 2");

    cleanup();
    installApi(numericPorts);
    render(<App />);
    await screen.findByTestId("topology-counts");
    const nodesTab = screen.getByRole("button", { name: "Nodes & parameters" });
    nodesTab.focus();
    await user.keyboard("{Enter}");
    const tableSelection = screen.getByRole("button", { name: "numeric-source" });
    await user.click(tableSelection);
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "numeric-source",
    );
    expect(tableSelection.getAttribute("aria-pressed")).toBe("true");
  });

  test("selects the focused semantic edge with Enter and Space", async () => {
    installApi(numericPorts);
    render(<App />);
    await screen.findByTestId("topology-counts");

    await waitFor(() =>
      expect(screen.getByTestId("canvas-edge-count").textContent).toContain(
        "1 edges",
      ),
    );
    const edgeModel = (await layoutDisplayGraph(
      adaptGraphDocument(numericPorts),
    )).edges[0];
    expect(edgeModel.ariaLabel).toContain(
      "Edge from numeric-source index 0 to numeric-sink index 2",
    );
    const appendFocusedEdge = () => {
      const edge = document.createElementNS("http://www.w3.org/2000/svg", "g");
      edge.classList.add("react-flow__edge");
      edge.setAttribute("data-id", edgeModel.id);
      edge.setAttribute("aria-label", edgeModel.ariaLabel ?? "");
      screen.getByTestId("topology-canvas").append(edge);
      return edge;
    };

    const edge = appendFocusedEdge();
    expect(fireEvent.keyDown(edge, { key: "Enter" })).toBe(false);
    expect(screen.getByTestId("edge-inspector").textContent).toContain(
      "numeric-source",
    );

    fireEvent.click(
      screen.getByRole("button", { name: "Clear topology selection" }),
    );
    expect(screen.queryByTestId("edge-inspector")).toBeNull();
    const currentEdge = appendFocusedEdge();
    expect(fireEvent.keyDown(currentEdge, { key: " " })).toBe(false);
    expect(screen.getByTestId("edge-inspector").textContent).toContain(
      "numeric-sink",
    );
  });

  test("shows loading, empty, and every malformed diagnostic without repair", async () => {
    let resolveGraph!: (response: Response) => void;
    vi.stubGlobal(
      "fetch",
      vi.fn(async (input: RequestInfo | URL) => {
        if (String(input).endsWith("/api/v1/graph")) {
          return new Promise<Response>((resolve) => {
            resolveGraph = resolve;
          });
        }
        return jsonResponse({ success: true, data: executionState });
      }),
    );
    render(<App />);
    expect(screen.getByText("Loading authoritative topology…")).not.toBeNull();
    resolveGraph(jsonResponse({ success: true, data: empty }));
    expect(await screen.findByText("Empty graph")).not.toBeNull();

    cleanup();
    vi.unstubAllGlobals();
    installApi(malformedBranches);
    render(<App />);
    expect(
      await screen.findByText("Topology cannot be drawn faithfully"),
    ).not.toBeNull();
    for (const code of [
      "invalid_edge",
      "missing_endpoint",
      "duplicate_edge_identity",
      "invalid_node",
      "invalid_port",
    ]) {
      expect(screen.getAllByText(new RegExp(code)).length).toBeGreaterThan(0);
    }
    expect(screen.getByText("Semantic raw topology fallback")).not.toBeNull();
    expect(screen.getByTestId("topology-counts").textContent).toContain(
      "5 nodes, 9 edges",
    );
  });

  test("fit, reset, and clear controls activate from the keyboard", async () => {
    installApi(numericPorts);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");
    const node = await waitFor(() => {
      const candidate = container.querySelector(
        '.react-flow__node[data-id="numeric-source"]',
      );
      expect(candidate).not.toBeNull();
      return candidate as HTMLElement;
    });
    fireEvent.click(node);
    expect(screen.getByTestId("node-inspector")).not.toBeNull();

    for (const name of [
      "Fit topology to view",
      "Reset deterministic topology layout",
      "Clear topology selection",
    ]) {
      const button = screen.getByRole("button", { name });
      button.focus();
      await user.keyboard("{Enter}");
      expect(document.activeElement).toBe(button);
    }
    expect(screen.queryByTestId("node-inspector")).toBeNull();
    await waitFor(() =>
      expect(screen.getByTestId("canvas-edge-count").textContent).toContain(
        "1 edges",
      ),
    );
  });

  test("refreshes the single authoritative graph after PATCH and retains valid selection", async () => {
    const api = installApi(numericPorts);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Nodes & parameters" }));
    await user.click(screen.getByRole("button", { name: "numeric-source" }));
    await user.click(screen.getAllByRole("button", { name: "Edit parameters" })[1]);
    fireEvent.change(screen.getByLabelText("Node configuration (JSON object)"), {
      target: { value: '{"updated":true}' },
    });
    await user.click(screen.getByRole("button", { name: "Save parameters" }));

    expect(
      await screen.findByText("Parameters for numeric-source updated in memory"),
    ).not.toBeNull();
    expect(api.getGraphFetches()).toBe(2);
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "numeric-source",
    );
    await waitFor(() =>
      expect(screen.getByTestId("revision-summary").textContent).toContain(
        "Coordinator revision 1",
      ),
    );
  });

  test("shows malformed semantic fallback and never leaves a blank canvas", async () => {
    installApi(malformed);
    render(<App />);
    expect(
      await screen.findByText("Topology cannot be drawn faithfully"),
    ).not.toBeNull();
    expect(screen.getByText(/duplicate_node_identity/)).not.toBeNull();
    expect(screen.getAllByText(/invalid_port/).length).toBeGreaterThan(0);
    expect(screen.getByText("Semantic raw topology fallback")).not.toBeNull();
    expect(screen.queryByTestId("topology-canvas")).toBeNull();
  });

  test("shows a visible fetch error and retry control", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async (input: RequestInfo | URL) => {
        if (String(input).endsWith("/api/v1/graph")) {
          return jsonResponse(
            { success: false, message: "fixture graph unavailable" },
            503,
          );
        }
        return jsonResponse({ success: true, data: executionState });
      }),
    );
    render(<App />);
    expect(await screen.findByText("Topology fetch failed")).not.toBeNull();
    expect(screen.getByText("fixture graph unavailable")).not.toBeNull();
    expect(
      (screen.getByRole("button", { name: "Retry graph fetch" }) as HTMLButtonElement)
        .disabled,
    ).toBe(false);
  });

  test("renders nested compounds, minimap, truthful counts, and raw equality", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");

    await waitFor(() =>
      expect(screen.getAllByTestId("graph-group-card")).toHaveLength(4),
    );
    expect(screen.getAllByTestId("graph-node-card")).toHaveLength(7);
    expect(screen.getByTestId("grouped-counts").textContent).toContain(
      "7 authoritative nodes, 4 groups, 7 edges/bundles (2 bundles)",
    );
    expect(container.querySelector(".react-flow__minimap")).not.toBeNull();
    const minimap = screen.getByRole("group", {
      name: "Keyboard topology minimap",
    });
    minimap.focus();
    expect(document.activeElement).toBe(minimap);
    expect(minimap.getAttribute("aria-keyshortcuts")).toContain("ArrowRight");
    const viewport = container.querySelector(".react-flow__viewport")!;
    const beforePan = viewport.getAttribute("style");
    fireEvent.keyDown(minimap, { key: "ArrowRight" });
    await waitFor(() =>
      expect(viewport.getAttribute("style")).not.toBe(beforePan),
    );
    expect(document.activeElement).toBe(minimap);
    expect(
      screen.getByRole("button", { name: "Grouped topology" }).getAttribute(
        "aria-pressed",
      ),
    ).toBe("true");

    await user.click(screen.getByRole("button", { name: "Raw topology" }));
    await waitFor(() =>
      expect(screen.getAllByTestId("graph-node-card")).toHaveLength(9),
    );
    expectExactRawHandles(container, groupedSplitMerge);
    const nodeButton = container.querySelector(
      '.react-flow__node[data-id="source_1"] .node-keyboard-select',
    ) as HTMLButtonElement;
    const layoutCount =
      screen.getByTestId("layout-invocation-count").textContent;
    const selectionViewport =
      container.querySelector(".react-flow__viewport")!.getAttribute("style");
    nodeButton.focus();
    await user.keyboard("{Enter}");
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "source_1",
    );
    expect(nodeButton.isConnected).toBe(true);
    expect(document.activeElement).toBe(nodeButton);
    expect(screen.getByTestId("layout-invocation-count").textContent).toBe(
      layoutCount,
    );
    expect(
      container.querySelector(".react-flow__viewport")!.getAttribute("style"),
    ).toBe(selectionViewport);
    expect(screen.queryAllByTestId("graph-group-card")).toHaveLength(0);
    expect(screen.getByTestId("canvas-edge-count").textContent).toContain(
      "9 edges",
    );
  });

  test("preserves hidden authoritative selection through grouped/raw and collapse cycles", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");

    await user.click(screen.getByRole("button", { name: "Raw topology" }));
    await waitFor(() =>
      expect(
        container.querySelector(
          '.react-flow__node[data-id="interior_1"] .node-keyboard-select',
        ),
      ).not.toBeNull(),
    );
    fireEvent.click(
      container.querySelector(
        '.react-flow__node[data-id="interior_1"] .node-keyboard-select',
      )!,
    );
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "interior_1",
    );

    await user.click(screen.getByRole("button", { name: "Grouped topology" }));
    await waitFor(() =>
      expect(
        container.querySelector(
          '.react-flow__node[data-id="parallel-stage"] .contains-selection',
        ) ??
          container.querySelector(
            '.react-flow__node[data-id="parallel-stage"] .graph-group-card.contains-selection',
          ),
      ).not.toBeNull(),
    );
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "interior_1",
    );
    expect(
      container.querySelector('.react-flow__node[data-id="interior_1"]'),
    ).toBeNull();

    for (let cycle = 0; cycle < 2; ++cycle) {
      await user.click(
        screen.getByRole("button", {
          name: "Expand group parallel-stage",
        }),
      );
      await waitFor(() =>
        expect(
          container.querySelector('.react-flow__node[data-id="interior_1"]'),
        ).not.toBeNull(),
      );
      await user.click(
        screen.getByRole("button", {
          name: "Collapse group parallel-stage",
        }),
      );
      await waitFor(() =>
        expect(
          container.querySelector('.react-flow__node[data-id="interior_1"]'),
        ).toBeNull(),
      );
    }
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "interior_1",
    );
  });

  test("inspects exact bundle members without replacing prior authoritative selection", async () => {
    const model = adaptGraphDocument(groupedSplitMerge);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["parallel-stage"]),
      isolatedGroupId: null,
    });
    const selectedEdges: string[] = [];
    render(
      <BundleInspector
        bundle={projection.bundles[0]}
        model={model}
        onSelectMemberEdge={(edgeId) => selectedEdges.push(edgeId)}
      />,
    );
    const bundleInspector = screen.getByTestId("bundle-inspector");
    expect(bundleInspector.textContent).toContain("Authoritative member count");
    expect(bundleInspector.textContent).toContain("2");
    expect(
      screen.getAllByRole("button", { name: /Inspect authoritative edge/ }),
    ).toHaveLength(2);
    fireEvent.click(
      screen.getAllByRole("button", { name: /Inspect authoritative edge/ })[0],
    );
    expect(selectedEdges).toEqual([projection.bundles[0].memberEdgeIds[0]]);
  });

  test("isolates nested groups and navigates exact breadcrumbs with keyboard", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    const isolate = await screen.findByRole("button", {
      name: "Isolate group parallel-stage",
    });
    isolate.focus();
    await user.keyboard("{Enter}");
    expect(await screen.findByText("Isolated: Parallel stage")).not.toBeNull();
    expect(
      screen.getByRole("navigation", { name: "Group breadcrumbs" }).textContent,
    ).toContain("Processing pipeline / Parallel stage");
    const parent = screen.getByRole("button", { name: "Return to parent" });
    parent.focus();
    await user.keyboard("{Enter}");
    expect(await screen.findByText("Isolated: Processing pipeline")).not.toBeNull();
    await user.click(
      screen.getByRole("button", { name: "Return to all topology" }),
    );
    expect(screen.queryByText(/Isolated:/)).toBeNull();
  });

  test("invalid hierarchy visibly falls back atomically to exact raw topology", async () => {
    installApi(invalidGroups);
    const { container } = render(<App />);
    expect(
      await screen.findByText(
        "Presentation grouping rejected; Raw topology preserved",
      ),
    ).not.toBeNull();
    expect(screen.getByText(/overlapping_group_member/)).not.toBeNull();
    await waitFor(() =>
      expect(screen.getAllByTestId("graph-node-card")).toHaveLength(3),
    );
    expect(screen.queryAllByTestId("graph-group-card")).toHaveLength(0);
    expect(
      screen.getByRole("button", { name: "Raw topology" }).getAttribute(
        "aria-pressed",
      ),
    ).toBe("true");
    expect(
      (screen.getByRole("button", {
        name: "Grouped topology",
      }) as HTMLButtonElement).disabled,
    ).toBe(true);
    expectExactRawHandles(container, invalidGroups);
    await waitFor(() =>
      expect(container.querySelectorAll(".react-flow__minimap-node")).toHaveLength(
        3,
      ),
    );
  });
});
