import { spawn } from "node:child_process";
import { mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import process from "node:process";

import puppeteer from "puppeteer-core";

function argumentsByName(argv) {
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    values.set(argv[index], argv[index + 1]);
  }
  return values;
}

const options = argumentsByName(process.argv.slice(2));
const dashboard = options.get("--dashboard");
const graph = options.get("--graph");
const port = Number(options.get("--port"));
const expectedNodes = Number(options.get("--expected-nodes"));
const expectedEdges = Number(options.get("--expected-edges"));
const screenshot = options.get("--screenshot");
const mode = options.get("--mode") ?? "interactive";
const firefox =
  process.env.GRAPHX_FIREFOX_EXECUTABLE ??
  "/Applications/Firefox.app/Contents/MacOS/firefox";

if (
  !dashboard ||
  !graph ||
  !Number.isSafeInteger(port) ||
  !Number.isSafeInteger(expectedNodes) ||
  !Number.isSafeInteger(expectedEdges) ||
  !screenshot
) {
  throw new Error(
    "required: --dashboard PATH --graph PATH --port PORT " +
      "--expected-nodes COUNT --expected-edges COUNT --screenshot PATH",
  );
}

const server = spawn(
  resolve(dashboard),
  ["--graph", resolve(graph), "--port", String(port)],
  { stdio: ["ignore", "pipe", "pipe"] },
);
let serverOutput = "";
server.stdout.on("data", (data) => {
  serverOutput += data;
});
server.stderr.on("data", (data) => {
  serverOutput += data;
});

async function waitForServer() {
  const deadline = Date.now() + 12_000;
  while (Date.now() < deadline) {
    if (server.exitCode !== null) {
      throw new Error(
        `dashboard exited before readiness (${server.exitCode})\n${serverOutput}`,
      );
    }
    try {
      const response = await fetch(`http://127.0.0.1:${port}/`);
      if (response.ok) {
        return;
      }
    } catch {
      // The process is still starting.
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 50));
  }
  throw new Error(`dashboard did not become ready\n${serverOutput}`);
}

async function stopServer() {
  if (server.exitCode !== null) {
    return;
  }
  server.kill("SIGTERM");
  await Promise.race([
    new Promise((resolveExit) => server.once("exit", resolveExit)),
    new Promise((_, reject) =>
      setTimeout(() => reject(new Error("dashboard shutdown timed out")), 5000),
    ),
  ]);
  if (server.exitCode !== 0) {
    throw new Error(
      `dashboard shutdown returned ${server.exitCode}\n${serverOutput}`,
    );
  }
}

function requireCondition(condition, detail) {
  if (!condition) {
    throw new Error(detail);
  }
}

async function clickButton(page, accessibleName) {
  const clicked = await page.evaluate((name) => {
    const button = [...document.querySelectorAll("button")].find(
      (candidate) =>
        candidate.getAttribute("aria-label") === name ||
        candidate.textContent?.trim() === name,
    );
    if (!(button instanceof HTMLButtonElement)) {
      return false;
    }
    button.click();
    return true;
  }, accessibleName);
  requireCondition(clicked, `button not found: ${accessibleName}`);
}

async function activateButtonWithKeyboard(page, accessibleName) {
  await page.waitForFunction(
    (name) => {
      const button = [...document.querySelectorAll("button")].find(
        (candidate) =>
          candidate.getAttribute("aria-label") === name ||
          candidate.textContent?.trim() === name,
      );
      if (
        !(button instanceof HTMLButtonElement) ||
        button.disabled ||
        !button.isConnected
      ) {
        return false;
      }
      button.focus();
      return document.activeElement === button;
    },
    { polling: "raf", timeout: 5_000 },
    accessibleName,
  );
  await page.keyboard.press("Enter");
}

