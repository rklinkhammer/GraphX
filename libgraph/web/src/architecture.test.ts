import { existsSync, readdirSync, readFileSync } from "node:fs";
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

function productionSources(relativeDirectory: string): string[] {
  const root = resolve(repositoryRoot, relativeDirectory);
  if (!existsSync(root)) return [];
  return readdirSync(root, { recursive: true })
    .map((entry) => `${relativeDirectory}/${String(entry)}`)
    .filter((path) => /\.(?:hpp|cpp)$/.test(path) &&
      !path.includes("/test/") && !path.includes("generated") &&
      !path.includes("vendor"))
    .sort();
}

const productionManagementFiles = [
  ...productionSources("libgraph/include"),
  ...productionSources("libgraph/src"),
  ...productionSources("tools"),
].sort();

const productionAuthorityFiles = [
  ...productionSources("libgraph/include/capabilities"),
  ...productionSources("libgraph/src/capabilities"),
  ...productionSources("libgraph/include/policies"),
  ...productionSources("libgraph/src/policies"),
  ...productionSources("libgraph/include/metrics"),
  ...productionSources("libgraph/src/metrics"),
  ...productionSources("libgraph/include/graph").filter((path) =>
    /\/(?:GraphCoordinator|GraphExecutor(?:Builder)?|GraphManagerCore|GraphHttpServer|GraphCli)\.hpp$/.test(path)),
  ...productionSources("libgraph/src/graph").filter((path) =>
    /\/(?:GraphCoordinator|GraphExecutor(?:Builder)?|GraphManagerCore|GraphHttpServer|GraphCli)\.cpp$/.test(path)),
];

const activeGraphAuthorityFiles = [
  ...productionSources("libgraph/include/graph")
    .filter((path) => !path.includes("/graph/dashboard/")),
  ...productionSources("libgraph/src/graph"),
  ...productionSources("tools"),
  "CMakeLists.txt",
  "libgraph/CMakeLists.txt",
].sort();

const productionFiles = [...new Set([
  ...productionWebFiles,
  ...productionAuthorityFiles,
  "libgraph/resources/web/index.html",
  "libgraph/include/capabilities/CommandCapability.hpp",
  "libgraph/include/capabilities/MetricsCapability.hpp",
  "libgraph/include/graph/GraphCli.hpp",
  "libgraph/include/graph/GraphExecutor.hpp",
  "libgraph/include/graph/GraphManagerCore.hpp",
  "libgraph/include/metrics/IMetricsSubscriber.hpp",
  "libgraph/include/metrics/MetricsEvent.hpp",
  "libgraph/include/metrics/NodeMetricsSchema.hpp",
  "libgraph/src/graph/GraphHttpServer.cpp",
  "libgraph/include/graph/GraphHttpServer.hpp",
  "libgraph/include/policies/MetricsPolicy.hpp",
  "libgraph/src/capabilities/CommandCapability.cpp",
  "libgraph/src/graph/GraphCli.cpp",
  "libgraph/src/graph/GraphExecutor.cpp",
  "libgraph/src/policies/MetricsPolicy.cpp",
  "tools/graph-dashboard.cpp",
  "tools/graph-cli.cpp",
])].sort();

const stripCppComments = (source: string): string =>
  source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");

type ClassDeclaration = { name: string; body: string };

