import { spawn } from "node:child_process";
import { mkdir, readFile } from "node:fs/promises";
import { dirname, extname, resolve } from "node:path";
import process from "node:process";

import puppeteer from "puppeteer-core";

const options = new Map();
for (let index = 2; index + 1 < process.argv.length; index += 2) {
  options.set(process.argv[index], process.argv[index + 1]);
}
const dashboard = options.get("--dashboard");
const graph = options.get("--graph");
const plugins = options.get("--plugins");
const port = Number(options.get("--port"));
const screenshot = options.get("--screenshot");
const exerciseLifecycle = options.get("--exercise-lifecycle") === "true";
const expectedNodes = Number(options.get("--expect-nodes"));
const expectedEdges = Number(options.get("--expect-edges"));
const firefox = process.env.GRAPHX_FIREFOX_EXECUTABLE ??
  "/Applications/Firefox.app/Contents/MacOS/firefox";
if (!dashboard || !graph || !Number.isSafeInteger(port) || !screenshot) {
  throw new Error("required: --dashboard PATH --graph PATH --port PORT --screenshot PATH");
}
const requireCondition = (condition, message) => {
  if (!condition) throw new Error(message);
};
const sourceDocument = JSON.parse(await readFile(resolve(graph), "utf8"));
if (Number.isSafeInteger(expectedNodes)) {
  requireCondition(sourceDocument.nodes.length === expectedNodes,
    `fixture node count ${sourceDocument.nodes.length} != ${expectedNodes}`);
}
if (Number.isSafeInteger(expectedEdges)) {
  requireCondition(sourceDocument.edges.length === expectedEdges,
    `fixture edge count ${sourceDocument.edges.length} != ${expectedEdges}`);
}

const serverArguments = ["--graph", resolve(graph), "--port", String(port)];
if (plugins) serverArguments.push("--plugins", resolve(plugins));
const server = spawn(resolve(dashboard), serverArguments, { stdio: ["ignore", "pipe", "pipe"] });
let output = "";
server.stdout.on("data", (data) => { output += data; });
server.stderr.on("data", (data) => { output += data; });
let browser;

async function waitFor(condition, detail, timeout = 12_000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (await condition()) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 25));
  }
  throw new Error(detail);
}

async function launch(reducedMotion) {
  const instance = await puppeteer.launch({
    browser: "firefox", executablePath: firefox, headless: true,
    args: ["--width=1280", "--height=900"],
    extraPrefsFirefox: { "ui.prefersReducedMotion": reducedMotion ? 1 : 0 },
  });
  const extension = await instance.installExtension(resolve("test/fixtures/firefox-page-zoom"));
  requireCondition(Boolean(extension), "Firefox page-zoom oracle extension did not install");
  return instance;
}

async function activate(page, label) {
  const enabled = await page.evaluate((name) => {
    const button = [...document.querySelectorAll("button")].find(
      (candidate) => candidate.textContent?.trim() === name ||
        candidate.getAttribute("aria-label") === name,
    );
    if (!(button instanceof HTMLButtonElement) || button.disabled) return false;
    button.focus();
    button.click();
    return true;
  }, label);
  requireCondition(enabled, `button unavailable: ${label}`);
}

async function waitCommand(page, command, priorCount) {
  await page.waitForFunction((name, count) => {
    const rows = [...document.querySelectorAll(".command-history li")];
    return rows.length > count && rows.some((row) =>
      row.querySelector("strong")?.textContent === name &&
      /completed|failed|cancelled/.test(row.textContent ?? ""));
  }, { polling: "raf", timeout: 20_000 }, command, priorCount);
}

async function executeButton(page, label, command) {
  const count = await page.$$eval(".command-history li", (rows) => rows.length);
  await activate(page, label);
  await waitCommand(page, command, count);
}

async function executePalette(page, command) {
  const count = await page.$$eval(".command-history li", (rows) => rows.length);
  await page.select("#command-palette", command);
  await activate(page, "Submit typed command");
  await waitCommand(page, command, count);
}

async function beginFollowedButton(page, label, command) {
  const count = await page.$$eval(".command-history li", (rows) => rows.length);
  await activate(page, label);
  await page.waitForFunction((name, priorCount) => {
    const rows = [...document.querySelectorAll(".command-history li")];
    return rows.length > priorCount && rows.some((row) =>
      row.querySelector("strong")?.textContent === name &&
      /accepted|running/.test(row.textContent ?? "") &&
      !/completed|failed|cancelled|expired/.test(row.textContent ?? ""));
  }, { polling: "raf", timeout: 5_000 }, command, count);
  return count;
}