async function selectCanvasNodeWithKeyboard(page, nodeId) {
  const maximumAttempts = 5;
  for (let attempt = 1; attempt <= maximumAttempts; attempt += 1) {
    await page.waitForFunction(
      (id) => {
        const node = [...document.querySelectorAll(".react-flow__node")].find(
          (candidate) => candidate.getAttribute("data-id") === id,
        );
        const button = node?.querySelector(".node-keyboard-select");
        if (!(button instanceof HTMLButtonElement) || !button.isConnected) {
          return false;
        }
        button.focus();
        return button.isConnected && document.activeElement === button;
      },
      { polling: "raf", timeout: 5_000 },
      nodeId,
    );

    await page.keyboard.press("Enter");
    try {
      await page.waitForFunction(
        (id) =>
          document
            .querySelector('[data-testid="node-inspector"]')
            ?.textContent?.includes(id),
        { polling: "raf", timeout: 1_000 },
        nodeId,
      );
      return;
    } catch {
      // ELK may have replaced the focused node between focus and Enter.
      // Reacquire the current instance; never reuse a detached element.
    }
  }
  throw new Error(
    `canvas node could not retain keyboard focus: ${nodeId} ` +
      `after ${maximumAttempts} attempts`,
  );
}

async function selectCanvasEdgeWithKeyboard(page, key) {
  const maximumAttempts = 5;
  for (let attempt = 1; attempt <= maximumAttempts; attempt += 1) {
    await page.waitForFunction(
      () => {
        const edge = document.querySelector(".react-flow__edge");
        if (!(edge instanceof SVGElement) || !edge.isConnected) {
          return false;
        }
        edge.focus();
        return edge.isConnected && document.activeElement === edge;
      },
      { polling: "raf", timeout: 5_000 },
    );
    const ariaLabel = await page.evaluate(
      () => document.activeElement?.getAttribute("aria-label") ?? "",
    );
    await page.keyboard.press(key);
    try {
      await page.waitForSelector('[data-testid="edge-inspector"]', {
        timeout: 1_000,
      });
      const inspector = await page.$eval(
        '[data-testid="edge-inspector"]',
        (element) => element.textContent ?? "",
      );
      return { ariaLabel, inspector };
    } catch {
      // Reacquire if layout or selection replaced the focused SVG element.
    }
  }
  throw new Error(
    `canvas edge could not retain keyboard focus for ${key} ` +
      `after ${maximumAttempts} attempts`,
  );
}

