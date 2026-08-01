import { readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, test } from "vitest";

const repositoryRoot = resolve(process.cwd(), "../..");
const read = (relativePath: string): string =>
  readFileSync(resolve(repositoryRoot, relativePath), "utf8");

const productionWebFiles = readdirSync(
  resolve(repositoryRoot, "libgraph/web/src"),
  { recursive: true },
)
  .map((entry) => `libgraph/web/src/${String(entry)}`)
  .filter(
    (path) =>
      /\.(?:ts|tsx|css)$/.test(path) &&
      !path.includes(".test.") &&
      !path.includes("/test/") &&
      !path.endsWith("/setup.ts"),
  )
  .sort();

const productionFiles = [
  ...productionWebFiles,
  "libgraph/resources/web/index.html",
  "libgraph/src/graph/GraphHttpServer.cpp",
  "libgraph/include/graph/GraphHttpServer.hpp",
  "tools/graph-dashboard.cpp",
];

describe("generic dashboard architecture scan", () => {
  test("automatically inventories every production web module", () => {
    expect(productionWebFiles).toContain("libgraph/web/src/identity.ts");
    expect(productionWebFiles).toContain("libgraph/web/src/App.tsx");
    expect(productionWebFiles).not.toContain(
      "libgraph/web/src/architecture.test.ts",
    );
  });

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
      "/api/v1/groups",
      "/api/v1/topology",
      "/api/v1/preferences",
      "/api/v1/metrics",
      "command console",
      "document.cookie",
    ]) {
      expect(source).not.toContain(forbidden);
    }
    expect(source).not.toMatch(/\bFHSS\b/);
    expect(source).not.toMatch(/\bSAR\b/);
    expect(source).not.toMatch(/\bfrequency_index\b/i);
    expect(source).not.toMatch(/\bdetector\b/i);
    expect(source).not.toMatch(/\bchannelizer\b/i);
    expect(source).not.toMatch(/\bmessage schedule\b/i);
    expect(source).not.toMatch(/\bIQ metadata\b/i);
    expect(source).not.toMatch(/\breceiver\b/i);
  });

  test("keeps presentation preferences local and excludes Phase 4/server state", () => {
    const app = read("libgraph/web/src/App.tsx");
    const preferences = read("libgraph/web/src/preferences.ts");
    const server = [
      read("libgraph/src/graph/GraphHttpServer.cpp"),
      read("libgraph/include/graph/GraphHttpServer.hpp"),
    ].join("\n");
    expect(preferences).toContain('"graphx.dashboard.presentation"');
    expect(preferences).toContain('cryptoProvider.subtle.digest("SHA-256"');
    expect(preferences).not.toContain("fetch(");
    expect(server).not.toContain("graphx.dashboard.presentation");
    expect(app.match(/`\$\{apiBase\}\/graph`/g)).toHaveLength(1);
    expect(app).not.toContain("document.cookie");
    expect(app).not.toContain("/metrics");
  });

  test("declares focused keyboard, reflow, forced-color, and reduced-motion protections", () => {
    const app = read("libgraph/web/src/App.tsx");
    const styles = read("libgraph/web/src/styles.css");
    expect(app).toContain("Skip to dashboard view controls");
    expect(app).not.toMatch(/tabIndex=\{?[1-9]/);
    expect(styles).toContain("@media (max-width: 480px)");
    expect(styles).toContain("@media (forced-colors: active)");
    expect(styles).toContain("@media (prefers-reduced-motion: reduce)");
    expect(styles).toContain("outline: 3px solid");
  });

  test("uses one graph authority and exposes no structural mutation gesture", () => {
    const app = read("libgraph/web/src/App.tsx");
    expect(app.match(/`\$\{apiBase\}\/graph`/g)).toHaveLength(1);
    expect(app.match(/\$\{apiBase\}\/nodes\//g)).toHaveLength(1);
    expect(app).toContain('method: "PATCH"');
    expect(app).toContain(
      "body: JSON.stringify({ node_config: nodeConfig })",
    );
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
      "collapsedGroupIds })",
      "isolatedGroupId })",
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