const screenshotExtension = extname(screenshot) || ".png";
const screenshotStem = resolve(screenshot).slice(
  0, resolve(screenshot).length - extname(screenshot).length,
);
const evidencePath = (motion, state) =>
  `${screenshotStem}-${motion}-${state}${screenshotExtension}`;

async function captureEvidence(page, motion, state) {
  const path = evidencePath(motion, state);
  const bytes = Buffer.from(await page.screenshot({ path, fullPage: true }));
  requireCondition(bytes.length > 24 && bytes.subarray(1, 4).toString() === "PNG",
    `invalid PNG evidence: ${path}`);
  const width = bytes.readUInt32BE(16);
  const height = bytes.readUInt32BE(20);
  requireCondition(width > 0 && height > 0,
    `empty screenshot evidence dimensions: ${path}`);
  return { path, width, height };
}

async function applyActualZoom(page, marker) {
  const before = await page.evaluate(() => ({ devicePixelRatio, innerWidth }));
  await page.evaluate((zoomMarker) => {
    window.addEventListener("graphx-phase3-page-zoom-applied", () => {
      document.documentElement.dataset[zoomMarker] = "true";
    }, { once: true });
    window.dispatchEvent(new CustomEvent("graphx-phase3-page-zoom", { detail: 2 }));
  }, marker);
  await page.waitForFunction((zoomBefore, zoomMarker) =>
    document.documentElement.dataset[zoomMarker] === "true" &&
    Math.max(devicePixelRatio / zoomBefore.devicePixelRatio,
      zoomBefore.innerWidth / innerWidth) >= 1.9,
  { polling: "raf", timeout: 5_000 }, before, marker);
  requireCondition(await page.evaluate(() =>
    document.body.style.zoom === "" &&
    document.documentElement.scrollWidth <= document.documentElement.clientWidth),
  "actual Firefox 200% zoom caused page overflow or used CSS zoom");
}

