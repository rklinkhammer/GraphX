import { spawn } from "node:child_process";
import { mkdir, readFile } from "node:fs/promises";
import { dirname, extname, resolve } from "node:path";
import process from "node:process";

import puppeteer from "puppeteer-core";

function argumentsByName(argv) {
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    values.set(argv[index], argv[index + 1]);
  }
  return values;
}

function requireCondition(condition, detail) {
  if (!condition) {
    throw new Error(detail);
  }
}

const options = argumentsByName(process.argv.slice(2));
const dashboard = options.get("--dashboard");
const graphPath = options.get("--graph");
const port = Number(options.get("--port"));
const screenshotPath = options.get("--screenshot");
const firefox =
  process.env.GRAPHX_FIREFOX_EXECUTABLE ??
  "/Applications/Firefox.app/Contents/MacOS/firefox";

if (!dashboard || !graphPath || !Number.isSafeInteger(port) || !screenshotPath) {
  throw new Error(
    "required: --dashboard PATH --graph PATH --port PORT --screenshot PATH",
  );
}

const sourceDocument = JSON.parse(
  await readFile(resolve(graphPath), "utf8"),
);
const stableNodeIds = sourceDocument.nodes
  .map((node) => node.id)
  .sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));
function expectedSemanticNodeOrder(document) {
  const groups = document.presentation?.groups;
  if (!Array.isArray(groups) || groups.length === 0) return stableNodeIds;
  const byId = new Map(groups.map((group) => [group.id, group]));
  const children = new Map(groups.map((group) => [group.id, []]));
  for (const group of groups) {
    if (group.parent && children.has(group.parent)) children.get(group.parent).push(group.id);
  }
  for (const ids of children.values()) ids.sort();
  const order = [];
  const visit = (id) => {
    for (const child of children.get(id) ?? []) visit(child);
    order.push(...[...(byId.get(id)?.members ?? [])].sort());
  };
  for (const root of groups.filter((group) => !group.parent).map((group) => group.id).sort()) visit(root);
  const grouped = new Set(groups.flatMap((group) => group.members ?? []));
  order.push(...stableNodeIds.filter((id) => !grouped.has(id)));
  return order;
}
const expectedNodeIds = expectedSemanticNodeOrder(sourceDocument);

const server = spawn(
  resolve(dashboard),
  ["--graph", resolve(graphPath), "--port", String(port)],
  { stdio: ["ignore", "pipe", "pipe"] },
);
let serverOutput = "";
server.stdout.on("data", (data) => { serverOutput += data; });
server.stderr.on("data", (data) => { serverOutput += data; });

async function waitForServer() {
  const deadline = Date.now() + 12_000;
  while (Date.now() < deadline) {
    if (server.exitCode !== null) {
      throw new Error(`dashboard exited before readiness\n${serverOutput}`);
    }
    try {
      if ((await fetch(`http://127.0.0.1:${port}/`)).ok) {
        return;
      }
    } catch {
      // Loopback server is still starting.
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 50));
  }
  throw new Error(`dashboard did not become ready\n${serverOutput}`);
}

async function stopServer() {
  if (server.exitCode !== null) return;
  server.kill("SIGTERM");
  await Promise.race([
    new Promise((resolveExit) => server.once("exit", resolveExit)),
    new Promise((_, reject) =>
      setTimeout(() => reject(new Error("dashboard shutdown timed out")), 5000),
    ),
  ]);
  requireCondition(server.exitCode === 0, `dashboard shutdown failed\n${serverOutput}`);
}

async function activateButton(page, name, key = "Enter") {
  const found = await page.evaluate((accessibleName) => {
    const button = [...document.querySelectorAll("button")].find(
      (candidate) =>
        candidate.getAttribute("aria-label") === accessibleName ||
        candidate.textContent?.trim() === accessibleName,
    );
    if (!(button instanceof HTMLButtonElement) || button.disabled) return false;
    button.focus();
    return document.activeElement === button;
  }, name);
  requireCondition(found, `button unavailable: ${name}`);
  await page.keyboard.press(key);
}

async function activateView(page, name, regionSelector, key = "Enter") {
  await activateButton(page, name, key);
  try {
    await page.waitForFunction(
      (accessibleName, selector) => {
        const button = [...document.querySelectorAll("button")].find(
          (candidate) => candidate.textContent?.trim() === accessibleName,
        );
        return button?.getAttribute("aria-pressed") === "true" &&
          document.activeElement === button &&
          document.querySelector(selector) !== null;
      },
      { polling: "raf", timeout: 5_000 },
      name,
      regionSelector,
    );
  } catch (error) {
    const state = await page.evaluate((accessibleName, selector) => {
      const button = [...document.querySelectorAll("button")].find(
        (candidate) => candidate.textContent?.trim() === accessibleName,
      );
      return {
        buttonConnected: button?.isConnected ?? false,
        pressed: button?.getAttribute("aria-pressed") ?? null,
        regionPresent: document.querySelector(selector) !== null,
        activeTag: document.activeElement?.tagName ?? null,
        activeName:
          document.activeElement?.getAttribute("aria-label") ??
          document.activeElement?.textContent?.trim() ??
          null,
      };
    }, name, regionSelector);
    throw new Error(
      `view activation did not settle for ${name} with ${key}: ${JSON.stringify(state)}`,
      { cause: error },
    );
  }
}