let browser;
try {
  await waitForServer();
  browser = await puppeteer.launch({
    browser: "firefox",
    executablePath: firefox,
    headless: true,
    args: ["--width=1440", "--height=1000"],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1440, height: 1000, deviceScaleFactor: 1 });
  const consoleErrors = [];
  page.on("console", (entry) => {
    if (entry.type() === "error") {
      consoleErrors.push(entry.text());
    }
  });
  page.on("pageerror", (error) => consoleErrors.push(error.message));
  let graphFetches = 0;
  page.on("request", (request) => {
    if (new URL(request.url()).pathname === "/api/v1/graph") {
      graphFetches += 1;
    }
  });

  await page.goto(`http://127.0.0.1:${port}/`, {
    waitUntil: "networkidle0",
    timeout: 20_000,
  });
  try {
    await page.waitForSelector('[data-testid="topology-counts"]', {
      timeout: 20_000,
    });
  } catch (error) {
    const body = await page.$eval("body", (element) => element.textContent ?? "");
    throw new Error(
      `${error.message}\nbody: ${body}\nconsole: ${consoleErrors.join("\n")}`,
    );
  }
  const inventory = await page.$eval(
    '[data-testid="topology-counts"]',
    (element) => element.textContent ?? "",
  );
  requireCondition(
    inventory.includes(`${expectedNodes} nodes`) &&
      inventory.includes(`${expectedEdges} edges`),
    `unexpected authoritative inventory: ${inventory}`,
  );

  const assetContracts = await page.evaluate(async () => {
    const paths = [
      "/assets/graphx-dashboard.js",
      "/assets/graphx-dashboard.css",
    ];
    return Promise.all(
      paths.map(async (path) => {
        const response = await fetch(path);
        return [path, response.status, response.headers.get("content-type")];
      }),
    );
  });
  requireCondition(
    JSON.stringify(assetContracts) ===
      JSON.stringify([
        [
          "/assets/graphx-dashboard.js",
          200,
          "application/javascript; charset=utf-8",
        ],
        ["/assets/graphx-dashboard.css", 200, "text/css; charset=utf-8"],
      ]),
    `asset contract mismatch: ${JSON.stringify(assetContracts)}`,
  );

  if (mode === "diagnostic") {
    await page.waitForSelector(".diagnostic-panel", { timeout: 10_000 });
    const diagnostic = await page.$eval(
      ".diagnostic-panel",
      (element) => element.textContent ?? "",
    );
    requireCondition(
      diagnostic.includes("Topology cannot be drawn faithfully") &&
        diagnostic.includes("Semantic raw topology fallback"),
      "malformed topology did not retain its visible semantic fallback",
    );
  } else if (expectedNodes === 0) {
    await page.waitForSelector(".empty-state", { timeout: 10_000 });
  } else {
    await page.waitForFunction(
      (nodeCount) =>
        document.querySelectorAll(".react-flow__node").length === nodeCount,
      { timeout: 30_000 },
      expectedNodes,
    );
    await page.waitForFunction(
      (edgeCount) =>
        document.querySelectorAll(".react-flow__edge").length === edgeCount,
      { timeout: 30_000 },
      expectedEdges,
    );
    const rendered = await page.evaluate(() => ({
      nodes: document.querySelectorAll(".react-flow__node").length,
      edges: document.querySelectorAll(".react-flow__edge").length,
      handles: document.querySelectorAll(".react-flow__handle").length,
      mutationControls: [...document.querySelectorAll("button")].filter(
        (button) =>
          /add node|delete node|connect edge|reconnect edge/i.test(
            button.textContent ?? "",
          ),
      ).length,
    }));
    requireCondition(
      rendered.nodes === expectedNodes && rendered.edges === expectedEdges,
      `rendered count mismatch: ${JSON.stringify(rendered)}`,
    );
    requireCondition(rendered.handles > 0 || expectedEdges === 0, "ports did not render");
    requireCondition(
      rendered.mutationControls === 0,
      "structural mutation control was exposed",
    );
    requireCondition(graphFetches === 1, `initial graph fetch count was ${graphFetches}`);

    const viewportSelector = ".react-flow__viewport";
    const initialTransform = await page.$eval(
      viewportSelector,
      (element) => element.getAttribute("style"),
    );
    await clickButton(page, "Zoom in topology");
    await new Promise((resolveWait) => setTimeout(resolveWait, 250));
    const zoomedTransform = await page.$eval(
      viewportSelector,
      (element) => element.getAttribute("style"),
    );
    requireCondition(
      initialTransform !== zoomedTransform,
      "zoom-in did not change the viewport transform",
    );
    await activateButtonWithKeyboard(page, "Fit topology to view");
    await page.waitForFunction(
      (previous) =>
        document.querySelector(".react-flow__viewport")?.getAttribute("style") !==
        previous,
      {},
      zoomedTransform,
    );
    await clickButton(page, "Zoom out topology");

    const canvas = await page.$(".topology-canvas");
    await canvas?.evaluate((element) =>
      element.scrollIntoView({ block: "center", inline: "center" }),
    );
    const box = await canvas?.boundingBox();
    requireCondition(Boolean(box), "topology canvas has no browser bounds");
    const beforePan = await page.$eval(
      viewportSelector,
      (element) => element.getAttribute("style"),
    );
    await page.mouse.move(box.x + 18, box.y + 18);
    await page.mouse.down();
    await page.mouse.move(box.x + 98, box.y + 63, {
      steps: 6,
    });
    await page.mouse.up();
    const afterPan = await page.$eval(
      viewportSelector,
      (element) => element.getAttribute("style"),
    );
    requireCondition(beforePan !== afterPan, "pan did not change viewport transform");
    await activateButtonWithKeyboard(
      page,
      "Reset deterministic topology layout",
    );
    await page.waitForFunction(
      (previous) =>
        document.querySelector(".react-flow__viewport")?.getAttribute("style") !==
        previous,
      {},
      afterPan,
    );
    await page.waitForFunction(
      (edgeCount) =>
        document.querySelectorAll(".react-flow__edge").length === edgeCount,
      { timeout: 30_000 },
      expectedEdges,
    );

    const graphResponse = await (
      await fetch(`http://127.0.0.1:${port}/api/v1/graph`)
    ).json();
    const firstNodeId = graphResponse.data.nodes[0].id;
    const firstNodeType = graphResponse.data.nodes[0].type;
    await selectCanvasNodeWithKeyboard(page, firstNodeId);
    const nodeClicked = await page.evaluate((id) => {
      const node = [...document.querySelectorAll(".react-flow__node")].find(
        (candidate) => candidate.getAttribute("data-id") === id,
      );
      if (!(node instanceof HTMLElement)) {
        return false;
      }
      node.click();
      return true;
    }, firstNodeId);
    requireCondition(nodeClicked, `canvas node missing: ${firstNodeId}`);

    if (expectedEdges > 0) {
      await page.waitForFunction(
        (edgeCount) =>
          document.querySelectorAll(".react-flow__edge").length === edgeCount,
        { timeout: 30_000 },
        expectedEdges,
      );
      await clickButton(page, "Clear topology selection");
      await page.waitForFunction(
        () => !document.querySelector('[data-testid="edge-inspector"]'),
      );
      const enterEdge = await selectCanvasEdgeWithKeyboard(page, "Enter");
      requireCondition(
        enterEdge.ariaLabel.includes("Edge from") &&
          enterEdge.ariaLabel.includes("Press Enter or Space to select"),
        `edge semantic label was incomplete: ${enterEdge.ariaLabel}`,
      );
      requireCondition(
        enterEdge.inspector.includes("Source node") &&
          enterEdge.inspector.includes("Target port"),
        "Enter did not populate the shared edge inspector",
      );
      await clickButton(page, "Clear topology selection");
      await page.waitForFunction(
        () => !document.querySelector('[data-testid="edge-inspector"]'),
      );
      const spaceEdge = await selectCanvasEdgeWithKeyboard(page, " ");
      requireCondition(
        spaceEdge.inspector.includes("Source port") &&
          spaceEdge.inspector.includes("Target node"),
        "Space did not populate the shared edge inspector",
      );
      await clickButton(page, "Clear topology selection");
      await page.waitForFunction(
        () => !document.querySelector('[data-testid="edge-inspector"]'),
      );
      await page.waitForFunction(
        (edgeCount) =>
          document.querySelectorAll(".react-flow__edge").length === edgeCount,
        { polling: "raf", timeout: 5_000 },
        expectedEdges,
      );
      await page.click(".react-flow__edge");
      await page.waitForSelector('[data-testid="edge-inspector"]');
      const edgeInspector = await page.$eval(
        '[data-testid="edge-inspector"]',
        (element) => element.textContent ?? "",
      );
      requireCondition(
        edgeInspector.includes("Source node") &&
          edgeInspector.includes("Source port") &&
          edgeInspector.includes("Target node") &&
          edgeInspector.includes("Target port"),
        "edge inspector omitted an exact endpoint field",
      );
    }

    const nodesTabFocused = await page.evaluate(() => {
      const button = [...document.querySelectorAll("button")].find(
        (candidate) => candidate.textContent?.trim() === "Nodes & parameters",
      );
      if (!(button instanceof HTMLButtonElement)) {
        return false;
      }
      button.focus();
      return document.activeElement === button;
    });
    requireCondition(nodesTabFocused, "view switch did not receive keyboard focus");
    await page.keyboard.press("Enter");
    await page.waitForSelector("#nodesTable");
    const tableSelected = await page.evaluate((id) => {
      const button = [...document.querySelectorAll(".table-selection")].find(
        (candidate) => candidate.textContent?.trim() === id,
      );
      if (!(button instanceof HTMLButtonElement)) {
        return false;
      }
      button.click();
      return true;
    }, firstNodeId);
    requireCondition(tableSelected, "table node was unavailable for selection");
    await page.waitForFunction(
      (id) =>
        [...document.querySelectorAll(".table-selection")].some(
          (candidate) =>
            candidate.textContent?.trim() === id &&
            candidate.getAttribute("aria-pressed") === "true",
        ),
      {},
      firstNodeId,
    );

    await clickButton(page, "Edit node parameters");
    await page.waitForSelector("#node-config");
    const editedConfig = await page.$eval("#node-config", (element) => {
      const current = JSON.parse(element.value);
      current.phase1_browser_marker = true;
      return current;
    });
    requireCondition(Boolean(editedConfig.phase1_browser_marker), "edit fixture failed");
    await page.$eval(
      "#node-config",
      (element, value) => {
        const setter = Object.getOwnPropertyDescriptor(
          HTMLTextAreaElement.prototype,
          "value",
        )?.set;
        setter?.call(element, value);
        element.dispatchEvent(
          new InputEvent("input", {
            bubbles: true,
            inputType: "insertText",
            data: value,
          }),
        );
      },
      JSON.stringify(editedConfig),
    );
    await clickButton(page, "Save parameters");
    await page.waitForFunction(
      (id) => document.body.textContent?.includes(`Parameters for ${id} updated in memory`),
      { timeout: 10_000 },
      firstNodeId,
    );
    requireCondition(graphFetches === 2, `PATCH refresh graph fetch count was ${graphFetches}`);
    const refreshed = await (
      await fetch(`http://127.0.0.1:${port}/api/v1/graph`)
    ).json();
    const refreshedNode = refreshed.data.nodes.find((node) => node.id === firstNodeId);
    requireCondition(
      refreshedNode.type === firstNodeType &&
        refreshedNode.node_config.phase1_browser_marker === true,
      "PATCH refresh did not preserve identity/type and update configuration",
    );
    const selectionRetained = await page.$eval(
      '[data-testid="node-inspector"]',
      (element, id) => element.textContent?.includes(id) ?? false,
      firstNodeId,
    );
    requireCondition(selectionRetained, "valid node selection was lost after refresh");
    await clickButton(page, "Topology");
    await page.waitForFunction(
      (nodeCount) =>
        document.querySelectorAll(".react-flow__node").length === nodeCount &&
        [...document.querySelectorAll(".react-flow__node")].every(
          (node) => getComputedStyle(node).visibility !== "hidden",
        ),
      { timeout: 30_000 },
      expectedNodes,
    );
    await page.waitForFunction(
      (edgeCount) =>
        document.querySelectorAll(".react-flow__edge").length === edgeCount,
      { timeout: 30_000 },
      expectedEdges,
    );
  }

  requireCondition(
    consoleErrors.length === 0,
    `browser console errors:\n${consoleErrors.join("\n")}`,
  );
  await mkdir(dirname(resolve(screenshot)), { recursive: true });
  await page.screenshot({ path: resolve(screenshot), fullPage: true });
} finally {
  if (browser) {
    await browser.close();
  }
  await stopServer();
}

console.log(
  `browser PASS port=${port} nodes=${expectedNodes} edges=${expectedEdges} ` +
    `mode=${mode} screenshot=${resolve(screenshot)}`,
);