try {
  await waitFor(async () => {
    if (server.exitCode !== null) throw new Error(`dashboard exited\n${output}`);
    try { return (await fetch(`http://127.0.0.1:${port}/`)).ok; } catch { return false; }
  }, `dashboard did not become ready\n${output}`);
  const graphResponse = await fetch(`http://127.0.0.1:${port}/api/v1/graph`).then((response) => response.json());
  requireCondition(graphResponse.success && graphResponse.snapshot?.content_identity,
    "graph response omitted atomic export metadata");
  requireCondition(graphResponse.data.nodes.length === sourceDocument.nodes.length &&
    graphResponse.data.edges.length === sourceDocument.edges.length,
  "authoritative graph count mismatch");
  const metricsResponse = await fetch(`http://127.0.0.1:${port}/api/v1/metrics`).then((response) => response.json());
  requireCondition(metricsResponse.success && metricsResponse.data?.schema_version === 1,
    "generic metric snapshot resource is unavailable");

  browser = await launch(true);
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 900, deviceScaleFactor: 1 });
  const errors = [];
  const requests = [];
  page.on("console", (entry) => { if (entry.type() === "error") errors.push(entry.text()); });
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.protocol === "http:" || url.protocol === "https:") {
      requests.push(`${request.method()} ${url.pathname}`);
    }
  });
  await page.goto(`http://127.0.0.1:${port}/`, { waitUntil: "networkidle0", timeout: 20_000 });
  await page.waitForSelector('[data-testid="topology-counts"]');
  requireCondition(await page.$eval('[data-testid="topology-counts"]', (element, nodes, edges) =>
    (element.textContent ?? "").includes(`${nodes} nodes`) &&
    (element.textContent ?? "").includes(`${edges} edges`),
  sourceDocument.nodes.length, sourceDocument.edges.length), "rendered topology count mismatch");
  requireCondition(await page.$eval("#command-palette", (element) => element.options.length === 6),
    "typed command discovery was not rendered");
  requireCondition(await page.$eval('[data-testid="metrics-status"]', (element) =>
    /available|unavailable|stale/.test(element.textContent ?? "")),
  "metric availability was not textual");
  await activate(page, "Semantic topology");
  await page.waitForFunction((count) =>
    document.querySelectorAll('[data-semantic-record^="edge:"]').length === count,
  { polling: "raf", timeout: 20_000 }, sourceDocument.edges.length);
  const semanticEdges = await page.$$eval('[data-semantic-record^="edge:"]', (rows) =>
    rows.map((row) => row.textContent ?? ""));
  for (const edge of sourceDocument.edges) {
    const sourcePort = Object.hasOwn(edge, "source_port")
      ? `index ${edge.source_port}` : `name “${edge.source_port_name}”`;
    const targetPort = Object.hasOwn(edge, "target_port")
      ? `index ${edge.target_port}` : `name “${edge.target_port_name}”`;
    requireCondition(semanticEdges.some((row) => row.includes(edge.source_node_id) &&
      row.includes(sourcePort) && row.includes(edge.target_node_id) && row.includes(targetPort)),
    `exact semantic edge tuple missing: ${edge.source_node_id} ${sourcePort} -> ${edge.target_node_id} ${targetPort}`);
  }

  // Export is an independent browser oracle: capture the actual Blob, filename,
  // and revocation without allowing the headless browser to write a download.
  await page.evaluate(() => {
    window.__phase4Download = { revoked: false };
    URL.createObjectURL = (blob) => {
      window.__phase4Download.blob = blob;
      return "blob:graphx-phase4-export";
    };
    URL.revokeObjectURL = (url) => {
      if (url === "blob:graphx-phase4-export") window.__phase4Download.revoked = true;
    };
    HTMLAnchorElement.prototype.click = function click() {
      window.__phase4Download.filename = this.download;
      window.__phase4Download.href = this.href;
    };
  });
  await activate(page, "Export graph snapshot");
  const exported = await page.evaluate(async () => ({
    filename: window.__phase4Download.filename,
    revoked: window.__phase4Download.revoked,
    document: JSON.parse(await window.__phase4Download.blob.text()),
  }));
  requireCondition(exported.revoked &&
    exported.filename === `graphx-graph-r${graphResponse.snapshot.coordinator_revision}-${graphResponse.snapshot.content_identity.replace(/[^A-Za-z0-9]/g, "").slice(0, 12)}.json`,
  `export filename/revocation mismatch: ${JSON.stringify(exported)}`);
  requireCondition(exported.document.artifact === "graphx.graph-export" &&
    exported.document.version === 1 &&
    exported.document.coordinator_revision === graphResponse.snapshot.coordinator_revision &&
    exported.document.content_identity === graphResponse.snapshot.content_identity &&
    JSON.stringify(exported.document.graph) === JSON.stringify(graphResponse.data),
  "export envelope did not preserve the atomic authoritative document");
  for (const excluded of ["metrics", "execution", "command_history", "selection", "preferences"]) {
    requireCondition(!Object.hasOwn(exported.document, excluded), `export leaked ${excluded}`);
  }

  if (exerciseLifecycle) {
    requireCondition(Boolean(plugins), "--exercise-lifecycle requires --plugins");
    await executeButton(page, "Configure", "configure");
    await executePalette(page, "init");
    await executeButton(page, "Start", "start");
    await page.waitForFunction(() =>
      document.querySelector('[data-testid="metrics-status"]')?.textContent?.includes("available"),
    { polling: "raf", timeout: 10_000 });
    const liveMetrics = await fetch(`http://127.0.0.1:${port}/api/v1/metrics`).then((response) => response.json());
    requireCondition(liveMetrics.data.graph_generation > 0 &&
      liveMetrics.data.schemas.every((schema) => schema.graph_generation === liveMetrics.data.graph_generation) &&
      liveMetrics.data.values.every((value) => value.graph_generation === liveMetrics.data.graph_generation),
    "metric generation/schema/value coherence failed");
    requireCondition(liveMetrics.data.values.some((value) =>
      value.target.kind === "node" && value.target.node_id === "source_1"),
    "exact source_1 metric identity was not observed");
    requireCondition(liveMetrics.data.values.some((value) =>
      value.target.kind === "node" && value.target.node_id === "sink_1"),
    "exact sink_1 metric identity was not observed");
    await activate(page, "Topology");
    await page.waitForSelector(".topology-shell");
    await page.waitForFunction(() =>
      [...document.querySelectorAll(".runtime-values li")].some((row) =>
        (row.textContent ?? "").includes("sampled ")),
    { polling: "raf", timeout: 5_000 });
    const initialMetricText = await page.$$eval(".runtime-values li", (rows) =>
      rows.map((row) => row.textContent ?? "").filter((text) => text.includes("sampled ")));
    requireCondition(initialMetricText.length > 0,
      "accepted runtime values and sample times were not rendered");
    const layoutBeforeMetricUpdate = await page.$eval(
      '[data-testid="layout-invocation-count"]', (element) => element.textContent,
    );
    const metricTextBeforeUpdate = initialMetricText.join("\n");
    const pollCountBeforeUpdate = requests.filter(
      (request) => request === "GET /api/v1/metrics",
    ).length;
    await waitFor(() => Promise.resolve(requests.filter(
      (request) => request === "GET /api/v1/metrics",
    ).length > pollCountBeforeUpdate), "no unpaused metric-only poll update was observed");
    await page.waitForFunction((before) =>
      [...document.querySelectorAll(".runtime-values li")]
        .map((row) => row.textContent ?? "").filter((text) => text.includes("sampled "))
        .join("\n") !== before,
    { polling: "raf", timeout: 5_000 }, metricTextBeforeUpdate);
    requireCondition(await page.$eval('[data-testid="layout-invocation-count"]',
      (element, before) => element.textContent === before, layoutBeforeMetricUpdate),
    "an unpaused metric-only update triggered topology relayout");
    for (const value of liveMetrics.data.values.filter((candidate) =>
      candidate.target.kind === "edge")) {
      requireCondition(value.target.source_node_id && value.target.target_node_id &&
        ["index", "name"].includes(value.target.source_port?.kind) &&
        ["index", "name"].includes(value.target.target_port?.kind),
      "edge metric omitted part of its exact port tuple");
    }

    // Pause must stop polling and activity animation while execution remains independent.
    await waitFor(() => Promise.resolve(requests.filter((request) => request === "GET /api/v1/metrics").length >= 2),
      "two metric polls were not observed");
    await page.evaluate(() => {
      document.documentElement.dataset.phase4PauseStart = String(performance.now());
    });
    const layoutCountBeforePause = await page.$eval(
      '[data-testid="layout-invocation-count"]', (element) => element.textContent,
    );
    // The metric-only update above replaces the single retained snapshot. Capture
    // that current snapshot—not the deliberately superseded initial one—as the
    // oracle immediately before pausing.
    const retainedSnapshot = await page.evaluate(() => {
      const metricText = [...document.querySelectorAll(".runtime-values li")]
        .map((row) => row.textContent ?? "");
      const statusText = document.querySelector('[data-testid="metrics-status"]')?.textContent ?? "";
      const captureTime = statusText.match(/captured ([^;]+)/)?.[1] ?? "";
      return { metricText, captureTime };
    });
    requireCondition(retainedSnapshot.metricText.length > 0 && retainedSnapshot.captureTime.length > 0,
      "latest runtime snapshot was not rendered immediately before pause");
    await activate(page, "Pause runtime updates");
    const pausedCount = requests.filter((request) => request === "GET /api/v1/metrics").length;
    await page.waitForFunction((count) =>
      document.querySelector('[data-testid="metrics-status"]')?.textContent?.includes("paused") &&
      performance.now() > Number(document.documentElement.dataset.phase4PauseStart ?? 0) + 2200,
    { polling: "raf", timeout: 5_000 }, pausedCount);
    requireCondition(requests.filter((request) => request === "GET /api/v1/metrics").length === pausedCount,
      "metrics polling continued while paused");
    const pausedMetricText = await page.$$eval(".runtime-values li", (rows) =>
      rows.map((row) => row.textContent ?? ""));
    requireCondition(JSON.stringify(pausedMetricText) === JSON.stringify(retainedSnapshot.metricText),
      "pause did not preserve the last accepted metric value and sample time");
    requireCondition(await page.$eval('[data-testid="metrics-status"]',
      (element, captureTime) => (element.textContent ?? "").includes("paused") &&
        (element.textContent ?? "").includes(captureTime), retainedSnapshot.captureTime),
    "paused status omitted the retained snapshot capture time");
    requireCondition(await page.$eval('[data-testid="layout-invocation-count"]',
      (element, before) => element.textContent === before, layoutCountBeforePause),
    "pause or retained metric rendering triggered topology relayout");
    requireCondition(await page.$$eval(".react-flow__edge.animated", (edges) => edges.length === 0),
      "edge animation continued while paused");
    await activate(page, "Resume runtime updates");
    await waitFor(() => Promise.resolve(
      requests.filter((request) => request === "GET /api/v1/metrics").length > pausedCount),
    "metrics polling did not resume");

    // Stop producing samples without stopping the runtime and require the
    // browser's independent three-second freshness policy to become visible.
    await page.waitForFunction(() =>
      document.querySelector('[data-testid="metrics-status"]')?.textContent?.includes("stale"),
    { polling: 100, timeout: 8_000 });

    await executeButton(page, "Stop", "stop");
    const stoppedMetrics = await fetch(`http://127.0.0.1:${port}/api/v1/metrics`).then((response) => response.json());
    requireCondition(stoppedMetrics.data.availability.state === "unavailable" &&
      stoppedMetrics.data.values.every((value) => value.availability === "unavailable" && value.value === null),
    "stopped metrics were not unavailable/null");
    await executePalette(page, "join");
    const priorGeneration = stoppedMetrics.data.graph_generation;
    for (const [nodeId, nodeConfig] of [
      ["source_1", { message_count: 100_000_000 }],
      ["sink_1", { expected_message_count: 100_000_000 }],
    ]) {
      const response = await fetch(
        `http://127.0.0.1:${port}/api/v1/nodes/${encodeURIComponent(nodeId)}`,
        { method: "PATCH", headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ node_config: nodeConfig }) },
      );
      requireCondition(response.ok, `unable to prepare blocking lifecycle node ${nodeId}`);
    }
    await executePalette(page, "configure");
    await page.waitForFunction((generation) => {
      const summary = document.querySelector('[data-testid="revision-summary"]')?.textContent ?? "";
      const status = document.querySelector('[data-testid="metrics-status"]')?.textContent ?? "";
      const match = summary.match(/generation\s+(\d+)/);
      return Boolean(match && Number(match[1]) > generation && status.includes("unavailable"));
    }, { polling: "raf", timeout: 10_000 }, priorGeneration);
    await executeButton(page, "Init", "init");
    await executePalette(page, "start");
    await page.waitForFunction((generation) => {
      const summary = document.querySelector('[data-testid="revision-summary"]')?.textContent ?? "";
      const match = summary.match(/generation\s+(\d+)/);
      return Boolean(match && Number(match[1]) > generation &&
        document.querySelector('[data-testid="metrics-status"]')?.textContent?.includes("available"));
    }, { polling: "raf", timeout: 10_000 }, priorGeneration);
    const runHistoryCount = await beginFollowedButton(page, "Run", "run");
    await executeButton(page, "Stop", "stop");
    await waitCommand(page, "run", runHistoryCount);
    await executeButton(page, "Join", "join");
    requireCondition(requests.some((request) => request === "POST /api/v1/execution/commands/run") &&
      requests.some((request) => /^GET \/api\/v1\/execution\/operations\/op-/.test(request)),
    "accepted Run was not polled through its operation resource");
  }

  await mkdir(dirname(resolve(screenshot)), { recursive: true });
  await activate(page, "Topology");
  await page.waitForSelector(".topology-shell");
  const motion = await page.evaluate(() => ({
    reduced: matchMedia("(prefers-reduced-motion: reduce)").matches,
    state: document.querySelector(".topology-shell")?.getAttribute("data-reduced-motion"),
  }));
  requireCondition(motion.reduced && motion.state === "reduce", "reduced-motion path was not active");
  const evidence = [await captureEvidence(page, "reduced", "desktop")];

  // Text spacing and 320-CSS-pixel reflow are objective DOM geometry checks.
  await page.setViewport({ width: 320, height: 900, deviceScaleFactor: 1 });
  const spacingStyle = await page.addStyleTag({ content: "*{line-height:1.5!important;letter-spacing:.12em!important;word-spacing:.16em!important}p{margin-bottom:2em!important}" });
  const narrow = await page.evaluate(() => ({
    page: document.documentElement.scrollWidth,
    viewport: document.documentElement.clientWidth,
    controls: [...document.querySelectorAll("button,select,input")].every((element) => {
      const box = element.getBoundingClientRect();
      return box.width > 0 && box.height > 0;
    }),
  }));
  requireCondition(narrow.page <= narrow.viewport && narrow.controls,
    `320px/text-spacing reflow failed: ${JSON.stringify(narrow)}`);
  evidence.push(await captureEvidence(page, "reduced", "narrow-text-spacing"));
  await spacingStyle.evaluate((element) => element.remove());

  await page.setViewport({ width: 1280, height: 900, deviceScaleFactor: 1 });
  await applyActualZoom(page, "phase4ReducedZoom");
  evidence.push(await captureEvidence(page, "reduced", "zoom-200"));

  const allowed = (request) => request === "GET /" ||
    request === "GET /assets/graphx-dashboard.css" ||
    request === "GET /assets/graphx-dashboard.js" ||
    request === "GET /api/v1/graph" ||
    request === "GET /api/v1/execution/state" ||
    request === "GET /api/v1/execution/commands" ||
    request === "GET /api/v1/metrics" ||
    /^PATCH \/api\/v1\/nodes\/(source_1|sink_1)$/.test(request) ||
    /^POST \/api\/v1\/execution\/commands\/(configure|init|start|run|stop|join)$/.test(request) ||
    /^GET \/api\/v1\/execution\/operations\/op-[A-Za-z0-9-]+$/.test(request);
  const unexpected = requests.filter((request) => !allowed(request));
  requireCondition(unexpected.length === 0, `request outside allowlist: ${JSON.stringify(unexpected)}`);
  requireCondition(errors.length === 0, `browser console errors: ${errors.join("; ")}`);

  await browser.close();
  browser = await launch(false);
  const ordinary = await browser.newPage();
  const ordinaryErrors = [];
  ordinary.on("console", (entry) => {
    if (entry.type() === "error") ordinaryErrors.push(entry.text());
  });
  ordinary.on("pageerror", (error) => ordinaryErrors.push(error.message));
  await ordinary.goto(`http://127.0.0.1:${port}/`, { waitUntil: "networkidle0", timeout: 20_000 });
  await ordinary.waitForSelector(".topology-shell");
  requireCondition(await ordinary.evaluate(() =>
    !matchMedia("(prefers-reduced-motion: reduce)").matches &&
    document.querySelector(".topology-shell")?.getAttribute("data-reduced-motion") === "no-preference"),
  "no-preference motion path was not active");
  await ordinary.setViewport({ width: 1280, height: 900, deviceScaleFactor: 1 });
  evidence.push(await captureEvidence(ordinary, "ordinary", "desktop"));
  await ordinary.setViewport({ width: 320, height: 900, deviceScaleFactor: 1 });
  const ordinarySpacingStyle = await ordinary.addStyleTag({ content: "*{line-height:1.5!important;letter-spacing:.12em!important;word-spacing:.16em!important}p{margin-bottom:2em!important}" });
  const ordinaryNarrow = await ordinary.evaluate(() => ({
    page: document.documentElement.scrollWidth,
    viewport: document.documentElement.clientWidth,
    controls: [...document.querySelectorAll("button,select,input")].every((element) => {
      const box = element.getBoundingClientRect();
      return box.width > 0 && box.height > 0;
    }),
  }));
  requireCondition(ordinaryNarrow.page <= ordinaryNarrow.viewport && ordinaryNarrow.controls,
    `ordinary-motion 320px/text-spacing reflow failed: ${JSON.stringify(ordinaryNarrow)}`);
  evidence.push(await captureEvidence(ordinary, "ordinary", "narrow-text-spacing"));
  await ordinarySpacingStyle.evaluate((element) => element.remove());
  await ordinary.setViewport({ width: 1280, height: 900, deviceScaleFactor: 1 });
  await applyActualZoom(ordinary, "phase4OrdinaryZoom");
  evidence.push(await captureEvidence(ordinary, "ordinary", "zoom-200"));
  requireCondition(ordinaryErrors.length === 0,
    `ordinary-motion browser errors: ${ordinaryErrors.join("; ")}`);
  requireCondition(evidence.length === 6 && evidence.every(({ width, height }) =>
    width > 0 && height > 0), "required Phase 4 screenshot matrix is incomplete");
  console.log(`phase4 screenshot evidence ${JSON.stringify(evidence)}`);
} finally {
  if (browser) await browser.close();
  if (server.exitCode === null) {
    server.kill("SIGTERM");
    await Promise.race([
      new Promise((resolveExit) => server.once("exit", resolveExit)),
      new Promise((_, reject) => setTimeout(() => reject(new Error(`dashboard shutdown timeout\n${output}`)), 5000)),
    ]);
  }
}

console.log(`phase4 browser PASS graph=${resolve(graph)} nodes=${sourceDocument.nodes.length} edges=${sourceDocument.edges.length} lifecycle=${exerciseLifecycle}`);