function classDeclarations(source: string): ClassDeclaration[] {
  const declarations: ClassDeclaration[] = [];
  const declaration = /\b(?:class|struct)\s+([A-Za-z_]\w*(?:::\w+)*)[^;{]*\{/g;
  for (const match of source.matchAll(declaration)) {
    const bodyStart = (match.index ?? 0) + match[0].length;
    let depth = 1;
    let cursor = bodyStart;
    while (cursor < source.length && depth > 0) {
      if (source[cursor] === "{") depth += 1;
      if (source[cursor] === "}") depth -= 1;
      cursor += 1;
    }
    if (depth === 0) {
      declarations.push({ name: match[1], body: source.slice(bodyStart, cursor - 1) });
    }
  }
  return declarations;
}

function managementAuthorityDeclarations(source: string): string[] {
  return classDeclarations(stripCppComments(source)).filter(({ name, body }) => {
    const lifecycle = ["Init", "Start", "Run", "Stop", "Join"]
      .filter((method) => new RegExp(`\\b${method}\\s*\\(`).test(body)).length;
    const coordinatesConfiguration = /\bSnapshot\s*\(/.test(body) &&
      /\b(?:UpdateNodeConfig|ConfigureGraph)\s*\(/.test(body);
    const servesCommands = /\bHandleRequest\s*\(/.test(body) &&
      /\bStart\s*\(/.test(body) && /\bStop\s*\(/.test(body);
    const ownsExecutionLifecycle = lifecycle >= 4 &&
      /\b(?:ExecutionResult|AddNode)\b/.test(body);
    const configuresLifecycle = lifecycle >= 5 &&
      /\bConfigure(?:Graph)?\s*\(/.test(body);
    const ownName = name.split("::").at(-1);
    const ownedAuthorities = new Set((body.match(
      /\b(?:GraphCoordinator|GraphExecutor|GraphHttpServer)\b/g) ?? [])
      .filter((authority) => authority !== ownName)).size;
    return ownsExecutionLifecycle || configuresLifecycle ||
      coordinatesConfiguration || servesCommands || ownedAuthorities >= 2;
  }).map(({ name }) => name);
}

function managementExecutableTargets(
  cmakeFiles: ReadonlyArray<{ path: string; source: string }>,
  sourceLookup: (cmakePath: string, sourcePath: string) => string,
): string[] {
  const targets: string[] = [];
  for (const cmake of cmakeFiles) {
    for (const match of cmake.source.matchAll(
      /add_executable\(\s*([^\s)]+)\s+([^)]+)\)/g,
    )) {
      const sources = match[2].match(/[^\s"']+\.(?:cpp|cc|cxx)/g) ?? [];
      const body = sources.map((path) => sourceLookup(cmake.path, path)).join("\n");
      if (/\bGraphHttpServer\b/.test(body) ||
          (/\bGraphCoordinator\b/.test(body) &&
           /\bGraphExecutorBuilder\b/.test(body))) {
        targets.push(match[1]);
      }
    }
  }
  return targets.sort();
}

function repositoryFiles(directory = repositoryRoot, prefix = ""): string[] {
  const ignored = new Set([
    ".git", "node_modules", "build", "build-ninja", "build-coverage",
    "Testing", ".cache",
  ]);
  const result: string[] = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (entry.isDirectory() && (ignored.has(entry.name) ||
        entry.name.startsWith("build-") || entry.name.startsWith("cmake-build-"))) {
      continue;
    }
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      result.push(...repositoryFiles(resolve(directory, entry.name), relative));
    } else {
      result.push(relative);
    }
  }
  return result;
}

const allCmakeFiles = repositoryFiles()
  .filter((path) => path.endsWith("CMakeLists.txt"))
  .sort();
const activeManagementCmakeFiles = allCmakeFiles.filter((path) =>
  !/(?:^|\/)(?:test|tests|vendor|generated|archive)(?:\/|$)/.test(path));

const callbackForbiddenWork =
  /(?:HandleRequest|JsonResponse|SendAll|SendHttp|::(?:socket|send)\s*\(|\.dump\s*\(|metrics_->|LOG4CXX|layout|history)/;

describe("generic dashboard architecture scan", () => {
  test("automatically inventories every production web module", () => {
    expect(productionWebFiles).toContain("libgraph/web/src/identity.ts");
    expect(productionWebFiles).toContain("libgraph/web/src/App.tsx");
    expect(productionWebFiles).not.toContain(
      "libgraph/web/src/architecture.test.ts",
    );
  });

  test("automatically inventories every generic management authority surface", () => {
    for (const required of [
      "libgraph/include/graph/GraphCoordinator.hpp",
      "libgraph/src/graph/GraphCoordinator.cpp",
      "libgraph/include/graph/GraphExecutorBuilder.hpp",
      "libgraph/src/graph/GraphExecutorBuilder.cpp",
      "libgraph/include/graph/GraphManagerCore.hpp",
      "libgraph/include/policies/CommandPolicy.hpp",
      "libgraph/include/metrics/IMetricsCallback.hpp",
    ]) {
      expect(productionAuthorityFiles, required).toContain(required);
    }
    expect(productionFiles).toContain("tools/graph-cli.cpp");
    expect(productionManagementFiles).toContain(
      "libgraph/include/capabilities/MetricsCapability.hpp",
    );
    expect(productionManagementFiles).toContain(
      "libgraph/src/policies/MetricsPolicy.cpp",
    );
    expect(productionManagementFiles).toContain(
      "libgraph/include/metrics/IMetricsSubscriber.hpp",
    );
    expect(productionManagementFiles).toContain("tools/graph-dashboard.cpp");
    expect(productionManagementFiles.some((path) =>
      /(?:^|\/)(?:test|tests|vendor|generated)(?:\/|$)/.test(path))).toBe(false);
  });

  test("structurally inventories one active server, coordinator, executor, runtime authority, and dashboard entry point", () => {
    const source = activeGraphAuthorityFiles.map((path) =>
      /\.(?:cpp|hpp)$/.test(path) ? stripCppComments(read(path)) : read(path)).join("\n");
    const definedAuthorities = [...source.matchAll(
      /\bclass\s+([A-Za-z_]\w*(?:Coordinator|HttpServer|Executor|RuntimeOwner|RuntimeSession|Manager))\s*(?:final\s*)?(?::(?!:)[^;{]+)?\{/g,
    )].map((match) => match[1]).sort();
    expect(definedAuthorities).toEqual([
      "GraphCoordinator", "GraphExecutor", "GraphHttpServer", "GraphManager",
      "SimpleHttpServer",
    ]);
    expect(source.match(/\bclass\s+GraphCoordinator\s*\{/g)).toHaveLength(1);
    expect(source.match(/\bclass\s+GraphExecutor\s*\{/g)).toHaveLength(1);
    expect(source.match(/\bclass\s+GraphHttpServer\s*:(?!:)/g)).toHaveLength(1);
    expect(source.match(/add_executable\(graphx_graph_dashboard\b/g)).toHaveLength(1);
    expect(read("tools/graph-dashboard.cpp")).not.toMatch(
      /graph\/dashboard\/|GraphRuntimeSession|EmbeddedDashboardServer/,
    );
    expect(activeGraphAuthorityFiles.some((path) =>
      path.includes("libgraph/include/graph/dashboard/"))).toBe(false);
    const sourceByFile = new Map(activeGraphAuthorityFiles.map((path) => [
      path, /\.(?:cpp|hpp)$/.test(path) ? stripCppComments(read(path)) : read(path),
    ]));
    expect([...sourceByFile].filter(([, text]) =>
      /::(?:socket|bind|listen|accept)\s*\(/.test(text)).map(([path]) => path))
      .toEqual(["libgraph/src/graph/GraphHttpServer.cpp"]);
    expect([...sourceByFile].filter(([, text]) =>
      /GraphCoordinator::UpdateNodeConfig\s*\(/.test(text)).map(([path]) => path))
      .toEqual(["libgraph/src/graph/GraphCoordinator.cpp"]);
    expect([...sourceByFile].filter(([, text]) =>
      /GraphExecutor::Init\s*\(/.test(text)).map(([path]) => path))
      .toEqual(["libgraph/src/graph/GraphExecutor.cpp"]);
    expect(source.match(/add_executable\([^\n]*dashboard[^\n]*/gi))
      .toEqual(["add_executable(graphx_graph_dashboard tools/graph-dashboard.cpp)"]);
  });

  test("detects management authority and entry points by behavior, not names", () => {
    const authorityByFile = productionManagementFiles
      .flatMap((path) => managementAuthorityDeclarations(read(path))
        .map((name) => `${path}:${name}`));
    expect(authorityByFile).toEqual([
      "libgraph/include/graph/GraphCli.hpp:GraphCli",
      "libgraph/include/graph/GraphCoordinator.hpp:GraphCoordinator",
      "libgraph/include/graph/GraphExecutor.hpp:GraphExecutor",
      "libgraph/include/graph/GraphManagerCore.hpp:GraphManager",
      // Grandfathered legacy server is compiled only when the explicit legacy
      // option is enabled; the generic entry point import prohibition below
      // proves it is not a second authority in graphx-dashboard.
      "libgraph/include/graph/dashboard/EmbeddedDashboardServer.hpp:EmbeddedDashboardServer",
      "libgraph/src/graph/GraphHttpServer.cpp:GraphHttpServer::Impl",
    ]);

    expect(activeManagementCmakeFiles).toContain("CMakeLists.txt");
    expect(activeManagementCmakeFiles).toContain("libgraph/CMakeLists.txt");
    expect(activeManagementCmakeFiles).toContain(
      "tools/package_smoke_consumer/CMakeLists.txt",
    );
    expect(activeManagementCmakeFiles).not.toContain(
      "libgraph/test/CMakeLists.txt",
    );
    const activeCmake = activeManagementCmakeFiles.map((path) =>
      ({ path, source: read(path) }));
    expect(managementExecutableTargets(activeCmake, (cmakePath, path) => {
      const base = cmakePath.includes("/")
        ? cmakePath.slice(0, cmakePath.lastIndexOf("/") + 1) : "";
      const candidate = `${base}${path}`;
      return existsSync(resolve(repositoryRoot, candidate)) ? read(candidate) :
        existsSync(resolve(repositoryRoot, path)) ? read(path) : "";
    }))
      .toEqual(["graphx_graph_dashboard"]);

    // Mutation sentinels prove differently named parallel authorities and
    // executables cannot evade the inventory by avoiding GraphX/dashboard names.
    expect(managementAuthorityDeclarations(`
      class ControlHub {
       public:
        bool Configure();
        bool Init();
        bool Start();
        bool Run();
        bool Stop();
        bool Join();
      };
    `)).toEqual(["ControlHub"]);
    const mutatedCmake = [...activeCmake, {
      path: "examples/alternate/CMakeLists.txt",
      source: "add_executable(alternate_ui control-ui.cpp)",
    }];
    expect(managementExecutableTargets(mutatedCmake, (cmakePath, path) =>
      cmakePath === "examples/alternate/CMakeLists.txt" && path === "control-ui.cpp"
        ? "GraphCoordinator coordinator; GraphHttpServer server;"
        : existsSync(resolve(repositoryRoot, path)) ? read(path) : ""))
      .toEqual(["alternate_ui", "graphx_graph_dashboard"]);
  });

  test("contains no parallel management or legacy dashboard dependency", () => {
    const source = productionFiles.map((path) =>
      /\.(?:cpp|hpp)$/.test(path) ? stripCppComments(read(path)) : read(path)).join("\n");
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

  test("locks out streams, shell execution, server export, new images, and CLI removal", () => {
    const production = productionFiles.map((path) =>
      /\.(?:cpp|hpp)$/.test(path) ? stripCppComments(read(path)) : read(path)).join("\n");
    expect(production).not.toMatch(/\/api\/v1\/(?:events|stream|export)\b/);
    expect(production).not.toMatch(/\b(?:EventSource|WebSocket)\b/);
    expect(production).not.toMatch(
      /\b(?:system|popen|fork|exec[lvpe]*|posix_spawn|ShellExecute|child_process)\s*(?:\.|\()/,
    );
    const serverWritableProduction = productionFiles
      .filter((path) => path !== "libgraph/src/graph/GraphCli.cpp")
      .map((path) => /\.(?:cpp|hpp)$/.test(path) ? stripCppComments(read(path)) : read(path))
      .join("\n");
    expect(serverWritableProduction).not.toMatch(
      /\b(?:ofstream|fopen|fwrite|writeFile|createWriteStream)\b/,
    );
    expect(stripCppComments(read("libgraph/src/graph/GraphCli.cpp"))
      .match(/\b(?:ofstream|fopen|fwrite)\b/g)).toEqual(["ofstream"]);
    const remoteUrls = production.match(/https?:\/\/[^\s"'`)]+/g) ?? [];
    expect(remoteUrls.filter((url) =>
      !/^http:\/\/(?:127\.0\.0\.1|localhost)(?::\d*)?(?:\/|$)/.test(url),
    )).toEqual([]);
    const dockerArtifacts = repositoryFiles()
      .filter((path) => /(?:^|\/)(?:Dockerfile[^/]*|compose\.ya?ml|docker-compose[^/]*\.ya?ml)$/.test(path))
      .sort();
    expect(dockerArtifacts).toEqual([
      ".devcontainer/Dockerfile",
      "containers/dashboard-operator/Dockerfile",
      "containers/dashboard-operator/compose.yaml",
      "containers/sanitizers/Dockerfile",
      "containers/sanitizers/compose.yaml",
    ]);
    const cmake = read("CMakeLists.txt");
    expect(cmake).toContain("add_executable(graphx_graph_cli");
    expect(cmake).toContain("OUTPUT_NAME graph-cli");
    expect(cmake).toMatch(/install\(TARGETS\s+graphx_graph_cli\b/);
  });

  test("keeps the metrics subscriber callback off I/O and authority re-entry paths", () => {
    const server = stripCppComments(read("libgraph/src/graph/GraphHttpServer.cpp"));
    const callback = server.slice(
      server.indexOf("void OnMetricsEvent(const app::metrics::MetricsEvent& event) noexcept"),
      server.indexOf("void OnMetricsGenerationReset"),
    );
    const accept = server.slice(
      server.indexOf("void AcceptMetricsEvent("),
      server.indexOf("HttpResponse HandleMetrics"),
    );
    expect(callback).toContain("AcceptMetricsEvent(event, observation)");
    expect(callback).toContain("MetricsCallbackScope callback_scope(observation)");
    expect(`${callback}\n${accept}`).not.toMatch(callbackForbiddenWork);
    expect(`${callback}\n${accept}\nSendAll(-1, {});`)
      .toMatch(callbackForbiddenWork);
    expect(`${callback}\n${accept}\n::send(fd, data, size, 0);`)
      .toMatch(callbackForbiddenWork);
    expect(`${callback}\n${accept}\nnlohmann::json(event).dump();`)
      .toMatch(callbackForbiddenWork);
    expect(accept).toContain("std::scoped_lock lock(metrics_mutex_)");
    expect(accept).toContain("ValidateEventContract(event)");
    expect(server).toContain("ObserveCallbackBoundary(&MetricsCallbackObservation::socket_operations)");
    expect(server).toContain("ObserveCallbackBoundary(&MetricsCallbackObservation::http_responses)");
    expect(server).toContain("ObserveCallbackBoundary(&MetricsCallbackObservation::json_serializations)");
    expect(server).toContain("MetricsCallbackObservation::capability_reentries");
    expect(`${callback}\n${accept}`).not.toMatch(/LOG4CXX|layout|history/);
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
    expect(app).toContain(
      '!metricsPaused && !metricsBrowserStale && metrics?.availability.state === "available"',
    );
    expect(read("libgraph/src/graph/GraphHttpServer.cpp")).toContain(
      'path == "/api/v1/metrics"',
    );
    expect(read("libgraph/web/src/runtime.ts")).toContain(
      'fetch("/api/v1/metrics"',
    );
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
