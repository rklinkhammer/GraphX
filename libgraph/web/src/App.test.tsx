import { act, cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { webcrypto } from "node:crypto";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { afterEach, describe, expect, test, vi } from "vitest";

import { adaptGraphDocument } from "./adapter";
import App, {
  BundleInspector,
  EdgeInspector,
  focusPersistentInspectorHeading,
  presentationSelectionSurvivesRefresh,
  removedSelectionNotice,
} from "./App";
import { adaptPresentationGroups } from "./hierarchy";
import * as hierarchyLayoutModule from "./hierarchyLayout";
import { layoutDisplayGraph } from "./layout";
import { projectPresentation } from "./presentation";
import {
  graphSignature,
  type PresentationPreferences,
} from "./preferences";
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

function installRemovingApi() {
  let patched = false;
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    if (url.endsWith("/api/v1/graph")) {
      const graph = structuredClone(numericPorts) as {
        nodes: Array<{ id: string }>;
        edges: unknown[];
      };
      if (patched) {
        graph.nodes = graph.nodes.filter((node) => node.id !== "numeric-source");
        graph.edges = [];
      }
      return jsonResponse({ success: true, data: graph });
    }
    if (url.endsWith("/api/v1/execution/state")) {
      return jsonResponse({ success: true, data: executionState });
    }
    if (init?.method === "PATCH") {
      patched = true;
      return jsonResponse({ success: true, data: {} });
    }
    return jsonResponse({ success: false, message: "unexpected request" }, 404);
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

function installEmptyingApi() {
  let patched = false;
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    if (url.endsWith("/api/v1/graph")) {
      return jsonResponse({
        success: true,
        data: patched ? { nodes: [], edges: [] } : structuredClone(numericPorts),
      });
    }
    if (url.endsWith("/api/v1/execution/state")) {
      return jsonResponse({ success: true, data: executionState });
    }
    if (init?.method === "PATCH") {
      patched = true;
      return jsonResponse({ success: true, data: {} });
    }
    return jsonResponse({ success: false, message: "unexpected request" }, 404);
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

function installRetryApi() {
  let graphRequests = 0;
  const fetchMock = vi.fn(async (input: RequestInfo | URL) => {
    const url = String(input);
    if (url.endsWith("/api/v1/graph")) {
      graphRequests += 1;
      return graphRequests === 1
        ? jsonResponse({ success: false, message: "temporary fetch failure" }, 503)
        : jsonResponse({ success: true, data: numericPorts });
    }
    if (url.endsWith("/api/v1/execution/state")) {
      return jsonResponse({ success: true, data: executionState });
    }
    return jsonResponse({ success: false, message: "unexpected request" }, 404);
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

function installPresentationRemovingApi(kind: "group" | "bundle") {
  let patched = false;
  const initialModel = adaptGraphDocument(groupedSplitMerge);
  const initialHierarchy = adaptPresentationGroups(initialModel);
  const initialProjection = projectPresentation(initialModel, initialHierarchy, {
    mode: "grouped",
    collapsedGroupIds: new Set(["parallel-stage"]),
    isolatedGroupId: null,
  });
  const bundledEdges = new Set(
    initialProjection.bundles.flatMap((bundle) => bundle.memberEdgeIds),
  );
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    if (url.endsWith("/api/v1/graph")) {
      const graph = structuredClone(groupedSplitMerge) as {
        edges: Array<Record<string, unknown>>;
        presentation: { groups: Array<{ id: string }> };
      };
      if (patched && kind === "group") {
        graph.presentation.groups = graph.presentation.groups.filter(
          (group) => group.id !== "parallel-stage",
        );
      }
      if (patched && kind === "bundle") {
        const graphModel = adaptGraphDocument(graph);
        graph.edges = graph.edges.filter(
          (_edge, index) => !bundledEdges.has(graphModel.edges[index].id),
        );
      }
      return jsonResponse({ success: true, data: graph });
    }
    if (url.endsWith("/api/v1/execution/state")) {
      return jsonResponse({ success: true, data: executionState });
    }
    if (init?.method === "PATCH") {
      patched = true;
      return jsonResponse({ success: true, data: {} });
    }
    return jsonResponse({ success: false, message: "unexpected request" }, 404);
  });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

function expectOnlyLocalReadRequests(fetchMock: ReturnType<typeof vi.fn>): void {
  const allowed = new Set([
    "GET /api/v1/graph",
    "GET /api/v1/execution/state",
  ]);
  const actual = fetchMock.mock.calls.map(([input, init]) => {
    const path = new URL(String(input), "http://dashboard.test").pathname;
    return `${init?.method ?? "GET"} ${path}`;
  });
  expect(actual.every((request) => allowed.has(request))).toBe(true);
  expect(actual).toContain("GET /api/v1/graph");
  expect(actual).toContain("GET /api/v1/execution/state");
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
  window.localStorage.clear();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe("generic dashboard components", () => {
  test("keeps page entry order and lifecycle keyboard activation focus", async () => {
    const api = installApi(numericPorts);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.tab();
    expect(document.activeElement).toBe(
      screen.getByRole("link", { name: "Skip to dashboard view controls" }),
    );
    await user.tab();
    const configure = screen.getByRole("button", { name: "Configure" });
    expect(document.activeElement).toBe(configure);
    await user.tab({ shift: true });
    expect(document.activeElement).toBe(
      screen.getByRole("link", { name: "Skip to dashboard view controls" }),
    );
    configure.focus();
    await user.keyboard("{Enter}");
    await waitFor(() =>
      expect(
        api.fetchMock.mock.calls.some(
          ([input, init]) =>
            init?.method === "POST" &&
            String(input).endsWith("/api/v1/execution/configure"),
        ),
      ).toBe(true),
    );
    expect(document.activeElement).toBe(configure);
  });

  test("retains focus across every lifecycle, mode, group collapse, and reset command", async () => {
    const api = installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");

    for (const label of ["Configure", "Init", "Start", "Run", "Stop", "Join"]) {
      const button = screen.getByRole("button", { name: label });
      const before = api.fetchMock.mock.calls.length;
      button.focus();
      await user.keyboard("{Enter}");
      await waitFor(() => expect(api.fetchMock.mock.calls.length).toBeGreaterThan(before));
      expect(document.activeElement).toBe(button);
    }

    const raw = screen.getByRole("button", { name: "Raw topology" });
    raw.focus();
    await user.keyboard("{Enter}");
    expect(raw.getAttribute("aria-pressed")).toBe("true");
    expect(document.activeElement).toBe(raw);
    const grouped = screen.getByRole("button", { name: "Grouped topology" });
    grouped.focus();
    await user.keyboard("{Enter}");
    expect(grouped.getAttribute("aria-pressed")).toBe("true");
    expect(document.activeElement).toBe(grouped);

    const collapse = await screen.findByRole("button", { name: "Collapse group pipeline" });
    collapse.focus();
    await user.keyboard("{Enter}");
    const expand = await screen.findByRole("button", { name: "Expand group pipeline" });
    expect(document.activeElement).toBe(expand);
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(
      await screen.findByRole("button", { name: "Collapse group pipeline" }),
    );

    const reset = screen.getByRole("button", {
      name: "Reset view preferences stored only in this browser",
    });
    reset.focus();
    await user.keyboard("{Enter}");
    await waitFor(() =>
      expect(document.querySelector(".notice-region")?.textContent).toContain(
        "View preferences reset to deterministic defaults",
      ),
    );
    expect(document.activeElement).toBe(reset);
  });

  test("traverses the complete active view and inspector in forward and reverse DOM order without a trap", async () => {
    installApi(numericPorts);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    await user.click(screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    }));
    const skip = screen.getByRole("link", { name: "Skip to dashboard view controls" });
    skip.focus();
    const forward: Element[] = [skip];
    for (let index = 0; index < 100; index += 1) {
      await user.tab();
      const active = document.activeElement;
      if (active === document.body) {
        await user.tab();
        expect(document.activeElement).toBe(skip);
        break;
      }
      if (active === skip) {
        break;
      }
      expect(active).not.toBeNull();
      forward.push(active!);
    }
    expect(document.activeElement).toBe(skip);
    expect(forward).toContain(screen.getByRole("button", { name: "Configure" }));
    expect(forward).toContain(screen.getByRole("button", { name: "Semantic topology" }));
    expect(forward).toContain(screen.getByLabelText(
      "Search identities, types, groups, endpoints, or ports",
    ));
    expect(forward).toContain(screen.getByRole("button", {
      name: "Edit parameters for node numeric-source",
    }));

    const reverse: Element[] = [];
    for (let index = 0; index < forward.length - 1; index += 1) {
      await user.tab({ shift: true });
      if (document.activeElement === document.body) {
        await user.tab({ shift: true });
      }
      reverse.push(document.activeElement!);
    }
    expect(reverse).toEqual(forward.slice(1).reverse());
    await user.tab({ shift: true });
    expect(document.activeElement).toBe(skip);
  });

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
    const nodesTab = screen.getByRole("button", { name: "Semantic topology" });
    nodesTab.focus();
    await user.keyboard("{Enter}");
    const tableSelection = screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    });
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
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    await user.click(screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    }));
    const edit = screen.getByRole("button", {
      name: "Edit parameters for node numeric-source",
    });
    edit.focus();
    await user.keyboard("{Enter}");
    const configuration = screen.getByLabelText(
      "Node configuration (JSON object)",
    );
    await waitFor(() => expect(document.activeElement).toBe(configuration));
    fireEvent.change(configuration, {
      target: { value: '{"updated":true}' },
    });
    const save = screen.getByRole("button", { name: "Save parameters" });
    await user.click(save);

    expect(
      await screen.findByText("Parameters for numeric-source updated in memory"),
    ).not.toBeNull();
    expect(api.getGraphFetches()).toBe(2);
    expect(screen.getByTestId("node-inspector").textContent).toContain(
      "numeric-source",
    );
    await waitFor(() =>
      expect(document.activeElement?.getAttribute("data-semantic-key")).toBe(
        "edit:numeric-source",
      ),
    );
    await waitFor(() =>
      expect(screen.getByTestId("revision-summary").textContent).toContain(
        "Coordinator revision 1",
      ),
    );
    expect(screen.getByTestId("revision-summary").textContent).toContain(
      "configuration dirty",
    );
    const requests = api.fetchMock.mock.calls.map(([input, init]) =>
      `${init?.method ?? "GET"} ${new URL(String(input), "http://dashboard.test").pathname}`,
    );
    const allowed = new Set([
      "GET /api/v1/graph",
      "GET /api/v1/execution/state",
      "PATCH /api/v1/nodes/numeric-source",
    ]);
    expect(requests.every((request) => allowed.has(request))).toBe(true);
    expect(requests.filter((request) => request === "GET /api/v1/graph")).toHaveLength(2);
    expect(requests.filter((request) => request === "PATCH /api/v1/nodes/numeric-source")).toHaveLength(1);
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
      screen.getAllByRole("button", { name: /Inspect exact edge:/ }),
    ).toHaveLength(2);
    for (const button of screen.getAllByRole("button", { name: /Inspect exact edge:/ })) {
      expect(button.getAttribute("aria-label")).toContain(
        button.textContent?.trim(),
      );
    }
    fireEvent.click(
      screen.getAllByRole("button", { name: /Inspect exact edge:/ })[0],
    );
    expect(selectedEdges).toEqual([projection.bundles[0].memberEdgeIds[0]]);
  });

  test("moves focus to the persistent inspector heading when an exact bundle invoker is removed", async () => {
    const model = adaptGraphDocument(groupedSplitMerge);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["parallel-stage"]),
      isolatedGroupId: null,
    });
    render(
      <>
        <h2 id="inspector-heading" tabIndex={-1}>Inspector</h2>
        <BundleInspector
          bundle={projection.bundles[0]}
          model={model}
          onSelectMemberEdge={(_edgeId, invoker) => {
            invoker.remove();
            focusPersistentInspectorHeading();
          }}
        />
      </>,
    );
    const exact = screen.getAllByRole("button", { name: /Inspect exact edge:/ });
    exact[0].focus();
    fireEvent.click(exact[0]);
    await waitFor(() =>
      expect(document.activeElement).toBe(
        screen.getByRole("heading", { name: "Inspector" }),
      ),
    );
    expect(document.activeElement?.isConnected).toBe(true);
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
    await waitFor(() =>
      expect(document.activeElement).toBe(
        document.querySelector('[data-focus-group="parallel-stage"]'),
      ),
    );
    expect(document.activeElement?.isConnected).toBe(true);
    expect(
      screen.getByRole("navigation", { name: "Group breadcrumbs" }).textContent,
    ).toContain("Processing pipeline / Parallel stage");
    const parent = screen.getByRole("button", { name: "Return to parent" });
    parent.focus();
    await user.keyboard("{Enter}");
    expect(await screen.findByText("Isolated: Processing pipeline")).not.toBeNull();
    const all = screen.getByRole("button", { name: "Return to all topology" });
    all.focus();
    await user.keyboard("{Enter}");
    expect(screen.queryByText(/Isolated:/)).toBeNull();
    await waitFor(() =>
      expect(document.activeElement).toBe(
        screen.getByRole("button", { name: "All topology" }),
      ),
    );
    expect(document.activeElement?.isConnected).toBe(true);
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

  test("keeps a grouped layout failure in one stable truthful raw fallback across views", async () => {
    const originalLayout = hierarchyLayoutModule.layoutPresentationGraph;
    const layoutSpy = vi
      .spyOn(hierarchyLayoutModule, "layoutPresentationGraph")
      .mockImplementation(async (model, hierarchy, projection, selection) =>
        originalLayout(
          model,
          hierarchy,
          projection,
          selection,
          projection.mode === "grouped"
            ? async () => {
                throw new Error("forced App compound layout failure");
              }
            : undefined,
        ),
      );
    const api = installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    expect(
      await screen.findByRole("heading", {
        name: "Presentation grouping rejected; Raw topology preserved",
      }),
    ).not.toBeNull();
    await waitFor(() => expect(layoutSpy).toHaveBeenCalledTimes(2));
    await waitFor(() =>
      expect(screen.getAllByTestId("graph-node-card")).toHaveLength(9),
    );

    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    expect(
      screen.getByRole("heading", {
        name: "Presentation grouping rejected; Raw topology preserved",
      }),
    ).not.toBeNull();
    const pipelineSummary = document.querySelector(
      '[data-semantic-key="group-disclosure:pipeline"]',
    );
    expect(pipelineSummary?.textContent).toContain("canvas raw layout fallback active");
    expect(pipelineSummary?.textContent).toContain("saved grouped preference expanded");
    expect(screen.getAllByRole("button", {
      name: /Set saved grouped preference .*: group /,
    }).length).toBeGreaterThan(0);
    expectOnlyLocalReadRequests(api.fetchMock);
    expect(layoutSpy).toHaveBeenCalledTimes(2);
  });

  test("provides the complete semantic hierarchy and edge alternative independently of canvas collapse", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));

    expect(screen.getByRole("region", { name: "Semantic topology" })).not.toBeNull();
    expect(screen.getByTestId("semantic-counts").textContent).toContain(
      "Showing 9 of 9 authoritative nodes and 9 of 9 authoritative edges",
    );
    expect(document.querySelectorAll('[data-semantic-record^="node:"]')).toHaveLength(9);
    expect(document.querySelectorAll('[data-semantic-record^="edge:"]')).toHaveLength(9);
    expect(
      screen.getByRole("button", { name: /Select authoritative node interior_1/ }),
    ).not.toBeNull();
    expect(screen.getAllByText(/parallel-stage/).length).toBeGreaterThan(0);

    const edgeButton = screen.getAllByRole("button", {
      name: /Select authoritative edge .*source_1.*name “Data”.*merge_1.*name “In0”/,
    })[0];
    await user.click(edgeButton);
    expect(edgeButton.getAttribute("aria-pressed")).toBe("true");
    expect(screen.getByTestId("edge-inspector").textContent).toContain("name “Data”");
    expect(screen.getByTestId("edge-inspector").textContent).toContain("name “In0”");
  });

  test("searches semantic node and edge identities with truthful visible/total counts", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    const outputsDisclosure = document.querySelector<HTMLElement>(
      '[data-semantic-key="group-disclosure:outputs"]',
    )!;
    const outputsDetails = outputsDisclosure.closest("details")!;
    outputsDetails.open = false;
    fireEvent(outputsDetails, new Event("toggle", { bubbles: true }));
    await waitFor(() => expect(outputsDetails.open).toBe(false));
    await user.type(
      screen.getByLabelText("Search identities, types, groups, endpoints, or ports"),
      "sink_1",
    );
    expect(screen.getByTestId("semantic-counts").textContent).toContain(
      "Showing 1 of 9 authoritative nodes and 1 of 9 authoritative edges",
    );
    expect(document.querySelectorAll('[data-semantic-record^="node:"]')).toHaveLength(1);
    expect(document.querySelectorAll('[data-semantic-record^="edge:"]')).toHaveLength(1);
    expect(outputsDetails.open).toBe(true);
    expect(
      screen.getByRole("button", { name: /Select authoritative node sink_1/ }),
    ).not.toBeNull();
    expect(screen.getByText(/Matching nodes temporarily reveal/)).not.toBeNull();
    outputsDetails.open = false;
    fireEvent(outputsDetails, new Event("toggle", { bubbles: true }));
    expect(outputsDetails.open).toBe(true);
    expect(
      screen.getByRole("button", { name: /Select authoritative node sink_1/ }),
    ).not.toBeNull();
    await user.clear(
      screen.getByLabelText("Search identities, types, groups, endpoints, or ports"),
    );
    expect(screen.getByTestId("semantic-counts").textContent).toContain(
      "Showing 9 of 9 authoritative nodes and 9 of 9 authoritative edges",
    );
    await waitFor(() => expect(outputsDetails.open).toBe(false));

    const typeFilter = screen.getByLabelText("Filter node type");
    await user.type(typeFilter, "SinkTestNode");
    expect(outputsDetails.open).toBe(true);
    expect(screen.getAllByRole("button", { name: /Select authoritative node sink_/ })).toHaveLength(2);
    await user.clear(typeFilter);
    await waitFor(() => expect(outputsDetails.open).toBe(false));
  });

  test("restores persisted closed disclosures after transient nested search reveal", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const first = render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    const outputsDisclosure = document.querySelector<HTMLElement>(
      '[data-semantic-key="group-disclosure:outputs"]',
    )!;
    const outputsDetails = outputsDisclosure.closest("details")!;
    outputsDetails.open = false;
    fireEvent(outputsDetails, new Event("toggle", { bubbles: true }));
    await waitFor(() =>
      expect(outputsDetails.open).toBe(false),
    );
    await waitFor(() => {
      const saved = JSON.parse(
        window.localStorage.getItem("graphx.dashboard.presentation") ?? "null",
      ) as { semantic_expanded_group_ids?: string[] } | null;
      expect(saved?.semantic_expanded_group_ids).not.toContain("outputs");
    });
    first.unmount();

    installApi(groupedSplitMerge);
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    const restoredDisclosure = document.querySelector<HTMLElement>(
      '[data-semantic-key="group-disclosure:outputs"]',
    )!;
    const restoredDetails = restoredDisclosure.closest("details")!;
    await waitFor(() => expect(restoredDetails.open).toBe(false));
    const search = screen.getByLabelText(
      "Search identities, types, groups, endpoints, or ports",
    );
    await user.type(search, "sink_1");
    expect(restoredDetails.open).toBe(true);
    expect(screen.getByRole("button", { name: /Select authoritative node sink_1/ })).not.toBeNull();
    await user.clear(search);
    await waitFor(() => expect(restoredDetails.open).toBe(false));
  });

  test("keeps semantic disclosure separate while canvas actions remain request-isolated", async () => {
    const api = installApi(groupedSplitMerge);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    const semanticView = screen.getByRole("button", { name: "Semantic topology" });
    semanticView.focus();
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(semanticView);
    const disclosure = document.querySelector<HTMLElement>(
      '[data-semantic-key="group-disclosure:parallel-stage"]',
    )!;
    const disclosureDetails = disclosure.closest("details")!;
    disclosureDetails.open = false;
    fireEvent(disclosureDetails, new Event("toggle", { bubbles: true }));
    await waitFor(() => expect(disclosureDetails.open).toBe(false));
    disclosureDetails.open = true;
    fireEvent(disclosureDetails, new Event("toggle", { bubbles: true }));
    await waitFor(() => expect(disclosureDetails.open).toBe(true));
    const inspect = screen.getByRole("button", {
      name: "Inspect group parallel-stage",
    });
    inspect.focus();
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(inspect);
    const collapse = screen.getByRole("button", {
      name: "Expand on canvas: group parallel-stage",
    });
    collapse.focus();
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(collapse);
    const isolate = screen.getByRole("button", {
      name: "Isolate on canvas: group parallel-stage",
    });
    isolate.focus();
    await user.keyboard("{Enter}");
    expect(document.activeElement).toBe(isolate);
    await waitFor(() =>
      expect(
        window.localStorage.getItem("graphx.dashboard.presentation"),
      ).not.toBeNull(),
    );

    expectOnlyLocalReadRequests(api.fetchMock);
    const saved = JSON.parse(
      window.localStorage.getItem("graphx.dashboard.presentation") ?? "null",
    ) as Record<string, unknown> | null;
    expect(saved).not.toBeNull();
    expect(saved).not.toHaveProperty("isolatedGroupId");
    expect(saved).not.toHaveProperty("selection");
    expect(saved).not.toHaveProperty("search");
  });

  test("traps modal Tab, closes on Escape without PATCH, and restores the invoking semantic control", async () => {
    const api = installApi(numericPorts);
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    const edit = screen.getByRole("button", {
      name: "Edit parameters for node numeric-source",
    });
    edit.focus();
    await user.keyboard("{Enter}");
    const textarea = screen.getByLabelText("Node configuration (JSON object)");
    await waitFor(() => expect(document.activeElement).toBe(textarea));
    await user.keyboard("{Tab}");
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Save parameters" }));
    await user.keyboard("{Tab}");
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Cancel" }));
    await user.keyboard("{Tab}");
    expect(document.activeElement).toBe(textarea);
    await user.keyboard("{Shift>}{Tab}{/Shift}");
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Cancel" }));
    await user.keyboard("{Shift>}{Tab}{/Shift}");
    expect(document.activeElement).toBe(screen.getByRole("button", { name: "Save parameters" }));
    await user.keyboard("{Shift>}{Tab}{/Shift}");
    expect(document.activeElement).toBe(textarea);
    await user.keyboard("{Escape}");
    expect(screen.queryByRole("dialog")).toBeNull();
    await waitFor(() => expect(document.activeElement).toBe(edit));
    expect(
      api.fetchMock.mock.calls.some(([, init]) => init?.method === "PATCH"),
    ).toBe(false);
  });

  test("reset removes local preferences and does not recreate them without a later operator view interaction", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    window.localStorage.setItem(
      "graphx.dashboard.presentation",
      JSON.stringify({ stale: true }),
    );
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", {
      name: "Reset view preferences stored only in this browser",
    }));
    await waitFor(() =>
      expect(document.querySelector(".notice-region")?.textContent).toContain(
        "View preferences reset to deterministic defaults",
      ),
    );
    expect(window.localStorage.getItem("graphx.dashboard.presentation")).toBeNull();
  });

  test("restores a valid grouped mode, collapse set, semantic disclosure set, and viewport", async () => {
    const restoredModel = adaptGraphDocument(groupedSplitMerge);
    const restoredHierarchy = adaptPresentationGroups(restoredModel);
    const signature = await graphSignature(
      restoredModel,
      restoredHierarchy,
      webcrypto as unknown as Crypto,
    );
    const record: PresentationPreferences = {
      schema: 1,
      graph_signature: signature,
      mode: "grouped",
      collapsed_group_ids: ["pipeline"],
      semantic_expanded_group_ids: ["outputs"],
      viewport: { x: -25, y: 50, zoom: 1.5 },
    };
    window.localStorage.setItem(
      "graphx.dashboard.presentation",
      JSON.stringify(record),
    );
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");
    expect(screen.getByRole("button", { name: "Grouped topology" }).getAttribute("aria-pressed")).toBe("true");
    await waitFor(() =>
      expect(container.querySelector('.react-flow__node[data-id="pipeline"] .graph-group-card.collapsed')).not.toBeNull(),
    );
    await waitFor(() =>
      expect(container.querySelector(".react-flow__viewport")?.getAttribute("style")).toContain("scale(1.5)"),
    );
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    expect(document.querySelector('[data-semantic-key="group-disclosure:outputs"]')?.closest("details")?.open).toBe(true);
    expect(document.querySelector('[data-semantic-key="group-disclosure:pipeline"]')?.closest("details")?.open).toBe(false);
  });

  test("coalesces rapid viewport changes into one bounded write with the final viewport", async () => {
    installApi(groupedSplitMerge);
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");
    await waitFor(() => expect(container.querySelector(".react-flow__viewport")).not.toBeNull());
    const setItem = vi.spyOn(window.localStorage, "setItem");
    const minimap = screen.getByTestId("minimap-keyboard-control");
    fireEvent.keyDown(minimap, { key: "ArrowRight" });
    fireEvent.keyDown(minimap, { key: "ArrowDown" });
    fireEvent.keyDown(minimap, { key: "ArrowLeft" });
    await waitFor(() => expect(setItem).toHaveBeenCalledTimes(1));
    const saved = JSON.parse(setItem.mock.calls[0][1]) as PresentationPreferences;
    expect(saved.viewport).toEqual({ x: 0, y: -72, zoom: 1 });
    expect(saved.viewport.x).toBeGreaterThanOrEqual(-10_000_000);
    expect(saved.viewport.x).toBeLessThanOrEqual(10_000_000);
    expect(saved.viewport.zoom).toBeGreaterThanOrEqual(0.1);
    expect(saved.viewport.zoom).toBeLessThanOrEqual(4);
  });

  test("restores deterministic defaults and disables retries after quota failure", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const setItem = vi
      .spyOn(window.localStorage, "setItem")
      .mockImplementation(() => {
        throw new DOMException("quota", "QuotaExceededError");
      });
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Raw topology" }));
    await waitFor(() =>
      expect(document.querySelector(".notice-region")?.textContent).toContain(
        "browser persistence is disabled for this page",
      ),
    );
    expect(setItem).toHaveBeenCalledTimes(1);
    expect(screen.getByRole("button", { name: "Grouped topology" }).getAttribute("aria-pressed")).toBe("true");
    vi.useFakeTimers();
    fireEvent.click(screen.getByRole("button", { name: "Raw topology" }));
    await act(async () => {
      vi.advanceTimersByTime(1_000);
    });
    expect(setItem).toHaveBeenCalledTimes(1);
    vi.useRealTimers();
  });

  test("uses deterministic defaults and disables persistence when storage reads are unavailable", async () => {
    installApi(groupedSplitMerge);
    const getItem = vi.spyOn(window.localStorage, "getItem").mockImplementation(() => {
      throw new DOMException("blocked", "SecurityError");
    });
    const setItem = vi.spyOn(window.localStorage, "setItem");
    render(<App />);
    await screen.findByTestId("topology-counts");
    expect(getItem).toHaveBeenCalled();
    expect(document.querySelector(".notice-region")?.textContent).toContain(
      "View preferences are unavailable; deterministic defaults are active",
    );
    expect(screen.getByRole("button", { name: "Grouped topology" }).getAttribute("aria-pressed")).toBe("true");
    vi.useFakeTimers();
    fireEvent.click(screen.getByRole("button", { name: "Raw topology" }));
    await act(async () => {
      vi.advanceTimersByTime(1_000);
    });
    expect(setItem).not.toHaveBeenCalled();
    vi.useRealTimers();
  });

  test("clears a removed refreshed selection, announces it, and focuses the semantic heading", async () => {
    installRemovingApi();
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    await user.click(screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    }));
    await user.click(screen.getByRole("button", {
      name: "Edit parameters for node numeric-source",
    }));
    await user.click(screen.getByRole("button", { name: "Save parameters" }));
    expect(document.querySelector(".notice-region")?.textContent).toContain(
      "Selected node numeric-source was removed by the graph refresh; selection cleared.",
    );
    await waitFor(() => expect(document.activeElement).toBe(screen.getByRole("heading", {
      name: "Semantic topology",
    })));
    expect(screen.queryByTestId("node-inspector")).toBeNull();
  });

  test("announces and focuses the active heading when a selected presentation group is removed", async () => {
    installPresentationRemovingApi("group");
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    await user.click(screen.getByRole("button", { name: "Inspect group parallel-stage" }));
    expect(screen.getByTestId("group-inspector").textContent).toContain("parallel-stage");
    await user.click(screen.getByRole("button", {
      name: "Edit parameters for node source_1",
    }));
    await user.click(screen.getByRole("button", { name: "Save parameters" }));
    await waitFor(() =>
      expect(document.querySelector(".notice-region")?.textContent).toContain(
        "Selected group parallel-stage was removed by the graph refresh; selection cleared.",
      ),
    );
    const heading = screen.getByRole("heading", { name: "Semantic topology" });
    await waitFor(() => expect(document.activeElement).toBe(heading));
    expect(screen.queryByTestId("group-inspector")).toBeNull();
  });

  test("detects a removed presentation bundle for the shared announcement/focus path", () => {
    const initialModel = adaptGraphDocument(groupedSplitMerge);
    const initialHierarchy = adaptPresentationGroups(initialModel);
    const state = {
      mode: "grouped" as const,
      collapsedGroupIds: new Set(["parallel-stage"]),
      isolatedGroupId: null,
    };
    const initialProjection = projectPresentation(
      initialModel,
      initialHierarchy,
      state,
    );
    const selection = {
      kind: "bundle" as const,
      id: initialProjection.bundles[0].id,
    };
    expect(
      presentationSelectionSurvivesRefresh(
        selection,
        initialModel,
        initialHierarchy,
        state,
      ),
    ).toBe(true);
    const memberIds = new Set(initialProjection.bundles[0].memberEdgeIds);
    const changedDocument = structuredClone(groupedSplitMerge) as {
      edges: Array<Record<string, unknown>>;
    };
    changedDocument.edges = changedDocument.edges.filter(
      (_edge, index) => !memberIds.has(initialModel.edges[index].id),
    );
    const changedModel = adaptGraphDocument(changedDocument);
    const changedHierarchy = adaptPresentationGroups(changedModel);
    expect(
      presentationSelectionSurvivesRefresh(
        selection,
        changedModel,
        changedHierarchy,
        state,
      ),
    ).toBe(false);
    expect(removedSelectionNotice(selection)).toBe(
      `Selected bundle ${selection.id} was removed by the graph refresh; selection cleared.`,
    );
  });

  test("focuses the persistent empty-graph heading when refresh removes the last node", async () => {
    installEmptyingApi();
    const user = userEvent.setup();
    render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    await user.click(screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    }));
    await user.click(screen.getByRole("button", {
      name: "Edit parameters for node numeric-source",
    }));
    await user.click(screen.getByRole("button", { name: "Save parameters" }));
    const emptyHeading = await screen.findByRole("heading", { name: "Empty graph" });
    await waitFor(() => expect(document.activeElement).toBe(emptyHeading));
    expect(emptyHeading.isConnected).toBe(true);
  });

  test("moves focus from a successful Retry to the surviving active-view heading", async () => {
    installRetryApi();
    const user = userEvent.setup();
    render(<App />);
    const retry = await screen.findByRole("button", { name: "Retry graph fetch" });
    retry.focus();
    await user.keyboard("{Enter}");
    await screen.findByTestId("topology-counts");
    const heading = screen.getByRole("heading", { name: "Read-only topology" });
    await waitFor(() => expect(document.activeElement).toBe(heading));
    expect(screen.queryByRole("button", { name: "Retry graph fetch" })).toBeNull();
  });

  test("keeps ungrouped nodes non-collapsible and reachable across filters and reset", async () => {
    installApi(numericPorts);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    const ungrouped = screen.getByRole("region", { name: /Ungrouped nodes — 2/ });
    expect(ungrouped.querySelector("details")).toBeNull();
    const search = screen.getByLabelText(
      "Search identities, types, groups, endpoints, or ports",
    );
    await user.type(search, "numeric-source");
    expect(container.querySelectorAll('[data-semantic-record^="node:"]')).toHaveLength(1);
    expect(screen.getByRole("button", {
      name: /Select authoritative node numeric-source/,
    })).not.toBeNull();
    await user.clear(search);
    await user.click(screen.getByRole("button", {
      name: "Reset view preferences stored only in this browser",
    }));
    expect(screen.getByRole("region", { name: /Ungrouped nodes — 2/ })).not.toBeNull();
  });

  test("has one skip link, no positive tabindex, unique landmark names, and non-color state text", async () => {
    installApi(groupedSplitMerge);
    const user = userEvent.setup();
    const { container } = render(<App />);
    await screen.findByTestId("topology-counts");
    expect(screen.getAllByRole("link", { name: "Skip to dashboard view controls" })).toHaveLength(1);
    expect(container.querySelector('[tabindex^="+"]')).toBeNull();
    expect(
      [...container.querySelectorAll("[tabindex]")].some((element) =>
        Number(element.getAttribute("tabindex")) > 0,
      ),
    ).toBe(false);
    await user.click(screen.getByRole("button", { name: "Semantic topology" }));
    for (const button of container.querySelectorAll<HTMLButtonElement>(
      "#semantic-topology-region button",
    )) {
      const visible = button.textContent?.replace(/\s+/g, " ").trim() ?? "";
      const accessible = (button.getAttribute("aria-label") ?? visible)
        .replace(/\s+/g, " ")
        .trim();
      expect(accessible).toContain(visible);
    }
    const node = screen.getByRole("button", {
      name: /Select authoritative node interior_1/,
    });
    await user.click(node);
    expect(node.closest("article")?.textContent).toContain("Selected authoritative node");
    expect(screen.getAllByRole("region", { name: "Semantic topology" })).toHaveLength(1);
    const ids = [...container.querySelectorAll("[id]")].map((element) => element.id);
    expect(new Set(ids).size).toBe(ids.length);
  });
});