const stem = screenshotPath.slice(0, -extname(screenshotPath).length);
const narrowScreenshot = `${stem}-narrow${extname(screenshotPath)}`;
const zoomScreenshot = `${stem}-zoom200${extname(screenshotPath)}`;
let browser;
try {
  await waitForServer();
  browser = await puppeteer.launch({
    browser: "firefox",
    executablePath: firefox,
    headless: true,
    args: ["--width=1440", "--height=1000"],
    extraPrefsFirefox: { "ui.prefersReducedMotion": 1 },
  });
  const zoomExtensionId = await browser.installExtension(
    resolve("test/fixtures/firefox-page-zoom"),
  );
  requireCondition(Boolean(zoomExtensionId), "Firefox page-zoom oracle extension did not install");
  const page = await browser.newPage();
  await page.setViewport({ width: 1440, height: 1000, deviceScaleFactor: 1 });
  const consoleErrors = [];
  const requests = [];
  page.on("console", (entry) => {
    if (entry.type() === "error") consoleErrors.push(entry.text());
  });
  page.on("pageerror", (error) => consoleErrors.push(error.message));
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.protocol !== "http:" && url.protocol !== "https:") return;
    requests.push([request.method(), url.pathname]);
  });

  await page.goto(`http://127.0.0.1:${port}/`, {
    waitUntil: "networkidle0",
    timeout: 20_000,
  });
  await page.waitForSelector('[data-testid="topology-counts"]', { timeout: 20_000 });

  await page.keyboard.press("Tab");
  requireCondition(
    await page.evaluate(() => document.activeElement?.textContent?.trim() === "Skip to dashboard view controls"),
    "first Tab did not focus the skip link",
  );
  await page.keyboard.press("Tab");
  requireCondition(
    await page.evaluate(() => document.activeElement?.closest(".execution-panel") !== null),
    "Tab after the skip link did not enter execution controls",
  );
  await page.keyboard.down("Shift");
  await page.keyboard.press("Tab");
  await page.keyboard.up("Shift");
  requireCondition(
    await page.evaluate(() => document.activeElement?.textContent?.trim() === "Skip to dashboard view controls"),
    "reverse Tab did not return from execution controls to the skip link",
  );
  await page.keyboard.press("Enter");
  requireCondition(
    await page.evaluate(() => document.activeElement?.id === "dashboard-view-controls"),
    "skip link did not focus dashboard view controls",
  );

  await activateButton(page, "Semantic topology", " ");
  await page.waitForFunction(
    () =>
      document.activeElement?.textContent?.trim() === "Semantic topology" &&
      document.activeElement?.getAttribute("aria-pressed") === "true",
    { polling: "raf", timeout: 5_000 },
  );
  requireCondition(
    await page.evaluate(() =>
      document.activeElement?.textContent?.trim() === "Semantic topology" &&
      document.activeElement?.getAttribute("aria-pressed") === "true"),
    "Space view activation did not retain focus/state",
  );
  await page.waitForSelector("#semantic-topology-region", { timeout: 10_000 });
  await page.waitForFunction(
    (nodes, edges) =>
      document.querySelectorAll('[data-semantic-record^="node:"]').length === nodes &&
      document.querySelectorAll('[data-semantic-record^="edge:"]').length === edges,
    { polling: "raf", timeout: 20_000 },
    sourceDocument.nodes.length,
    sourceDocument.edges.length,
  );

  const semanticEvidence = await page.evaluate(() => ({
    nodeIds: [...document.querySelectorAll('[data-semantic-record^="node:"]')]
      .map((element) => element.getAttribute("data-semantic-record")?.slice(5)),
    edgeRows: [...document.querySelectorAll('[data-semantic-record^="edge:"]')]
      .map((element) => element.textContent ?? ""),
    counts: document.querySelector('[data-testid="semantic-counts"]')?.textContent ?? "",
    positiveTabindex: [...document.querySelectorAll("[tabindex]")]
      .filter((element) => Number(element.getAttribute("tabindex")) > 0).length,
    duplicateIds: (() => {
      const ids = [...document.querySelectorAll("[id]")].map((element) => element.id);
      return ids.length - new Set(ids).size;
    })(),
    landmarkNames: [...document.querySelectorAll("main, nav, section[aria-label], section[aria-labelledby], aside")]
      .map((element) => element.getAttribute("aria-label") ?? element.getAttribute("aria-labelledby") ?? ""),
  }));
  requireCondition(
    JSON.stringify(semanticEvidence.nodeIds) === JSON.stringify(expectedNodeIds),
    `semantic node identity/order mismatch: ${JSON.stringify(semanticEvidence.nodeIds)}`,
  );
  requireCondition(
    semanticEvidence.edgeRows.length === sourceDocument.edges.length,
    "semantic edge inventory count mismatch",
  );
  for (const edge of sourceDocument.edges) {
    const sourcePort = Object.hasOwn(edge, "source_port")
      ? `index ${edge.source_port}`
      : `name “${edge.source_port_name}”`;
    const targetPort = Object.hasOwn(edge, "target_port")
      ? `index ${edge.target_port}`
      : `name “${edge.target_port_name}”`;
    requireCondition(
      semanticEvidence.edgeRows.some(
        (row) =>
          row.includes(edge.source_node_id) &&
          row.includes(sourcePort) &&
          row.includes(edge.target_node_id) &&
          row.includes(targetPort),
      ),
      `semantic edge tuple missing: ${edge.source_node_id} ${sourcePort} -> ${edge.target_node_id} ${targetPort}`,
    );
  }
  requireCondition(semanticEvidence.positiveTabindex === 0, "positive tabindex found");
  requireCondition(semanticEvidence.duplicateIds === 0, "duplicate DOM IDs found");

  const resetFocused = await page.evaluate(() => {
    const button = document.querySelector('[data-storage-key="graphx.dashboard.presentation"]');
    if (!(button instanceof HTMLButtonElement)) return false;
    button.focus();
    return document.activeElement === button;
  });
  requireCondition(resetFocused, "reset control could not receive focus");
  await page.keyboard.press("Tab");
  requireCondition(
    await page.evaluate(() => document.activeElement?.id === "semantic-search"),
    "Tab order did not move from view/reset controls into the active semantic view",
  );
  await page.keyboard.down("Shift");
  await page.keyboard.press("Tab");
  await page.keyboard.up("Shift");
  requireCondition(
    await page.evaluate(() => document.activeElement?.hasAttribute("data-storage-key")),
    "reverse Tab did not return from active semantic view to view/reset controls",
  );

  await page.focus("#semantic-search");
  await page.keyboard.type(expectedNodeIds[0]);
  await page.waitForFunction(
    () => document.querySelectorAll('[data-semantic-record^="node:"]').length === 1,
    { polling: "raf", timeout: 5_000 },
  );
  const platformModifier = process.platform === "darwin" ? "Meta" : "Control";
  await page.keyboard.down(platformModifier);
  await page.keyboard.press("A");
  await page.keyboard.up(platformModifier);
  await page.keyboard.press("Backspace");
  await page.waitForFunction(
    (count) => document.querySelectorAll('[data-semantic-record^="node:"]').length === count,
    { polling: "raf", timeout: 5_000 },
    sourceDocument.nodes.length,
  );

  const firstGroupId = sourceDocument.presentation?.groups?.[0]?.id;
  if (firstGroupId) {
    const disclosureKey = `group-disclosure:${firstGroupId}`;
    const disclosureBefore = await page.evaluate((key) => {
      const summary = document.querySelector(`[data-semantic-key="${key}"]`);
      if (!(summary instanceof HTMLElement)) return null;
      summary.focus();
      return summary.closest("details")?.open ?? null;
    }, disclosureKey);
    requireCondition(disclosureBefore !== null, `group disclosure unavailable: ${firstGroupId}`);
    await page.keyboard.press(" ");
    await page.waitForFunction(
      (key, before) =>
        document.querySelector(`[data-semantic-key="${key}"]`)?.closest("details")?.open !== before,
      { polling: "raf", timeout: 5_000 },
      disclosureKey,
      disclosureBefore,
    );
    requireCondition(
      await page.evaluate((key) => document.activeElement?.getAttribute("data-semantic-key") === key, disclosureKey),
      "semantic disclosure Space activation lost focus",
    );
    await page.keyboard.press("Enter");
    await page.waitForFunction(
      (key, before) =>
        document.querySelector(`[data-semantic-key="${key}"]`)?.closest("details")?.open === before,
      { polling: "raf", timeout: 5_000 },
      disclosureKey,
      disclosureBefore,
    );

    await activateButton(page, `Inspect group ${firstGroupId}`, " ");
    await page.waitForFunction(
      (id) => document.querySelector('[data-testid="group-inspector"]')?.textContent?.includes(id),
      { polling: "raf", timeout: 5_000 },
      firstGroupId,
    );

    await activateView(page, "Topology", "#topology-heading");
    const bundleEvidence = await page.evaluate(() => {
      const edge = document.querySelector(".react-flow__edge.bundle-edge");
      if (!(edge instanceof SVGGElement)) return null;
      edge.focus();
      return {
        id: edge.getAttribute("data-id"),
        label: edge.getAttribute("aria-label"),
        role: edge.getAttribute("role"),
        tabIndex: edge.getAttribute("tabindex"),
        pressed: edge.getAttribute("aria-pressed"),
        selected: edge.classList.contains("selected"),
        focused: document.activeElement === edge,
      };
    });
    if (bundleEvidence !== null) {
      requireCondition(
        bundleEvidence.id &&
          bundleEvidence.label?.startsWith("Presentation bundle from ") &&
          bundleEvidence.role === "button" &&
          bundleEvidence.tabIndex === "0" &&
          bundleEvidence.pressed === "false" &&
          !bundleEvidence.selected &&
          bundleEvidence.focused,
        `bundle edge semantics mismatch: ${JSON.stringify(bundleEvidence)}`,
      );
      await page.keyboard.press("Enter");
      await page.waitForFunction(
        (id) => {
          const edge = [...document.querySelectorAll(".react-flow__edge")]
            .find((candidate) => candidate.getAttribute("data-id") === id);
          return document.querySelector('[data-testid="bundle-inspector"]') !== null &&
            document.querySelector('[data-testid="group-inspector"]') === null &&
            edge?.getAttribute("aria-pressed") === "true" &&
            edge.classList.contains("selected") &&
            document.activeElement === edge;
        },
        { polling: "raf", timeout: 5_000 },
        bundleEvidence.id,
      );
    }
    await activateView(page, "Semantic topology", "#semantic-topology-region");
    await activateButton(page, `Inspect group ${firstGroupId}`);
    const stateText = await page.$eval(
      `[data-semantic-key="${disclosureKey}"]`,
      (element) => element.textContent ?? "",
    );
    const canvasAction = stateText.includes("canvas collapsed")
      ? `Expand on canvas: group ${firstGroupId}`
      : `Collapse on canvas: group ${firstGroupId}`;
    await activateButton(page, canvasAction, " ");
    const toggledCanvasAction = canvasAction.startsWith("Expand")
      ? `Collapse on canvas: group ${firstGroupId}`
      : `Expand on canvas: group ${firstGroupId}`;
    await page.waitForFunction(
      (name) => document.activeElement?.getAttribute("aria-label") === name,
      { polling: "raf", timeout: 5_000 },
      toggledCanvasAction,
    );
    requireCondition(
      await page.evaluate((name) => document.activeElement?.getAttribute("aria-label") === name, toggledCanvasAction),
      "semantic canvas-collapse Space activation lost focus",
    );
    await activateButton(page, `Isolate on canvas: group ${firstGroupId}`);
    await page.waitForFunction(
      (id) =>
        document.activeElement?.getAttribute("aria-label") ===
        `Isolate on canvas: group ${id}`,
      { polling: "raf", timeout: 5_000 },
      firstGroupId,
    );
    requireCondition(
      await page.evaluate((id) => document.activeElement?.getAttribute("aria-label") === `Isolate on canvas: group ${id}`, firstGroupId),
      "semantic isolate activation lost focus",
    );
    const isolatedGroupNeedsExpansion = await page.evaluate((id) =>
      [...document.querySelectorAll("button")].some(
        (button) => button.getAttribute("aria-label") === `Expand on canvas: group ${id}`,
      ), firstGroupId);
    if (isolatedGroupNeedsExpansion) {
      await activateButton(page, `Expand on canvas: group ${firstGroupId}`);
    }

    const firstCanvasNodeId = sourceDocument.presentation.groups[0].members[0];
    await activateView(page, "Topology", "#topology-heading");
    await activateButton(page, "Raw topology");
    await page.waitForFunction(
      () => document.querySelector(".react-flow__edge:not(.bundle-edge)") !== null &&
        [...document.querySelectorAll("button")].some(
          (button) => button.textContent?.trim() === "Raw topology" &&
            button.getAttribute("aria-pressed") === "true",
        ),
      { polling: "raf", timeout: 10_000 },
    );
    const exactEdgeEvidence = await page.evaluate(() => {
      const edge = document.querySelector(".react-flow__edge:not(.bundle-edge)");
      if (!(edge instanceof SVGGElement)) return null;
      edge.focus();
      return {
        id: edge.getAttribute("data-id"),
        label: edge.getAttribute("aria-label"),
        role: edge.getAttribute("role"),
        tabIndex: edge.getAttribute("tabindex"),
        pressed: edge.getAttribute("aria-pressed"),
        selected: edge.classList.contains("selected"),
        focused: document.activeElement === edge,
      };
    });
    requireCondition(
      exactEdgeEvidence?.id &&
        exactEdgeEvidence.label?.startsWith("Edge from ") &&
        exactEdgeEvidence.role === "button" &&
        exactEdgeEvidence.tabIndex === "0" &&
        exactEdgeEvidence.pressed === "false" &&
        !exactEdgeEvidence.selected &&
        exactEdgeEvidence.focused,
      `exact edge semantics mismatch: ${JSON.stringify(exactEdgeEvidence)}`,
    );
    await page.keyboard.press("Enter");
    try {
      await page.waitForFunction(
        (id, label) => {
          const edge = [...document.querySelectorAll(".react-flow__edge")]
            .find((candidate) => candidate.getAttribute("data-id") === id);
          return document.querySelector('[data-testid="edge-inspector"]') !== null &&
            document.querySelector('[data-testid="group-inspector"]') === null &&
            document.querySelector('[data-testid="bundle-inspector"]') === null &&
            edge?.getAttribute("aria-label") === label &&
            edge.getAttribute("aria-pressed") === "true" &&
            edge.classList.contains("selected") &&
            document.activeElement === edge;
        },
        { polling: "raf", timeout: 5_000 },
        exactEdgeEvidence.id,
        exactEdgeEvidence.label,
      );
    } catch (error) {
      const state = await page.evaluate((id) => {
        const edge = [...document.querySelectorAll(".react-flow__edge")]
          .find((candidate) => candidate.getAttribute("data-id") === id);
        return {
          edgeConnected: edge?.isConnected ?? false,
          pressed: edge?.getAttribute("aria-pressed") ?? null,
          selected: edge?.classList.contains("selected") ?? false,
          edgeInspector: document.querySelector('[data-testid="edge-inspector"]')?.textContent ?? null,
          groupInspector: document.querySelector('[data-testid="group-inspector"]') !== null,
          bundleInspector: document.querySelector('[data-testid="bundle-inspector"]') !== null,
          activeId: document.activeElement?.getAttribute("data-id") ?? null,
          activeName: document.activeElement?.getAttribute("aria-label") ?? null,
        };
      }, exactEdgeEvidence.id);
      throw new Error(`exact edge activation did not settle: ${JSON.stringify(state)}`, { cause: error });
    }
    await activateButton(page, "Grouped topology");
    await page.waitForFunction(
      () => [...document.querySelectorAll("button")].some(
        (button) => button.textContent?.trim() === "Grouped topology" &&
          button.getAttribute("aria-pressed") === "true",
      ),
      { polling: "raf", timeout: 10_000 },
    );
    const selectCanvasNodeName = `Select node ${firstCanvasNodeId}`;
    await page.waitForFunction(
      (name) => [...document.querySelectorAll("button")].some(
        (button) => button.getAttribute("aria-label") === name,
      ),
      { polling: "raf", timeout: 10_000 },
      selectCanvasNodeName,
    );
    const canvasNodeSemantics = await page.evaluate((nodeId) => {
      const wrapper = document.querySelector(`.react-flow__node[data-id="${CSS.escape(nodeId)}"]`);
      const card = wrapper?.querySelector(".graph-node-card");
      const select = wrapper?.querySelector(".node-keyboard-select");
      return {
        wrapperTabIndex: wrapper?.getAttribute("tabindex") ?? null,
        wrapperRole: wrapper?.getAttribute("role") ?? null,
        cardTag: card?.tagName ?? null,
        cardTabIndex: card?.getAttribute("tabindex") ?? null,
        cardRole: card?.getAttribute("role") ?? null,
        selectTag: select?.tagName ?? null,
        selectPressed: select?.getAttribute("aria-pressed") ?? null,
      };
    }, firstCanvasNodeId);
    requireCondition(
      canvasNodeSemantics.wrapperTabIndex !== "0" &&
        canvasNodeSemantics.wrapperRole === null &&
        canvasNodeSemantics.cardTag === "ARTICLE" &&
        canvasNodeSemantics.cardTabIndex === null &&
        canvasNodeSemantics.cardRole === null &&
        canvasNodeSemantics.selectTag === "BUTTON",
      `canvas node exposed duplicate or misleading activation semantics: ${JSON.stringify(canvasNodeSemantics)}`,
    );

    // Puppeteer's Firefox BiDi key map uses the literal space character for
    // the physical Space key and rejects the Chromium-style "Space" name.
    for (const key of ["Enter", " "]) {
      await activateView(page, "Semantic topology", "#semantic-topology-region");
      await activateButton(page, `Inspect group ${firstGroupId}`);
      await page.waitForFunction(
        (id) => document.querySelector('[data-testid="group-inspector"]')?.textContent?.includes(id),
        { polling: "raf", timeout: 5_000 },
        firstGroupId,
      );
      await activateView(page, "Topology", "#topology-heading");
      await page.waitForFunction(
        (name) => [...document.querySelectorAll("button")].some(
          (button) => button.getAttribute("aria-label") === name,
        ),
        { polling: "raf", timeout: 10_000 },
        selectCanvasNodeName,
      );
      await activateButton(page, selectCanvasNodeName, key);
      await page.waitForFunction(
        (id, name) =>
          document.querySelector('[data-testid="node-inspector"]')?.textContent?.includes(id) &&
          document.querySelector('[data-testid="group-inspector"]') === null &&
          [...document.querySelectorAll("button")].some(
            (button) =>
              button.getAttribute("aria-label") === name &&
              button.getAttribute("aria-pressed") === "true" &&
              document.activeElement === button,
          ),
        { polling: "raf", timeout: 5_000 },
        firstCanvasNodeId,
        selectCanvasNodeName,
      );
    }

    await activateView(page, "Semantic topology", "#semantic-topology-region");
    await activateButton(page, `Inspect group ${firstGroupId}`);
    await activateView(page, "Topology", "#topology-heading");
    await page.waitForFunction(
      (name) => [...document.querySelectorAll("button")].some(
        (button) => button.getAttribute("aria-label") === name,
      ),
      { polling: "raf", timeout: 10_000 },
      selectCanvasNodeName,
    );
    await page.evaluate((nodeId) => {
      const wrapper = [...document.querySelectorAll(".react-flow__node")].find(
        (candidate) => candidate.getAttribute("data-id") === nodeId,
      );
      const card = wrapper?.querySelector(".graph-node-card");
      if (!(card instanceof HTMLElement)) {
        throw new Error(`canvas node card unavailable: ${nodeId}`);
      }
      card.click();
    }, firstCanvasNodeId);
    await page.waitForFunction(
      (id) =>
        document.querySelector('[data-testid="node-inspector"]')?.textContent?.includes(id) &&
        document.querySelector('[data-testid="group-inspector"]') === null,
      { polling: "raf", timeout: 5_000 },
      firstCanvasNodeId,
    );
    await activateView(page, "Semantic topology", "#semantic-topology-region");
  }

  const contrastEvidence = await page.evaluate(() => {
    const channels = (color) => {
      const values = color.match(/[\d.]+/g)?.slice(0, 3).map(Number);
      if (!values || values.length !== 3) throw new Error(`unreadable computed color: ${color}`);
      return values.map((value) => value / 255);
    };
    const luminance = (color) => {
      const linear = channels(color).map((channel) =>
        channel <= 0.04045
          ? channel / 12.92
          : ((channel + 0.055) / 1.055) ** 2.4,
      );
      return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
    };
    const ratio = (first, second) => {
      const values = [luminance(first), luminance(second)].sort((a, b) => b - a);
      return (values[0] + 0.05) / (values[1] + 0.05);
    };
    const firstSelection = document.querySelector('[data-semantic-record^="node:"] button');
    const firstRecord = document.querySelector('[data-semantic-record^="node:"]');
    const group = document.querySelector(".semantic-group-item > details");
    const edgeRegion = document.querySelector(".semantic-edge-table-region");
    if (!(firstSelection instanceof HTMLButtonElement) || !firstRecord || !edgeRegion) {
      throw new Error("contrast evidence targets unavailable");
    }
    firstSelection.focus();
    const selectionStyle = getComputedStyle(firstSelection);
    const recordStyle = getComputedStyle(firstRecord);
    const groupStyle = group ? getComputedStyle(group) : null;
    const edgeStyle = getComputedStyle(edgeRegion);
    return {
      focus: ratio(selectionStyle.outlineColor, recordStyle.backgroundColor),
      control: ratio(selectionStyle.borderTopColor, selectionStyle.backgroundColor),
      group: groupStyle
        ? ratio(groupStyle.borderTopColor, groupStyle.backgroundColor)
        : null,
      edge: ratio(edgeStyle.borderTopColor, getComputedStyle(document.querySelector(".primary-view")).backgroundColor),
    };
  });
  for (const [name, ratio] of Object.entries(contrastEvidence)) {
    if (ratio === null) continue;
    requireCondition(ratio >= 3, `${name} non-text contrast below 3:1: ${ratio}`);
  }

  const firstNodeId = expectedNodeIds[0];
  const firstNodeType = sourceDocument.nodes.find((node) => node.id === firstNodeId).type;
  await activateButton(
    page,
    `Select authoritative node ${firstNodeId}, type ${firstNodeType}`,
    " ",
  );
  await page.waitForSelector('[data-testid="node-inspector"]', {
    timeout: 5_000,
  });
  await page.waitForFunction(
    (id, type) => {
      const inspector = document.querySelector('[data-testid="node-inspector"]');
      if (!inspector) return false;
      const values = [...inspector.querySelectorAll("dd")]
        .map((element) => element.textContent?.trim() ?? "");
      return values[0] === id && values[1] === type;
    },
    { polling: "raf", timeout: 5_000 },
    firstNodeId,
    firstNodeType,
  );
  requireCondition(
    await page.$eval(
      '[data-testid="node-inspector"]',
      (element, id, type) => {
        const values = [...element.querySelectorAll("dd")]
          .map((candidate) => candidate.textContent?.trim() ?? "");
        return values[0] === id && values[1] === type;
      },
      firstNodeId,
      firstNodeType,
    ),
    "semantic node selection did not synchronize with inspector",
  );

  const firstEdgeName = await page.$eval(
    '[data-semantic-record^="edge:"] button',
    (button) => button.getAttribute("aria-label"),
  );
  requireCondition(Boolean(firstEdgeName), "semantic edge accessible name unavailable");
  await activateButton(page, firstEdgeName);
  await page.waitForSelector('[data-testid="edge-inspector"]', { timeout: 5_000 });
  await activateButton(page, firstEdgeName, " ");

  const editName = `Edit parameters for node ${firstNodeId}`;
  await activateButton(page, editName);
  await page.waitForSelector('[role="dialog"]');
  await page.waitForFunction(
    () => document.activeElement?.id === "node-config",
    { polling: "raf", timeout: 5_000 },
  );
  await page.keyboard.press("Tab");
  requireCondition(await page.evaluate(() => document.activeElement?.textContent?.trim() === "Save parameters"), "dialog forward Tab missed Save");
  await page.keyboard.press("Tab");
  requireCondition(await page.evaluate(() => document.activeElement?.textContent?.trim() === "Cancel"), "dialog forward Tab missed Cancel");
  await page.keyboard.press("Tab");
  requireCondition(await page.evaluate(() => document.activeElement?.id === "node-config"), "dialog forward Tab did not cycle");
  await page.keyboard.down("Shift");
  await page.keyboard.press("Tab");
  await page.keyboard.up("Shift");
  requireCondition(await page.evaluate(() => document.activeElement?.textContent?.trim() === "Cancel"), "dialog reverse Tab did not cycle");
  await page.keyboard.press("Escape");
  await page.waitForSelector('[role="dialog"]', { hidden: true });
  await page.waitForFunction(
    (name) => document.activeElement?.getAttribute("aria-label") === name,
    { polling: "raf", timeout: 5_000 },
    editName,
  );
  const requestsBeforeElsewhereEscape = requests.length;
  await page.keyboard.press("Escape");
  requireCondition(requests.length === requestsBeforeElsewhereEscape, "Escape outside the dialog issued a request");

  let persistedDisclosureOpen = null;
  if (firstGroupId) {
    const disclosureKey = `group-disclosure:${firstGroupId}`;
    persistedDisclosureOpen = await page.evaluate((key) => {
      const summary = document.querySelector(`[data-semantic-key="${key}"]`);
      if (!(summary instanceof HTMLElement)) return null;
      summary.focus();
      return summary.closest("details")?.open ?? null;
    }, disclosureKey);
    requireCondition(persistedDisclosureOpen !== null, "persistence disclosure target unavailable");
    await page.keyboard.press(" ");
    persistedDisclosureOpen = !persistedDisclosureOpen;
    await page.waitForFunction(
      (key, expected) =>
        document.querySelector(`[data-semantic-key="${key}"]`)?.closest("details")?.open === expected,
      { polling: "raf", timeout: 5_000 },
      disclosureKey,
      persistedDisclosureOpen,
    );
  }

  await activateButton(page, "Topology");
  await page.waitForSelector("#topology-heading", { timeout: 5_000 });
  await activateButton(page, "Raw topology");
  await page.waitForFunction(
    () => [...document.querySelectorAll("button")].some(
      (button) => button.textContent?.trim() === "Raw topology" && button.getAttribute("aria-pressed") === "true",
    ),
    { polling: "raf", timeout: 5_000 },
  );
  requireCondition(
    await page.evaluate(() =>
      [...document.querySelectorAll("button")].some(
        (button) => button.textContent?.trim() === "Raw topology" && button.getAttribute("aria-pressed") === "true",
      ),
    ),
    "raw mode did not activate before persistence",
  );
  const preferenceBeforeViewport = await page.evaluate(() =>
    localStorage.getItem("graphx.dashboard.presentation"),
  );
  await activateButton(page, "Reset deterministic topology layout");
  const minimapFocused = await page.evaluate(() => {
    const minimap = document.querySelector('[data-testid="minimap-keyboard-control"]');
    if (!(minimap instanceof HTMLElement)) return false;
    minimap.focus();
    return document.activeElement === minimap;
  });
  requireCondition(minimapFocused, "minimap could not receive focus for persisted viewport");
  await page.keyboard.press("ArrowLeft");
  await page.waitForFunction(
    (before) => {
      const serialized = localStorage.getItem("graphx.dashboard.presentation");
      if (!serialized || serialized === before) return false;
      const value = JSON.parse(serialized);
      return value.mode === "raw" &&
        Number.isFinite(value.viewport?.x) &&
        Number.isFinite(value.viewport?.y) &&
        Number.isFinite(value.viewport?.zoom);
    },
    { polling: "raf", timeout: 5_000 },
    preferenceBeforeViewport,
  );
  const persistedRecord = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("graphx.dashboard.presentation")),
  );
  requireCondition(
    persistedRecord.schema === 1 &&
      /^sha256:[0-9a-f]{64}$/.test(persistedRecord.graph_signature) &&
      persistedRecord.mode === "raw" &&
      Array.isArray(persistedRecord.collapsed_group_ids) &&
      Array.isArray(persistedRecord.semantic_expanded_group_ids) &&
      persistedRecord.collapsed_group_ids.length <= 256 &&
      persistedRecord.semantic_expanded_group_ids.length <= 256 &&
      Number.isFinite(persistedRecord.viewport.x) &&
      Number.isFinite(persistedRecord.viewport.y) &&
      persistedRecord.viewport.zoom >= 0.1 && persistedRecord.viewport.zoom <= 4,
    `saved preference record was not exact/bounded: ${JSON.stringify(persistedRecord)}`,
  );

  await page.reload({ waitUntil: "networkidle0", timeout: 20_000 });
  await page.waitForSelector('[data-testid="topology-counts"]', { timeout: 20_000 });
  requireCondition(
    await page.evaluate(() =>
      [...document.querySelectorAll("button")].some(
        (button) => button.textContent?.trim() === "Raw topology" && button.getAttribute("aria-pressed") === "true",
      ),
    ),
    "saved raw mode was not restored after reload",
  );
  await page.waitForFunction(
    (viewport) => {
      const element = document.querySelector(".react-flow__viewport");
      if (!element) return false;
      const matrix = new DOMMatrix(getComputedStyle(element).transform);
      return Math.abs(matrix.e - viewport.x) < 0.01 &&
        Math.abs(matrix.f - viewport.y) < 0.01 &&
        Math.abs(matrix.a - viewport.zoom) < 0.001;
    },
    { polling: "raf", timeout: 5_000 },
    persistedRecord.viewport,
  );
  if (firstGroupId) {
    await activateButton(page, "Semantic topology");
    await page.waitForFunction(
      (key, expected) =>
        document.querySelector(`[data-semantic-key="group-disclosure:${key}"]`)?.closest("details")?.open === expected,
      { polling: "raf", timeout: 5_000 },
      firstGroupId,
      persistedDisclosureOpen,
    );
    await activateButton(page, "Topology");
    await activateButton(page, "Grouped topology");
    const expectedCollapseName = persistedRecord.collapsed_group_ids.includes(firstGroupId)
      ? `Expand group ${firstGroupId}`
      : `Collapse group ${firstGroupId}`;
    await page.waitForFunction(
      (name) => [...document.querySelectorAll("button")].some((button) => button.getAttribute("aria-label") === name),
      { polling: "raf", timeout: 10_000 },
      expectedCollapseName,
    );
  }

  await page.waitForFunction(
    (settledMode) => {
      const saved = localStorage.getItem("graphx.dashboard.presentation");
      return saved !== null && JSON.parse(saved).mode === settledMode;
    },
    { polling: "raf", timeout: 5_000 },
    firstGroupId ? "grouped" : "raw",
  );
  await page.evaluate(() => {
    const value = JSON.parse(localStorage.getItem("graphx.dashboard.presentation"));
    value.graph_signature = `sha256:${"0".repeat(64)}`;
    value.mode = "raw";
    value.collapsed_group_ids = [];
    value.semantic_expanded_group_ids = [];
    value.viewport = { x: 900, y: -700, zoom: 3 };
    localStorage.setItem("graphx.dashboard.presentation", JSON.stringify(value));
  });
  await page.reload({ waitUntil: "networkidle0", timeout: 20_000 });
  await page.waitForSelector('[data-testid="topology-counts"]', { timeout: 20_000 });
  await page.waitForFunction(
    () => document.querySelector(".notice-region")?.textContent?.includes(
      "Saved view preferences were invalid or for another graph; deterministic defaults are active.",
    ),
    { polling: "raf", timeout: 5_000 },
  );
  const expectedDefaultMode = firstGroupId ? "Grouped topology" : "Raw topology";
  requireCondition(
    await page.evaluate((name) =>
      [...document.querySelectorAll("button")].some(
        (button) => button.textContent?.trim() === name && button.getAttribute("aria-pressed") === "true",
      ), expectedDefaultMode),
    "stale preferences did not restore deterministic mode",
  );
  requireCondition(
    !(await page.$eval(".react-flow__viewport", (element) => element.getAttribute("style") ?? "")).includes("scale(3)"),
    "stale viewport was applied",
  );

  await activateButton(page, "Reset view preferences stored only in this browser");
  await page.waitForFunction(
    () =>
      document.querySelector(".notice-region")?.textContent?.includes(
        "View preferences reset to deterministic defaults",
      ) && localStorage.getItem("graphx.dashboard.presentation") === null,
    { polling: "raf", timeout: 5_000 },
  );
  requireCondition(
    (await page.evaluate(() => localStorage.getItem("graphx.dashboard.presentation"))) === null,
    "preference reset recreated local storage without later interaction",
  );

  const reducedMotion = await page.evaluate(() => {
    const button = document.querySelector("button");
    const style = getComputedStyle(button);
    const topology = document.querySelector(".topology-shell");
    return {
      system: matchMedia("(prefers-reduced-motion: reduce)").matches,
      animation: style.animationDuration,
      transition: style.transitionDuration,
      applicationState: topology?.getAttribute("data-reduced-motion"),
      operationDuration: Number(topology?.getAttribute("data-motion-duration-ms")),
      counts: document.querySelector('[data-testid="topology-counts"]')?.textContent ?? "",
    };
  });
  const durationSeconds = (value) =>
    value.endsWith("ms")
      ? Number.parseFloat(value) / 1000
      : Number.parseFloat(value);
  requireCondition(
    reducedMotion.system &&
      durationSeconds(reducedMotion.animation) <= 0.00001 &&
      durationSeconds(reducedMotion.transition) <= 0.00001 &&
      reducedMotion.applicationState === "reduce" &&
      reducedMotion.operationDuration === 0 &&
      reducedMotion.counts.includes(`${sourceDocument.nodes.length} nodes`) &&
      reducedMotion.counts.includes(`${sourceDocument.edges.length} edges`),
    `reduced-motion CSS not active: ${JSON.stringify(reducedMotion)}`,
  );
  await activateButton(page, "Semantic topology");
  await page.waitForSelector("#semantic-topology-region", { timeout: 5_000 });

  await mkdir(dirname(resolve(screenshotPath)), { recursive: true });
  await page.screenshot({ path: resolve(screenshotPath), fullPage: true });

  await page.setViewport({ width: 320, height: 900, deviceScaleFactor: 1 });
  await page.evaluate(() => {
    const style = document.createElement("style");
    style.id = "phase3-text-spacing-oracle";
    style.textContent = `
      * {
        line-height: 1.5 !important;
        letter-spacing: 0.12em !important;
        word-spacing: 0.16em !important;
      }
      p { margin-bottom: 2em !important; }
    `;
    document.head.append(style);
  });
  const reflow = await page.evaluate(() => ({
    viewportWidth: document.documentElement.clientWidth,
    pageWidth: document.documentElement.scrollWidth,
    edgeRegionContained:
      document.querySelector(".semantic-edge-table-region")?.scrollWidth >
      document.querySelector(".semantic-edge-table-region")?.clientWidth,
    semanticVisible: document.querySelector("#semantic-topology-region")?.getBoundingClientRect().width > 0,
    inspectorVisible: document.querySelector(".inspector")?.getBoundingClientRect().width > 0,
    exactUniversalRule: document.querySelector("#phase3-text-spacing-oracle")?.textContent?.includes("* {") === true,
    semanticRepresentativeCount: document.querySelectorAll(
      "#semantic-topology-region button, #semantic-topology-region input, #semantic-topology-region summary, .semantic-edge-table-region",
    ).length,
    clippedRepresentatives: [
      ...document.querySelectorAll(
        ".view-tabs button, .execution-actions button, #semantic-topology-region button, #semantic-topology-region input, #semantic-topology-region summary, .inspector button",
      ),
    ]
      .filter((element) => element.getClientRects().length > 0)
      .filter(
        (element) =>
          element.scrollWidth > element.clientWidth + 1 ||
          element.scrollHeight > element.clientHeight + 1,
      )
      .map((element) => element.getAttribute("aria-label") ?? element.textContent?.trim()),
    outOfViewportRepresentatives: [
      ...document.querySelectorAll(
        ".view-tabs button, .execution-actions button, #semantic-topology-region button, #semantic-topology-region input, #semantic-topology-region summary, .semantic-edge-table-region, .inspector button",
      ),
    ]
      .filter((element) => element.getClientRects().length > 0)
      .filter((element) => {
        if (
          element.closest(".semantic-edge-table-region") &&
          !element.classList.contains("semantic-edge-table-region")
        ) {
          return false;
        }
        const rect = element.getBoundingClientRect();
        return rect.left < -1 || rect.right > document.documentElement.clientWidth + 1;
      })
      .map((element) => element.getAttribute("aria-label") ?? element.textContent?.trim()),
  }));
  requireCondition(
    reflow.viewportWidth === 320 && reflow.pageWidth <= 320,
    `320px page reflow overflowed: ${JSON.stringify(reflow)}`,
  );
  requireCondition(reflow.edgeRegionContained, "edge table overflow was not contained");
  requireCondition(reflow.semanticVisible && reflow.inspectorVisible, "semantic/inspector content was hidden");
  requireCondition(
    reflow.exactUniversalRule &&
      reflow.semanticRepresentativeCount > 0 &&
      reflow.clippedRepresentatives.length === 0 &&
      reflow.outOfViewportRepresentatives.length === 0,
    `text-spacing clipped or displaced representative content: ${JSON.stringify(reflow)}`,
  );
  await page.screenshot({ path: resolve(narrowScreenshot), fullPage: true });

  await page.setViewport({ width: 1280, height: 900, deviceScaleFactor: 1 });
  const zoomBefore = await page.evaluate(() => ({
    devicePixelRatio,
    innerWidth,
  }));
  await page.evaluate(() => {
    window.addEventListener(
      "graphx-phase3-page-zoom-applied",
      () => { document.documentElement.dataset.phase3PageZoomApplied = "true"; },
      { once: true },
    );
    window.dispatchEvent(
      new CustomEvent("graphx-phase3-page-zoom", { detail: 2 }),
    );
  });
  await page.waitForFunction(
    (before) =>
      document.documentElement.dataset.phase3PageZoomApplied === "true" &&
      Math.max(
        devicePixelRatio / before.devicePixelRatio,
        before.innerWidth / innerWidth,
      ) >= 1.9 &&
      Math.max(
        devicePixelRatio / before.devicePixelRatio,
        before.innerWidth / innerWidth,
      ) <= 2.1,
    { polling: "raf", timeout: 5_000 },
    zoomBefore,
  );
  const zoomReflow = await page.evaluate((before) => ({
    cssSimulation: document.body.style.zoom,
    devicePixelRatio,
    innerWidth,
    viewportWidth: document.documentElement.clientWidth,
    pageWidth: document.documentElement.scrollWidth,
    semanticVisible: document.querySelector("#semantic-topology-region")?.getBoundingClientRect().width > 0,
    resetVisible: document.querySelector('[data-storage-key="graphx.dashboard.presentation"]')?.getBoundingClientRect().width > 0,
    applied: document.documentElement.dataset.phase3PageZoomApplied,
    scale: Math.max(
      devicePixelRatio / before.devicePixelRatio,
      before.innerWidth / innerWidth,
    ),
  }), zoomBefore);
  requireCondition(
    zoomReflow.cssSimulation === "" &&
      zoomReflow.applied === "true" &&
      zoomReflow.scale >= 1.9 && zoomReflow.scale <= 2.1 &&
      zoomReflow.pageWidth <= zoomReflow.viewportWidth &&
      zoomReflow.semanticVisible &&
      zoomReflow.resetVisible,
    `200% zoom reflow failed: ${JSON.stringify(zoomReflow)}`,
  );
  await page.screenshot({ path: resolve(zoomScreenshot), fullPage: true });

  const allowedLocalRequests = new Set([
    "GET /",
    "GET /assets/graphx-dashboard.css",
    "GET /assets/graphx-dashboard.js",
    "GET /api/v1/graph",
    "GET /api/v1/execution/state",
  ]);
  const unexpectedRequests = requests.filter(
    ([method, path]) => !allowedLocalRequests.has(`${method} ${path}`),
  );
  requireCondition(
    unexpectedRequests.length === 0,
    `semantic/focus/preference interactions issued a request outside the explicit allowlist: ${JSON.stringify(unexpectedRequests)}`,
  );
  requireCondition(consoleErrors.length === 0, `browser console errors:\n${consoleErrors.join("\n")}`);

  await browser.close();
  browser = undefined;
  browser = await puppeteer.launch({
    browser: "firefox",
    executablePath: firefox,
    headless: true,
    args: ["--width=1440", "--height=1000"],
    extraPrefsFirefox: { "ui.prefersReducedMotion": 0 },
  });
  const ordinaryPage = await browser.newPage();
  await ordinaryPage.setViewport({ width: 1440, height: 1000, deviceScaleFactor: 1 });
  const ordinaryErrors = [];
  const ordinaryRequests = [];
  ordinaryPage.on("console", (entry) => {
    if (entry.type() === "error") ordinaryErrors.push(entry.text());
  });
  ordinaryPage.on("pageerror", (error) => ordinaryErrors.push(error.message));
  ordinaryPage.on("request", (request) => {
    const url = new URL(request.url());
    if (url.protocol !== "http:" && url.protocol !== "https:") return;
    ordinaryRequests.push([request.method(), url.pathname]);
  });
  await ordinaryPage.goto(`http://127.0.0.1:${port}/`, {
    waitUntil: "networkidle0",
    timeout: 20_000,
  });
  await ordinaryPage.waitForSelector(".topology-shell", { timeout: 20_000 });
  const ordinaryMotion = await ordinaryPage.evaluate(() => ({
    system: matchMedia("(prefers-reduced-motion: reduce)").matches,
    applicationState: document.querySelector(".topology-shell")?.getAttribute("data-reduced-motion"),
    operationDuration: Number(document.querySelector(".topology-shell")?.getAttribute("data-motion-duration-ms")),
    counts: document.querySelector('[data-testid="topology-counts"]')?.textContent ?? "",
  }));
  requireCondition(
    !ordinaryMotion.system &&
      ordinaryMotion.applicationState === "no-preference" &&
      ordinaryMotion.operationDuration === 200 &&
      ordinaryMotion.counts.includes(`${sourceDocument.nodes.length} nodes`) &&
      ordinaryMotion.counts.includes(`${sourceDocument.edges.length} edges`) &&
      ordinaryMotion.counts === reducedMotion.counts,
    `no-preference motion/static-state oracle failed: ${JSON.stringify({ reducedMotion, ordinaryMotion })}`,
  );
  const ordinaryUnexpectedRequests = ordinaryRequests.filter(
    ([method, path]) => !allowedLocalRequests.has(`${method} ${path}`),
  );
  requireCondition(
    ordinaryUnexpectedRequests.length === 0,
    `no-preference page issued request outside allowlist: ${JSON.stringify(ordinaryUnexpectedRequests)}`,
  );
  requireCondition(ordinaryErrors.length === 0, `no-preference console errors:\n${ordinaryErrors.join("\n")}`);
} finally {
  if (browser) await browser.close();
  await stopServer();
}

console.log(
    `phase3 browser PASS graph=${resolve(graphPath)} port=${port} ` +
    `screenshots=${resolve(screenshotPath)},${resolve(narrowScreenshot)},${resolve(zoomScreenshot)}`,
);
