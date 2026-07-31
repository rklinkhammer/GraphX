import { readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, test } from "vitest";

const repositoryRoot = resolve(process.cwd(), "../..");
const read = (relativePath: string): string =>
  readFileSync(resolve(repositoryRoot, relativePath), "utf8");

const productionFiles = [
  "libgraph/web/src/adapter.ts",
  "libgraph/web/src/App.tsx",
  "libgraph/web/src/layout.ts",
  "libgraph/web/src/main.tsx",
  "libgraph/web/src/styles.css",
  "libgraph/web/src/types.ts",
  "libgraph/resources/web/index.html",
  "libgraph/src/graph/GraphHttpServer.cpp",
  "libgraph/include/graph/GraphHttpServer.hpp",
  "tools/graph-dashboard.cpp",
];

describe("generic dashboard architecture scan", () => {
  test("contains no parallel management or legacy dashboard dependency", () => {
    const source = productionFiles.map(read).join("\n");
    for (const forbidden of [
      "/api/v1/fhss",
      "/api/v2",
      "examples/DSP/dashboard",
      "EmbeddedDashboardServer",
      "GraphRuntimeSession",
      "ReceiverGraphCoordinator",
      "ReceiverGraphHttpServer",
      "unpkg.com",
      "cdn.jsdelivr.net",
    ]) {
      expect(source).not.toContain(forbidden);
    }
    expect(source).not.toMatch(/\bFHSS\b/);
    expect(source).not.toMatch(/\bSAR\b/);
    expect(source).not.toMatch(/\bfrequency_index\b/i);
    expect(source).not.toMatch(/\breceiver\b/i);
  });

  test("uses one graph authority and exposes no structural mutation gesture", () => {
    const app = read("libgraph/web/src/App.tsx");
    expect(app.match(/`\$\{apiBase\}\/graph`/g)).toHaveLength(1);
    expect(app.match(/\$\{apiBase\}\/nodes\//g)).toHaveLength(1);
    expect(app).toContain('method: "PATCH"');
    expect(app).toContain("nodesDraggable={false}");
    expect(app).toContain("nodesConnectable={false}");
    expect(app).toContain("edgesReconnectable={false}");
    expect(app).toContain("deleteKeyCode={null}");
    for (const forbidden of [
      "onConnect=",
      "onEdgesDelete=",
      "onNodesDelete=",
      "onReconnect=",
      "GraphExecutor",
      "GraphManager",
    ]) {
      expect(app).not.toContain(forbidden);
    }
  });

  test("keeps one entry point and a deterministic self-hosted asset inventory", () => {
    const rootCmake = read("CMakeLists.txt");
    expect(rootCmake.match(/add_executable\(graphx_graph_dashboard /g)).toHaveLength(
      1,
    );
    const index = read("libgraph/resources/web/index.html");
    expect(index).toContain('href="/assets/graphx-dashboard.css"');
    expect(index).toContain('src="/assets/graphx-dashboard.js"');
    expect(index).not.toMatch(/https?:\/\//);
    expect(
      readdirSync(resolve(repositoryRoot, "libgraph/resources/web/assets")).sort(),
    ).toEqual(["graphx-dashboard.css", "graphx-dashboard.js"]);
    expect(read("libgraph/CMakeLists.txt")).toContain(
      "install(DIRECTORY resources/web/",
    );
  });
});
