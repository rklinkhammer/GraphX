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
const screenshot = options.get("--screenshot");
const scenario = options.get("--scenario");
const firefox =
  process.env.GRAPHX_FIREFOX_EXECUTABLE ??
  "/Applications/Firefox.app/Contents/MacOS/firefox";

if (
  !dashboard ||
  !graph ||
  !Number.isSafeInteger(port) ||
  !screenshot ||
  !["generic", "fhss", "invalid"].includes(scenario)
) {
  throw new Error(
    "required: --dashboard PATH --graph PATH --port PORT --screenshot PATH " +
      "--scenario generic|fhss|invalid",
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

function requireCondition(condition, detail) {
  if (!condition) {
    throw new Error(detail);
  }
}

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
      // The loopback server is still starting.
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

async function waitCounts(page, authoritativeNodes, groups, edges) {
  await page.waitForFunction(
    (expectedNodes, expectedGroups, expectedEdges) =>
      document.querySelectorAll('[data-testid="graph-node-card"]').length ===
        expectedNodes &&
      document.querySelectorAll('[data-testid="graph-group-card"]').length ===
        expectedGroups &&
      document.querySelectorAll(".react-flow__edge").length === expectedEdges,
    { polling: "raf", timeout: 30_000 },
    authoritativeNodes,
    groups,
    edges,
  );
  await page.waitForFunction(
    () =>
      document.querySelectorAll(".react-flow__minimap-node").length > 0,
    { polling: "raf", timeout: 30_000 },
  );
  const minimapNodes = await page.$$eval(
    ".react-flow__minimap-node",
    (elements) => elements.length,
  );
  requireCondition(
    minimapNodes === authoritativeNodes + groups,
    `minimap node count ${minimapNodes} did not match visible canvas count ${
      authoritativeNodes + groups
    }`,
  );
}

async function activateButton(page, accessibleName) {
  await page.waitForFunction(
    (name) => {
      const button = [...document.querySelectorAll("button")].find(
        (candidate) =>
          candidate.getAttribute("aria-label") === name ||
          candidate.textContent?.trim() === name,
      );
      if (!(button instanceof HTMLButtonElement) || button.disabled) {
        return false;
      }
      button.focus();
      return document.activeElement === button;
    },
    { polling: "raf", timeout: 10_000 },
    accessibleName,
  );
  await page.keyboard.press("Enter");
}

async function clickButton(page, accessibleName) {
  const result = await page.evaluate((name) => {
    const button = [...document.querySelectorAll("button")].find(
      (candidate) =>
        candidate.getAttribute("aria-label") === name ||
        candidate.textContent?.trim() === name,
    );
    if (!(button instanceof HTMLButtonElement) || button.disabled) {
      return false;
    }
    button.click();
    return true;
  }, accessibleName);
  requireCondition(result, `button not available: ${accessibleName}`);
}

async function selectNodeWithKeyboard(page, nodeId) {
  const button = await page.waitForSelector(
    `.react-flow__node[data-id="${nodeId}"] .node-keyboard-select`,
    { timeout: 10_000 },
  );
  const before = await page.evaluate(() => ({
    viewport: document.querySelector(".react-flow__viewport")?.getAttribute("style"),
    layouts: document.querySelector('[data-testid="layout-invocation-count"]')
      ?.textContent,
  }));
  await button.focus();
  await page.keyboard.press("Enter");
  await page.waitForFunction(
    (id) =>
      document
        .querySelector('[data-testid="node-inspector"]')
        ?.textContent?.includes(id),
    { polling: "raf", timeout: 10_000 },
    nodeId,
  );
  const stable = await button.evaluate(
    (element) => element.isConnected && document.activeElement === element,
  );
  const after = await page.evaluate(() => ({
    viewport: document.querySelector(".react-flow__viewport")?.getAttribute("style"),
    layouts: document.querySelector('[data-testid="layout-invocation-count"]')
      ?.textContent,
  }));
  requireCondition(stable, `node ${nodeId} focus/DOM identity was replaced`);
  requireCondition(
    JSON.stringify(after) === JSON.stringify(before),
    `node ${nodeId} selection changed layout/viewport: ${JSON.stringify({ before, after })}`,
  );
}

async function verifyExactRawHandles(page, sourceDocument, label) {
  const result = await page.evaluate((documentSource) => {
    const expected = new Map(
      documentSource.nodes.map((node) => [node.id, new Set()]),
    );
    const encodePort = (edge, prefix, direction) => {
      const numeric = `${prefix}_port`;
      const named = `${prefix}_port_name`;
      return Object.prototype.hasOwnProperty.call(edge, numeric)
        ? `${direction}|index:${edge[numeric]}`
        : `${direction}|name:${String(edge[named]).length}:${edge[named]}`;
    };
    for (const edge of documentSource.edges) {
      expected
        .get(edge.source_node_id)
        ?.add(encodePort(edge, "source", "output"));
      expected
        .get(edge.target_node_id)
        ?.add(encodePort(edge, "target", "input"));
    }
    const actual = new Map();
    for (const card of document.querySelectorAll(".react-flow__node")) {
      const nodeId = card.getAttribute("data-id");
      if (nodeId === null) continue;
      actual.set(
        nodeId,
        [...card.querySelectorAll("[data-handleid]")]
          .map((handle) => handle.getAttribute("data-handleid"))
          .sort(),
      );
    }
    return {
      expected: [...expected.entries()]
        .map(([id, handles]) => [id, [...handles].sort()])
        .sort(),
      actual: [...actual.entries()].sort(),
      presentationHandles: document.querySelectorAll(
        '[data-handleid^="presentation-boundary-"]',
      ).length,
    };
  }, sourceDocument);
  requireCondition(
    JSON.stringify(result.actual) === JSON.stringify(result.expected),
    `${label} raw handles differ from authoritative ports: ${JSON.stringify(result)}`,
  );
  requireCondition(
    result.presentationHandles === 0,
    `${label} raw canvas exposed ${result.presentationHandles} presentation handles`,
  );
}

async function verifyMinimapKeyboard(page, label) {
  const control = await page.waitForSelector(
    '[data-testid="minimap-keyboard-control"]',
    { timeout: 10_000 },
  );
  await control.focus();
  const before = await page.$eval(
    ".react-flow__viewport",
    (element) => element.getAttribute("style"),
  );
  await page.keyboard.press("ArrowRight");
  await page.waitForFunction(
    (prior) =>
      document.querySelector(".react-flow__viewport")?.getAttribute("style") !==
      prior,
    { polling: "raf", timeout: 10_000 },
    before,
  );
  const afterPan = await page.$eval(
    ".react-flow__viewport",
    (element) => element.getAttribute("style"),
  );
  await page.keyboard.press("-");
  await page.waitForFunction(
    (prior) =>
      document.querySelector(".react-flow__viewport")?.getAttribute("style") !==
      prior,
    { polling: "raf", timeout: 10_000 },
    afterPan,
  );
  requireCondition(
    await control.evaluate(
      (element) =>
        element.isConnected &&
        document.activeElement === element &&
        element.getAttribute("aria-keyshortcuts")?.includes("ArrowRight") &&
        element.getAttribute("aria-keyshortcuts")?.includes("-"),
    ),
    `${label} minimap lost focus, semantics, or DOM identity`,
  );
}

async function inspectFirstBundle(page, expectedMembers) {
  await page.waitForSelector(".react-flow__edge.bundle-edge", {
    timeout: 15_000,
  });
  const bundleIds = await page.$$eval(
    ".react-flow__edge.bundle-edge",
    (edges) => edges.map((edge) => edge.getAttribute("data-id")),
  );
  const bundleId = bundleIds[0];
  const bundleEdge = await page.waitForSelector(
    `.react-flow__edge[data-id="${bundleId}"]`,
    { timeout: 10_000 },
  );
  const beforeSelection = await page.evaluate(() => ({
    viewport: document.querySelector(".react-flow__viewport")?.getAttribute("style"),
    layouts: document.querySelector('[data-testid="layout-invocation-count"]')
      ?.textContent,
  }));
  await bundleEdge.evaluate((element) => element.focus());
  await page.keyboard.press("Enter");
  await page.waitForSelector('[data-testid="bundle-inspector"]', {
    timeout: 10_000,
  });
  const bundleStability = await bundleEdge.evaluate((element) => ({
    connected: element.isConnected,
    active: document.activeElement === element,
    activeTag: document.activeElement?.tagName,
    activeClass: document.activeElement?.getAttribute("class"),
  }));
  requireCondition(
    bundleStability.connected && bundleStability.active,
    `bundle selection replaced/unfocused the canvas edge: ${JSON.stringify(bundleStability)}`,
  );
  requireCondition(
    JSON.stringify(
      await page.evaluate(() => ({
        viewport: document
          .querySelector(".react-flow__viewport")
          ?.getAttribute("style"),
        layouts: document.querySelector(
          '[data-testid="layout-invocation-count"]',
        )?.textContent,
      })),
    ) === JSON.stringify(beforeSelection),
    "bundle selection changed deterministic layout or viewport",
  );
  const inspection = await page.$eval(
    '[data-testid="bundle-inspector"]',
    (element) => element.textContent ?? "",
  );
  requireCondition(
    inspection.includes(`Authoritative member count${expectedMembers}`),
    `bundle member count was not ${expectedMembers}: ${inspection}`,
  );
  const memberButtons = await page.$$(
    '[data-testid="bundle-inspector"] button[aria-label^="Inspect authoritative edge "]',
  );
  requireCondition(
    memberButtons.length === expectedMembers,
    `bundle exposed ${memberButtons.length} members, expected ${expectedMembers}`,
  );
  const beforeMemberSelection = await page.evaluate(() => ({
    viewport: document.querySelector(".react-flow__viewport")?.getAttribute("style"),
    layouts: document.querySelector('[data-testid="layout-invocation-count"]')
      ?.textContent,
  }));
  await memberButtons[0].click();
  await page.waitForSelector('[data-testid="edge-inspector"]', {
    timeout: 10_000,
  });
  const edgeInspection = await page.$eval(
    '[data-testid="edge-inspector"]',
    (element) => element.textContent ?? "",
  );
  requireCondition(
    edgeInspection.includes("Source node") &&
      edgeInspection.includes("Source port") &&
      edgeInspection.includes("Target node") &&
      edgeInspection.includes("Target port"),
    "bundle member did not resolve to the exact authoritative edge inspector",
  );
  requireCondition(
    JSON.stringify(
      await page.evaluate(() => ({
        viewport: document
          .querySelector(".react-flow__viewport")
          ?.getAttribute("style"),
        layouts: document.querySelector(
          '[data-testid="layout-invocation-count"]',
        )?.textContent,
      })),
    ) === JSON.stringify(beforeMemberSelection),
    "bundle-member selection changed deterministic layout or viewport",
  );
}

async function editNode(page, nodeId, marker) {
  await activateButton(page, "Nodes & parameters");
  await page.waitForSelector("#nodesTable", { timeout: 10_000 });
  await clickButton(page, nodeId);
  await clickButton(page, "Edit node parameters");
  await page.waitForSelector("#node-config", { timeout: 10_000 });
  await page.$eval(
    "#node-config",
    (element, [key, value]) => {
      const parsed = JSON.parse(element.value);
      parsed[key] = value;
      const encoded = JSON.stringify(parsed);
      const setter = Object.getOwnPropertyDescriptor(
        HTMLTextAreaElement.prototype,
        "value",
      )?.set;
      setter?.call(element, encoded);
      element.dispatchEvent(
        new InputEvent("input", {
          bubbles: true,
          inputType: "insertText",
          data: encoded,
        }),
      );
    },
    marker,
  );
  await clickButton(page, "Save parameters");
  await page.waitForFunction(
    (id) =>
      document.body.textContent?.includes(
        `Parameters for ${id} updated in memory`,
      ),
    { polling: "raf", timeout: 10_000 },
    nodeId,
  );
  await activateButton(page, "Topology");
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
  const mutationRequests = [];
  let graphFetches = 0;
  page.on("console", (entry) => {
    if (entry.type() === "error") {
      consoleErrors.push(entry.text());
    }
  });
  page.on("pageerror", (error) => consoleErrors.push(error.message));
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname === "/api/v1/graph") {
      graphFetches += 1;
    }
    if (!["GET", "HEAD"].includes(request.method())) {
      mutationRequests.push([request.method(), url.pathname]);
    }
  });

  await page.goto(`http://127.0.0.1:${port}/`, {
    waitUntil: "networkidle0",
    timeout: 20_000,
  });
  await page.waitForSelector('[data-testid="topology-counts"]', {
    timeout: 20_000,
  });
  const graphResponse = await (
    await fetch(`http://127.0.0.1:${port}/api/v1/graph`)
  ).json();
  const sourceDocument = graphResponse.data;

  if (scenario === "generic") {
    requireCondition(
      sourceDocument.nodes.length === 9 && sourceDocument.edges.length === 9,
      "generic source cardinality was not 9/9",
    );
    await waitCounts(page, 7, 4, 7);
    const initial = await page.$eval(
      '[data-testid="grouped-counts"]',
      (element) => element.textContent ?? "",
    );
    requireCondition(
      initial.includes("7 authoritative nodes") &&
        initial.includes("4 groups") &&
        initial.includes("2 bundles") &&
        initial.includes("hidden: 2 nodes, 4 edges"),
      `generic grouped counts were incorrect: ${initial}`,
    );
    requireCondition(
      (await page.$(".react-flow__minimap")) !== null,
      "generic minimap was absent",
    );
    await verifyMinimapKeyboard(page, "generic");
    requireCondition(graphFetches === 1, `initial graph fetch count was ${graphFetches}`);

    await activateButton(page, "Raw topology");
    await waitCounts(page, 9, 0, 9);
    await verifyExactRawHandles(page, sourceDocument, "generic");
    await selectNodeWithKeyboard(page, "interior_1");
    await activateButton(page, "Grouped topology");
    await waitCounts(page, 7, 4, 7);
    requireCondition(
      await page.$eval(
        '.react-flow__node[data-id="parallel-stage"] .graph-group-card',
        (element) => element.classList.contains("contains-selection"),
      ),
      "collapsed ancestor did not indicate hidden authoritative selection",
    );
    requireCondition(
      await page.$eval(
        '[data-testid="node-inspector"]',
        (element) => element.textContent?.includes("interior_1") ?? false,
      ),
      "hidden authoritative node inspector selection was lost",
    );

    for (let cycle = 0; cycle < 2; ++cycle) {
      await activateButton(page, "Expand group parallel-stage");
      await waitCounts(page, 9, 4, 9);
      await activateButton(page, "Collapse group parallel-stage");
      await waitCounts(page, 7, 4, 7);
    }
    await inspectFirstBundle(page, 2);

    await activateButton(page, "Isolate group parallel-stage");
    await waitCounts(page, 2, 0, 0);
    const breadcrumbs = await page.$eval(
      'nav[aria-label="Group breadcrumbs"]',
      (element) => element.textContent ?? "",
    );
    requireCondition(
      breadcrumbs.includes("Processing pipeline") &&
        breadcrumbs.includes("Parallel stage"),
      `nested breadcrumbs were incomplete: ${breadcrumbs}`,
    );
    await activateButton(page, "Return to parent");
    await page.waitForFunction(
      () => document.body.textContent?.includes("Isolated: Processing pipeline"),
      { polling: "raf", timeout: 10_000 },
    );
    await activateButton(page, "Return to all topology");
    await waitCounts(page, 7, 4, 7);

    await editNode(page, "source_1", ["phase2_browser_marker", true]);
    await waitCounts(page, 7, 4, 7);
    requireCondition(
      graphFetches === 2,
      `generic PATCH refresh graph fetch count was ${graphFetches}`,
    );
    const refreshed = await (
      await fetch(`http://127.0.0.1:${port}/api/v1/graph`)
    ).json();
    requireCondition(
      JSON.stringify(refreshed.data.presentation) ===
        JSON.stringify(sourceDocument.presentation),
      "PATCH did not preserve authored presentation metadata exactly",
    );
    requireCondition(
      !JSON.stringify(refreshed.data).includes("collapsedGroupIds") &&
        !JSON.stringify(refreshed.data).includes("isolatedGroupId") &&
        !JSON.stringify(refreshed.data).includes("presentation-boundary"),
      "local presentation state leaked into authoritative JSON",
    );
    requireCondition(
      mutationRequests.length === 1 &&
        mutationRequests[0][0] === "PATCH" &&
        mutationRequests[0][1] === "/api/v1/nodes/source_1",
      `unexpected generic mutations: ${JSON.stringify(mutationRequests)}`,
    );
  } else if (scenario === "fhss") {
    requireCondition(
      sourceDocument.nodes.length === 75 && sourceDocument.edges.length === 137,
      "FHSS source cardinality was not 75/137",
    );
    const group = sourceDocument.presentation?.groups?.[0];
    const expectedMembers = Array.from(
      { length: 64 },
      (_, index) => `detector_${index}`,
    );
    requireCondition(
      group?.id === "detector-bank" &&
        JSON.stringify(group.members) === JSON.stringify(expectedMembers),
      "FHSS authored detector group was not exactly detector_0..detector_63",
    );
    const tuples = new Set(
      sourceDocument.edges.map((edge) =>
        JSON.stringify([
          edge.source_node_id,
          edge.source_port,
          edge.target_node_id,
          edge.target_port,
        ]),
      ),
    );
    for (let index = 0; index < 64; ++index) {
      requireCondition(
        tuples.has(
          JSON.stringify([
            "channelizer",
            index,
            `detector_${index}`,
            0,
          ]),
        ) &&
          tuples.has(
            JSON.stringify([
              `detector_${index}`,
              0,
              "merge",
              index + 1,
            ]),
          ),
        `FHSS exact bank mapping missing at index ${index}`,
      );
    }

    await waitCounts(page, 11, 1, 11);
    const collapsedCounts = await page.$eval(
      '[data-testid="grouped-counts"]',
      (element) => element.textContent ?? "",
    );
    requireCondition(
      collapsedCounts.includes("2 bundles") &&
        collapsedCounts.includes("hidden: 64 nodes, 128 edges"),
      `FHSS collapsed counts were incorrect: ${collapsedCounts}`,
    );
    requireCondition(
      (await page.$(".react-flow__minimap")) !== null,
      "FHSS minimap was absent",
    );
    await verifyMinimapKeyboard(page, "FHSS");

    await activateButton(page, "Raw topology");
    await waitCounts(page, 75, 0, 137);
    await verifyExactRawHandles(page, sourceDocument, "FHSS");
    await selectNodeWithKeyboard(page, "detector_31");
    await activateButton(page, "Grouped topology");
    await waitCounts(page, 11, 1, 11);
    requireCondition(
      await page.$eval(
        '.react-flow__node[data-id="detector-bank"] .graph-group-card',
        (element) => element.classList.contains("contains-selection"),
      ),
      "FHSS collapsed group did not indicate hidden detector selection",
    );
    await inspectFirstBundle(page, 64);

    for (let cycle = 0; cycle < 2; ++cycle) {
      await activateButton(page, "Expand group detector-bank");
      await waitCounts(page, 75, 1, 137);
      requireCondition(
        (await page.$('.react-flow__node[data-id="detector_31"]')) !== null,
        "FHSS expansion did not restore detector_31",
      );
      await activateButton(page, "Collapse group detector-bank");
      await waitCounts(page, 11, 1, 11);
    }
    await activateButton(page, "Expand group detector-bank");
    await waitCounts(page, 75, 1, 137);
    requireCondition(
      graphFetches === 1,
      `FHSS hierarchy operations refetched graph ${graphFetches} times`,
    );
    requireCondition(
      mutationRequests.length === 0,
      `FHSS hierarchy operations mutated state: ${JSON.stringify(mutationRequests)}`,
    );
  } else {
    await page.waitForSelector(".hierarchy-diagnostic", { timeout: 10_000 });
    const diagnostic = await page.$eval(
      ".hierarchy-diagnostic",
      (element) => element.textContent ?? "",
    );
    requireCondition(
      diagnostic.includes("Presentation grouping rejected") &&
        diagnostic.includes("overlapping_group_member") &&
        diagnostic.includes("Raw topology preserved"),
      `invalid hierarchy diagnostic was incomplete: ${diagnostic}`,
    );
    await waitCounts(
      page,
      sourceDocument.nodes.length,
      0,
      sourceDocument.edges.length,
    );
    await verifyExactRawHandles(page, sourceDocument, "invalid hierarchy");
    requireCondition(
      await page.$eval(
        "button",
        () => {
          const grouped = [...document.querySelectorAll("button")].find(
            (button) => button.textContent?.trim() === "Grouped topology",
          );
          const raw = [...document.querySelectorAll("button")].find(
            (button) => button.textContent?.trim() === "Raw topology",
          );
          return (
            grouped instanceof HTMLButtonElement &&
            grouped.disabled &&
            raw?.getAttribute("aria-pressed") === "true"
          );
        },
      ),
      "invalid hierarchy did not force explicit Raw topology mode",
    );
    requireCondition(
      mutationRequests.length === 0,
      `invalid hierarchy caused mutation: ${JSON.stringify(mutationRequests)}`,
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
  `hierarchy browser PASS scenario=${scenario} port=${port} ` +
    `screenshot=${resolve(screenshot)}`,
);
