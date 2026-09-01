import { afterEach, describe, expect, test, vi } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { adaptGraphDocument } from "./adapter";
import {
  ANIMATED_EDGE_LIMIT,
  COMMAND_HISTORY_LIMIT,
  aggregateMetricText,
  discoverCommands,
  extractTopLevelJsonMember,
  isExactAvailableActivity,
  metricText,
  mayAnimateEdge,
  parseMetricsSnapshot,
  pollOperation,
  prepareGraphExportFromRaw,
  retainOperationHistory,
  submitCommand,
  validateMetricTargetSchemaBounds,
  validateMetricsRawSplitBounds,
  validateCommandArguments,
  type OperationRecord,
} from "./runtime";

const graph = adaptGraphDocument({
  nodes: [
    { id: "same-label-a", type: "RepeatedType" },
    { id: "same-label-b", type: "RepeatedType" },
  ],
  edges: [{
    source_node_id: "same-label-a",
    source_port_name: "out|0",
    target_node_id: "same-label-b",
    target_port: 3,
  }],
});
const rejectionCategories = {
  schema_contract: 0, sample_contract: 0, authority_mismatch: 0,
  subscriber_failure: 0, internal: 0,
};
const repositoryRoot = resolve(process.cwd(), "../..");
const loadFixture = (path: string): Record<string, unknown> =>
  JSON.parse(readFileSync(resolve(repositoryRoot, path), "utf8")) as Record<string, unknown>;

function snapshot(values: unknown[], generation = 7): unknown {
  const normalized: Record<string, unknown>[] = values.map((candidate) => {
    const record = candidate as Record<string, unknown>;
    const decimal = record.scalar_type === "integer" || record.scalar_type === "unsigned";
    return {
      availability_rule: "latest_event",
      ...record,
      scalar_encoding: decimal ? "decimal_string" : "native",
      value: decimal && typeof record.value === "number" ? String(record.value) : record.value,
    };
  });
  return {
    success: true,
    data: {
      schema_version: 1,
      graph_generation: generation,
      active_revision: 4,
      snapshot_sequence: 11,
      snapshot_time: "2026-08-04T12:00:00.000Z",
      availability: { state: "available", reason: "" },
      schemas: normalized.map((candidate) => ({
        target: candidate.target,
        graph_generation: generation,
        metric_id: candidate.metric_id,
        scalar_type: candidate.scalar_type,
        scalar_encoding: candidate.scalar_encoding,
        unit: candidate.unit,
        semantics: candidate.semantics,
        aggregation: candidate.aggregation,
        availability_rule: candidate.availability_rule,
      })),
      values: normalized,
      diagnostics: { rejected: 0, dropped_queue_full: 0,
        rejection_categories: rejectionCategories },
    },
  };
}

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe("Phase 4 runtime contracts", () => {
  test("bounds command history, graph export bytes, and animated edges", () => {
    const operation = (index: number) => ({
      command: "run",
      operation_id: `operation-${index}`,
      status: "completed",
      state: "STOPPED",
      coordinator_revision: index,
      configured_revision: index,
      active_revision: index,
      graph_generation: index,
      configuration_dirty: false,
    });
    let history: OperationRecord[] = [];
    for (let index = 0; index < COMMAND_HISTORY_LIMIT + 20; ++index) {
      history = retainOperationHistory(history, operation(index));
    }
    expect(history).toHaveLength(COMMAND_HISTORY_LIMIT);
    expect(history[0].operation_id).toBe(`operation-${COMMAND_HISTORY_LIMIT + 19}`);
    expect(history.at(-1)?.operation_id).toBe("operation-20");
    history = retainOperationHistory(history, {
      ...history[20], status: "failed", diagnostic: "replacement",
    });
    expect(history).toHaveLength(COMMAND_HISTORY_LIMIT);
    expect(history[0].diagnostic).toBe("replacement");
    expect(new Set(history.map((entry) => entry.operation_id)).size)
      .toBe(COMMAND_HISTORY_LIMIT);

    const prepared = prepareGraphExportFromRaw(
      '{"nodes":[],"edges":[]}', 42, "id:/safe",
    );
    expect(JSON.parse(prepared.encoded)).toMatchObject({
      artifact: "graphx.graph-export",
      version: 1,
      coordinator_revision: 42,
      content_identity: "id:/safe",
    });
    expect(prepared.filename).toBe("graphx-graph-r42-idsafe.json");
    expect(() => prepareGraphExportFromRaw(
      `{"payload":"${"x".repeat(16_777_216)}"}`, 1, "x",
    ))
      .toThrow("16 MiB");

    const animated = Array.from({ length: ANIMATED_EDGE_LIMIT + 10 }, (_, index) =>
      mayAnimateEdge(true, false, index));
    expect(animated.filter(Boolean)).toHaveLength(ANIMATED_EDGE_LIMIT);
    expect(mayAnimateEdge(true, true, 0)).toBe(false);
    expect(mayAnimateEdge(false, false, 0)).toBe(false);
  });

  test("exports the exact authoritative graph token without rounding large integers", () => {
    const rawGraph = '{"nodes":[{"id":"n","type":"T","node_config":{"exact":9007199254740993},"output_ports":[{"index":0}]}],"edges":[],"presentation":{"groups":[{"id":"g","members":["n"]}]}}';
    const response = `{"success":true,"data":${rawGraph},"snapshot":{"coordinator_revision":7,"content_identity":"abc"}}`;
    const extracted = extractTopLevelJsonMember(response, "data");
    expect(extracted).toBe(rawGraph);
    const exported = prepareGraphExportFromRaw(extracted, 7, "abc").encoded;
    expect(exported).toContain('"exact":9007199254740993');
    expect(exported).toContain('"output_ports":[{"index":0}]');
    expect(exported).toContain('"presentation":{"groups":[{"id":"g","members":["n"]}]}');
    expect(JSON.parse(exported)).toMatchObject({
      artifact: "graphx.graph-export", version: 1,
      coordinator_revision: 7, content_identity: "abc",
    });
    expect(() => extractTopLevelJsonMember(
      '{"data":{},"data":{"nodes":[]}}', "data",
    )).toThrow("duplicate data");
  });

  test("measures escaped schema and value tokens before JSON decoding", () => {
    const escaped = "\\u0061";
    const underCount = Math.floor((262_144 - 4) / escaped.length);
    const response = (schemaCount: number, valueCount = 0) =>
      `{"success":true,"data":{"schemas":["${escaped.repeat(schemaCount)}"],` +
      `"values":["${escaped.repeat(valueCount)}"]}}`;
    expect(() => validateMetricsRawSplitBounds(response(underCount))).not.toThrow();
    expect(() => validateMetricsRawSplitBounds(response(underCount + 1)))
      .toThrow("split raw payload bounds");
    const valueUnder = Math.floor((524_288 - 4) / escaped.length);
    expect(() => validateMetricsRawSplitBounds(response(0, valueUnder))).not.toThrow();
    expect(() => validateMetricsRawSplitBounds(response(0, valueUnder + 1)))
      .toThrow("split raw payload bounds");
  });

  test("binds independently authored minimal, nested, complex, SAR, and FHSS metrics only by exact identity", () => {
    const fixtureMetric = (target: Record<string, unknown>, metricId: string,
      overrides: Record<string, unknown> = {}) => ({
      target, graph_generation: 7, metric_id: metricId, scalar_type: "number",
      unit: "items", semantics: "gauge", aggregation: "sum",
      availability: "available", reason: "", value: 1,
      sample_time: "2026-08-04T12:00:00.000Z", ...overrides,
    });
    const cases = [
      { label: "minimal", path: "libgraph/test/config/topologies/minimal_graph.json",
        nodes: 2, edges: 1, nodeId: "source_1",
        target: { kind: "edge", source_node_id: "source_1",
          source_port: { kind: "index", value: 0 }, target_node_id: "sink_1",
          target_port: { kind: "index", value: 0 } },
        edgeIdentity: "edge|8:source_1|index:0|6:sink_1|index:0" },
      { label: "nested", path: "libgraph/test/config/topologies/generic_nested_semantic.json",
        nodes: 4, edges: 3, nodeId: "source",
        target: { kind: "edge", source_node_id: "source",
          source_port: { kind: "name", value: "Data" }, target_node_id: "transform_a",
          target_port: { kind: "index", value: 0 } },
        edgeIdentity: "edge|6:source|name:4:Data|11:transform_a|index:0" },
      { label: "complex", path: "libgraph/test/config/topologies/complex_network.json",
        nodes: 9, edges: 9, nodeId: "source_1",
        target: { kind: "edge", source_node_id: "source_1",
          source_port: { kind: "name", value: "Data" }, target_node_id: "merge_1",
          target_port: { kind: "name", value: "In0" } },
        edgeIdentity: "edge|8:source_1|name:4:Data|7:merge_1|name:3:In0" },
      { label: "SAR", path: "examples/SAR/config/sar_stripmap_fanout.json",
        nodes: 21, edges: 23, nodeId: "src",
        target: { kind: "edge", source_node_id: "src",
          source_port: { kind: "index", value: 0 }, target_node_id: "window",
          target_port: { kind: "index", value: 0 } },
        edgeIdentity: "edge|3:src|index:0|6:window|index:0" },
      { label: "FHSS", path: "libdsp/config/fhss_phase2_binary_iq_receiver.json",
        nodes: 75, edges: 137, nodeId: "assembler",
        target: { kind: "edge", source_node_id: "assembler",
          source_port: { kind: "index", value: 0 }, target_node_id: "sink",
          target_port: { kind: "index", value: 0 } },
        edgeIdentity: "edge|9:assembler|index:0|4:sink|index:0" },
    ] as const;
    const activityText =
      "activity: 2 events/s; available; sampled 2026-08-04T12:00:00.000Z";
    const queueText =
      "queue_depth: 3 items; available; sampled 2026-08-04T12:00:00.000Z";
    for (const oracle of cases) {
      const display = adaptGraphDocument(loadFixture(oracle.path));
      const { label } = oracle;
      expect(display.diagnostics, label).toEqual([]);
      expect(display.nodes, label).toHaveLength(oracle.nodes);
      expect(display.edges, label).toHaveLength(oracle.edges);
      expect(display.nodes.some((node) => node.id === oracle.nodeId), label).toBe(true);
      expect(display.edges.some((edge) => edge.id === oracle.edgeIdentity &&
        edge.sourceNodeId === oracle.target.source_node_id &&
        edge.targetNodeId === oracle.target.target_node_id &&
        JSON.stringify(edge.sourcePort) === JSON.stringify(oracle.target.source_port) &&
        JSON.stringify(edge.targetPort) === JSON.stringify(oracle.target.target_port)), label)
        .toBe(true);
      const parsed = parseMetricsSnapshot(snapshot([
        fixtureMetric(oracle.target, "activity", { value: 2, unit: "events/s" }),
        fixtureMetric({ kind: "node", node_id: oracle.nodeId }, "queue_depth", { value: 3 }),
      ]), display, 7);
      expect(parsed.availability.state, label).toBe("available");
      expect(parsed.graph_generation, label).toBe(7);
      expect(parsed.values.map((value) => value.identity), label)
        .toEqual([`edge:${oracle.edgeIdentity}`, `node:${oracle.nodeId}`]);
      expect(parsed.values.map(metricText), label)
        .toEqual([activityText, queueText]);
      const renderedOverlay = new Map(parsed.values.map((value) =>
        [value.identity, metricText(value)]));
      expect(renderedOverlay.get(`edge:${oracle.edgeIdentity}`), label)
        .toBe(activityText);
      expect(renderedOverlay.get(`node:${oracle.nodeId}`), label).toBe(queueText);
      expect(aggregateMetricText(parsed.values.filter((value) =>
        value.metric_id === "activity")), label)
        .toEqual(["activity: 1/1 members available; sum 2 events/s"]);
      expect(() => parseMetricsSnapshot(snapshot([
        fixtureMetric({ kind: "node", node_id: `${oracle.nodeId}-unknown` }, "queue_depth"),
      ]), display, 7), label).toThrow("not authoritative");
    }
  });
  test("correlates duplicate types only through exact authoritative node IDs", () => {
    const valid = {
      target: { kind: "node", node_id: "same-label-b" },
      graph_generation: 7,
      metric_id: "depth",
      scalar_type: "unsigned",
      unit: "messages",
      semantics: "gauge",
      aggregation: "sum",
      availability: "available",
      reason: "",
      value: 9,
      sample_time: "2026-08-04T12:00:00.000Z",
    };
    const parsed = parseMetricsSnapshot(snapshot([valid]), graph, 7);
    expect(parsed.values).toHaveLength(1);
    expect(parsed.values[0].identity).toBe("node:same-label-b");
    expect(() => parseMetricsSnapshot(snapshot([{
        target: { kind: "node", node_id: "RepeatedType" },
        graph_generation: 7,
        metric_id: "wrong",
        scalar_type: "unsigned",
        unit: "messages",
        semantics: "gauge",
        aggregation: "sum",
        availability: "available",
        reason: "",
        value: 99,
        sample_time: "2026-08-04T12:00:00.000Z",
      }]), graph, 7)).toThrow("not authoritative");
  });

  test("requires the complete numeric/named port tuple and active generation", () => {
    const value = {
      target: {
        kind: "edge",
        source_node_id: "same-label-a",
        source_port: { kind: "name", value: "out|0" },
        target_node_id: "same-label-b",
        target_port: { kind: "index", value: 3 },
      },
      graph_generation: 7,
      metric_id: "activity",
      scalar_type: "number",
      unit: "messages/s",
      semantics: "gauge",
      aggregation: "sum",
      availability: "available",
      reason: "",
      value: 4.5,
      sample_time: "2026-08-04T12:00:00.000Z",
    };
    const parsed = parseMetricsSnapshot(snapshot([value]), graph, 7);
    // Literal encoding is an independent oracle for the production identity
    // helper: node strings are length-prefixed and port kinds are explicit.
    expect(parsed.values[0].identity).toBe(
      "edge:edge|12:same-label-a|name:5:out|0|12:same-label-b|index:3",
    );
    expect(() => parseMetricsSnapshot(snapshot([value], 6), graph, 7))
      .toThrow("stale metric generation");
  });

  test("aggregates only compatible declared numeric values", () => {
    const values = parseMetricsSnapshot(snapshot([1, 2].map((value, index) => ({
      target: { kind: "node", node_id: index === 0 ? "same-label-a" : "same-label-b" },
      graph_generation: 7,
      metric_id: "depth",
      scalar_type: "unsigned",
      unit: "messages",
      semantics: "gauge",
      aggregation: "sum",
      availability: "available",
      reason: "",
      value,
      sample_time: "2026-08-04T12:00:00.000Z",
    }))), graph, 7).values;
    expect(aggregateMetricText(values)).toEqual([
      "depth: 2/2 members available; sum 3 messages",
    ]);
  });

  test("uses discovery and typed submission routes and rejects unsupported fields", async () => {
    const fetchMock = vi.fn(async (input: RequestInfo | URL) => {
      const url = String(input);
      if (url.endsWith("/commands")) {
        return new Response(JSON.stringify({ success: true, data: [
          { name: "configure", asynchronous: false, arguments: {}, description: "configure" },
          { name: "bad", asynchronous: false,
            arguments: { properties: { command_line: { type: "object" } } }, description: "bad" },
        ] }), { status: 200, headers: { "Content-Type": "application/json" } });
      }
      return new Response(JSON.stringify({ success: true, data: {
        command: "configure", operation_id: "op-1", status: "completed",
        state: "CONFIGURED", coordinator_revision: 0, configured_revision: 0,
        active_revision: null, graph_generation: 7, configuration_dirty: false,
      }, message: "done" }), { status: 200, headers: { "Content-Type": "application/json" } });
    });
    vi.stubGlobal("fetch", fetchMock);
    const commands = await discoverCommands();
    expect(commands.map((command) => command.supported)).toEqual([true, false]);
    await submitCommand("configure");
    expect(fetchMock.mock.calls[1][0]).toBe("/api/v1/execution/commands/configure");
  });

  test("accepts the server length-prefixed metric wire order and rejects plain-text order", () => {
    const orderedGraph = adaptGraphDocument({
      nodes: [{ id: "z", type: "Node" }, { id: "aa", type: "Node" }],
      edges: [
        { source_node_id: "z", source_port_name: "q",
          target_node_id: "aa", target_port: 2 },
        { source_node_id: "aa", source_port: 10,
          target_node_id: "z", target_port_name: "rr" },
      ],
    });
    const metric = (target: Record<string, unknown>, metricId: string) => ({
      target,
      graph_generation: 7,
      metric_id: metricId,
      scalar_type: "number",
      unit: "events/s",
      semantics: "gauge",
      aggregation: "sum",
      availability: "available",
      reason: "",
      value: 1,
      sample_time: "2026-08-04T12:00:00.000Z",
    });
    // This expected order is an independent fixture oracle for the C++ wire
    // grammar: kind, then UTF-8 byte-length-prefixed tuple parts and metric ID.
    const serverOrderedNodes = [
      metric({ kind: "node", node_id: "z" }, "z"),
      metric({ kind: "node", node_id: "z" }, "aa"),
      metric({ kind: "node", node_id: "aa" }, "z"),
    ];
    expect(parseMetricsSnapshot(snapshot(serverOrderedNodes), orderedGraph, 7).values)
      .toHaveLength(3);
    expect(() => parseMetricsSnapshot(
      snapshot([...serverOrderedNodes].reverse()), orderedGraph, 7,
    )).toThrow("deterministically ordered");
    const duplicateValue = snapshot([
      serverOrderedNodes[0], serverOrderedNodes[0],
    ]) as { data: { schemas: unknown[] } };
    duplicateValue.data.schemas.pop();
    expect(() => parseMetricsSnapshot(duplicateValue, orderedGraph, 7))
      .toThrow("duplicate metric value identity");
    const missingValue = snapshot(serverOrderedNodes) as {
      data: { values: unknown[] };
    };
    missingValue.data.values.pop();
    expect(() => parseMetricsSnapshot(missingValue, orderedGraph, 7))
      .toThrow("schemas and values are not one-to-one");

    const serverOrderedEdges = [
      metric({ kind: "edge", source_node_id: "z",
        source_port: { kind: "name", value: "q" }, target_node_id: "aa",
        target_port: { kind: "index", value: 2 } }, "z"),
      metric({ kind: "edge", source_node_id: "aa",
        source_port: { kind: "index", value: 10 }, target_node_id: "z",
        target_port: { kind: "name", value: "rr" } }, "aa"),
    ];
    expect(parseMetricsSnapshot(snapshot(serverOrderedEdges), orderedGraph, 7).values)
      .toHaveLength(2);
    expect(() => parseMetricsSnapshot(
      snapshot([...serverOrderedEdges].reverse()), orderedGraph, 7,
    )).toThrow("deterministically ordered");

    const bmpId = "\uE000a";
    const astralId = "\u{10000}";
    const unicodeGraph = adaptGraphDocument({
      nodes: [{ id: bmpId, type: "Node" }, { id: astralId, type: "Node" },
        { id: "t", type: "Node" }],
      edges: [
        { source_node_id: bmpId, source_port_name: "p",
          target_node_id: "t", target_port: 0 },
        { source_node_id: astralId, source_port_name: "p",
          target_node_id: "t", target_port: 0 },
      ],
    });
    const unicodeNodes = [
      metric({ kind: "node", node_id: bmpId }, bmpId),
      metric({ kind: "node", node_id: astralId }, astralId),
    ];
    expect(parseMetricsSnapshot(snapshot(unicodeNodes), unicodeGraph, 7).values)
      .toHaveLength(2);
    expect(() => parseMetricsSnapshot(
      snapshot([...unicodeNodes].reverse()), unicodeGraph, 7,
    )).toThrow("deterministically ordered");
    const unicodeEdges = [
      metric({ kind: "edge", source_node_id: bmpId,
        source_port: { kind: "name", value: "p" }, target_node_id: "t",
        target_port: { kind: "index", value: 0 } }, "z"),
      metric({ kind: "edge", source_node_id: astralId,
        source_port: { kind: "name", value: "p" }, target_node_id: "t",
        target_port: { kind: "index", value: 0 } }, "z"),
    ];
    expect(parseMetricsSnapshot(snapshot(unicodeEdges), unicodeGraph, 7).values)
      .toHaveLength(2);
    expect(() => parseMetricsSnapshot(
      snapshot([...unicodeEdges].reverse()), unicodeGraph, 7,
    )).toThrow("deterministically ordered");

    const overByteNodeId = "😀".repeat(65);
    const overBytePort = "😀".repeat(33);
    const overByteGraph = adaptGraphDocument({
      nodes: [{ id: overByteNodeId, type: "Node" }, { id: "t", type: "Node" }],
      edges: [{ source_node_id: "t", source_port_name: overBytePort,
        target_node_id: overByteNodeId, target_port: 0 }],
    });
    expect(() => parseMetricsSnapshot(snapshot([
      metric({ kind: "node", node_id: overByteNodeId }, "z"),
    ]), overByteGraph, 7)).toThrow("not authoritative");
    expect(() => parseMetricsSnapshot(snapshot([
      metric({ kind: "node", node_id: "z" }, "😀".repeat(33)),
    ]), orderedGraph, 7)).toThrow("descriptor contract");
    expect(() => parseMetricsSnapshot(snapshot([
      metric({ kind: "edge", source_node_id: "t",
        source_port: { kind: "name", value: overBytePort },
        target_node_id: overByteNodeId,
        target_port: { kind: "index", value: 0 } }, "z"),
    ]), overByteGraph, 7)).toThrow("tuple is invalid");
  });

  test("rejects malformed schema, typed values, diagnostics, future time, and old sequence", () => {
    const base = {
      target: { kind: "node", node_id: "same-label-a" },
      graph_generation: 7,
      metric_id: "depth",
      scalar_type: "unsigned",
      unit: "messages",
      semantics: "gauge",
      aggregation: "sum",
      availability: "available",
      reason: "",
      value: 4,
      sample_time: "2026-08-04T12:00:00.000Z",
    };
    const wrongType = snapshot([{ ...base, value: -1 }]);
    expect(() => parseMetricsSnapshot(wrongType, graph, 7)).toThrow("typed value");
    const wrongUnit = snapshot([base]) as { data: { values: Array<Record<string, unknown>> } };
    wrongUnit.data.values[0].unit = "bytes";
    expect(() => parseMetricsSnapshot(wrongUnit, graph, 7)).toThrow("does not match");
    const badDiagnostics = snapshot([base]) as { data: { diagnostics: Record<string, unknown> } };
    badDiagnostics.data.diagnostics.rejected = -1;
    expect(() => parseMetricsSnapshot(badDiagnostics, graph, 7)).toThrow("diagnostics");
    const future = snapshot([{ ...base, sample_time: "2999-01-01T00:00:00.000Z" }]);
    expect(() => parseMetricsSnapshot(future, graph, 7)).toThrow("typed value");
    expect(() => parseMetricsSnapshot(snapshot([base]), graph, 7,
      { generation: 7, sequence: 12 })).toThrow("out-of-order");
    const extraSchema = snapshot([base]) as {
      data: { schemas: Array<Record<string, unknown>> };
    };
    extraSchema.data.schemas[0].unexpected = true;
    expect(() => parseMetricsSnapshot(extraSchema, graph, 7)).toThrow("undeclared fields");
    const extraValue = snapshot([base]) as {
      data: { values: Array<Record<string, unknown>> };
    };
    extraValue.data.values[0].unexpected = true;
    expect(() => parseMetricsSnapshot(extraValue, graph, 7)).toThrow("undeclared fields");
    const extraTarget = snapshot([base]) as {
      data: { values: Array<{ target: Record<string, unknown> }> };
    };
    extraTarget.data.values[0].target.unexpected = true;
    expect(() => parseMetricsSnapshot(extraTarget, graph, 7)).toThrow("undeclared fields");
    const emptyGlobalReason = snapshot([base]) as {
      data: { availability: Record<string, unknown>; values: Array<Record<string, unknown>> };
    };
    emptyGlobalReason.data.availability = { state: "stale", reason: "" };
    emptyGlobalReason.data.values[0] = {
      ...emptyGlobalReason.data.values[0], availability: "unavailable", reason: "stale",
      value: null, sample_time: null,
    };
    expect(() => parseMetricsSnapshot(emptyGlobalReason, graph, 7)).toThrow("availability");
  });

  test("accepts only calendar-valid canonical millisecond UTC metric timestamps", () => {
    const base = {
      target: { kind: "node", node_id: "same-label-a" },
      graph_generation: 7, metric_id: "depth", scalar_type: "unsigned",
      unit: "messages", semantics: "gauge", aggregation: "sum",
      availability: "available", reason: "", value: 4,
      sample_time: "2024-02-29T23:59:59.999Z",
    };
    expect(() => parseMetricsSnapshot(snapshot([base]), graph, 7)).not.toThrow();

    const invalidTimes = [
      "2026-08-04",
      "2026-08-04 12:00:00.000Z",
      "2026-08-04T12:00:00.000+00:00",
      "2026-02-29T12:00:00.000Z",
      "2026-08-04T24:00:00.000Z",
      "2026-08-04T12:00:00Z",
      "2026-08-04T12:00:00.0Z",
      "2026-08-04T12:00:00.0000Z",
      "2026-08-04T12:00:00.000Zjunk",
    ];
    for (const malformed of invalidTimes) {
      const badSample = snapshot([{ ...base, sample_time: malformed }]);
      expect(() => parseMetricsSnapshot(badSample, graph, 7), malformed)
        .toThrow("typed value");
      const badSnapshot = snapshot([base]) as { data: { snapshot_time: string } };
      badSnapshot.data.snapshot_time = malformed;
      expect(() => parseMetricsSnapshot(badSnapshot, graph, 7), malformed)
        .toThrow("metadata");
    }
  });

  test("rejects available numeric values in stale envelopes", () => {
    const document = snapshot([{
      target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
      metric_id: "depth", scalar_type: "unsigned", unit: "messages",
      semantics: "gauge", aggregation: "sum", availability: "available",
      reason: "", value: 1, sample_time: "2026-08-04T12:00:00.000Z",
    }]) as { data: { availability: Record<string, unknown> } };
    document.data.availability = { state: "stale", reason: "sample_timeout" };
    expect(() => parseMetricsSnapshot(document, graph, 7)).toThrow("stale or unavailable");
  });

  test("accepts publisher-unavailable sample time and renders no number", () => {
    const parsed = parseMetricsSnapshot(snapshot([{
      target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
      metric_id: "depth", scalar_type: "unsigned", unit: "messages",
      semantics: "gauge", aggregation: "sum", availability: "unavailable",
      reason: "publisher_offline", value: null,
      sample_time: "2026-08-04T12:00:00.000Z",
    }]), graph, 7);
    expect(parsed.values[0].sample_time).toBe("2026-08-04T12:00:00.000Z");
    expect(metricText(parsed.values[0])).toBe("depth: unavailable (publisher_offline)");
  });

  test("preserves exact signed and unsigned 64-bit decimal strings", () => {
    for (const [scalar_type, values] of [
      ["integer", ["-9223372036854775808", "9223372036854775807"]],
      ["unsigned", ["18446744073709551615"]],
    ] as const) {
      for (const value of values) {
        const parsed = parseMetricsSnapshot(snapshot([{
          target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
          metric_id: "exact", scalar_type, unit: "items", semantics: "gauge",
          aggregation: "sum", availability: "available", reason: "", value,
          sample_time: "2026-08-04T12:00:00.000Z",
        }]), graph, 7);
        expect(parsed.values[0].value).toBe(value);
      }
    }
  });

  test("preserves an independently specified full-width counter epoch and rate", () => {
    const parsed = parseMetricsSnapshot(snapshot([{
      target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
      metric_id: "completed", scalar_type: "unsigned", unit: "items",
      semantics: "monotonic_counter", aggregation: "rate",
      availability: "available", reason: "", value: "18446744073709551615",
      sample_time: "2026-08-04T12:00:00.000Z",
      counter_epoch_encoding: "decimal_string",
      counter_epoch: "18446744073709551615", rate: 12.5, rate_reason: "",
    }]), graph, 7);
    expect(parsed.values[0]).toMatchObject({
      value: "18446744073709551615",
      counter_epoch: "18446744073709551615",
      counter_epoch_encoding: "decimal_string",
      rate: 12.5,
      rate_reason: "",
    });
  });

  test("returns one explicit incompatible or missing-member aggregate per metric scope", () => {
    const values = parseMetricsSnapshot(snapshot([
      { target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
        metric_id: "depth", scalar_type: "unsigned", unit: "messages",
        semantics: "gauge", aggregation: "sum", availability: "available",
        reason: "", value: 1, sample_time: "2026-08-04T12:00:00.000Z" },
      { target: { kind: "node", node_id: "same-label-b" }, graph_generation: 7,
        metric_id: "depth", scalar_type: "unsigned", unit: "bytes",
        semantics: "gauge", aggregation: "sum", availability: "available",
        reason: "", value: 2, sample_time: "2026-08-04T12:00:00.000Z" },
    ]), graph, 7).values;
    expect(aggregateMetricText(values)).toEqual([
      "depth: 2/2 members available; unavailable (incompatible member metrics)",
    ]);
    expect(aggregateMetricText(values.slice(0, 1), 2)).toEqual([
      "depth: 1/2 members available; unavailable (members have no matching metric)",
    ]);
  });

  test("animates only an exact available positive edge activity gauge", () => {
    const activity = parseMetricsSnapshot(snapshot([{
      target: { kind: "edge", source_node_id: "same-label-a",
        source_port: { kind: "name", value: "out|0" }, target_node_id: "same-label-b",
        target_port: { kind: "index", value: 3 } }, graph_generation: 7,
      metric_id: "activity", scalar_type: "number", unit: "events/s",
      semantics: "gauge", aggregation: "sum", availability: "available",
      reason: "", value: 1, sample_time: "2026-08-04T12:00:00.000Z",
    }]), graph, 7).values[0];
    expect(isExactAvailableActivity(activity)).toBe(true);
    expect(isExactAvailableActivity({ ...activity, value: 0 })).toBe(false);
    expect(isExactAvailableActivity({ ...activity, availability: "unavailable" })).toBe(false);
    expect(isExactAvailableActivity({ ...activity, metric_id: "depth" })).toBe(false);
  });

  test("validates unsigned/numeric/string argument contracts and forbidden fields", async () => {
    const descriptor = {
      name: "bounded", asynchronous: false, description: "",
      arguments: { properties: {
        count: { type: "unsigned", minimum: 0, maximum: 9 },
        mode: { type: "string", enum: ["a", "b"] },
      }, required: ["count"] }, supported: true,
    };
    expect(validateCommandArguments(descriptor, { count: 2, mode: "a" })).toBeNull();
    expect(validateCommandArguments(descriptor, { count: -1 })).toContain("minimum");
    expect(validateCommandArguments(descriptor, { count: Number.NaN })).toContain("integer");
    expect(validateCommandArguments(descriptor, {})).toContain("required");
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ success: true, data: [{
      name: "bad", asynchronous: false, description: "",
      arguments: { properties: { executable_path: { type: "string", maxLength: 8 } } },
    }] }), { status: 200 })));
    expect((await discoverCommands())[0].supported).toBe(false);
  });

  test("admits only the bounded command-schema subset and preserves empty enum values", async () => {
    const candidates = [
      { name: "empty-value", arguments: { properties: {
        mode: { type: "string", enum: ["", "active"] },
      }, required: ["mode"], additionalProperties: false } },
      { name: "unknown-root", arguments: { properties: {}, patternProperties: {} } },
      { name: "unknown-field", arguments: { properties: {
        mode: { type: "string", maxLength: 8, pattern: "x" },
      } } },
      { name: "undeclared-required", arguments: { properties: {}, required: ["missing"] } },
      { name: "empty-enum", arguments: { properties: {
        mode: { type: "string", enum: [] },
      } } },
      { name: "duplicate-enum", arguments: { properties: {
        mode: { type: "string", enum: ["same", "same"] },
      } } },
      { name: "enum-too-long", arguments: { properties: {
        mode: { type: "string", enum: ["long"], maxLength: 3 },
      } } },
      { name: "unsafe-integer", arguments: { properties: {
        count: { type: "integer", minimum: 0, maximum: Number.MAX_SAFE_INTEGER + 1 },
      } } },
      { name: "open-object", arguments: { properties: {}, additionalProperties: true } },
    ].map((candidate) => ({ asynchronous: false, description: "", ...candidate }));
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({
      success: true, data: candidates,
    }), { status: 200 })));
    const discovered = await discoverCommands();
    expect(discovered.map(({ name, supported }) => [name, supported])).toEqual([
      ["empty-value", true],
      ["unknown-root", false],
      ["unknown-field", false],
      ["undeclared-required", false],
      ["empty-enum", false],
      ["duplicate-enum", false],
      ["enum-too-long", false],
      ["unsafe-integer", false],
      ["open-object", false],
    ]);
    expect(validateCommandArguments(discovered[0], { mode: "" })).toBeNull();
  });

  test("marks malformed command descriptor metadata unsupported", async () => {
    const candidates = [
      { name: "missing-arguments", asynchronous: false, description: "" },
      { name: "null-arguments", asynchronous: false, description: "", arguments: null },
      { name: "array-arguments", asynchronous: false, description: "", arguments: [] },
      { name: "bad-async", asynchronous: "false", description: "", arguments: {} },
      { name: "bad-description", asynchronous: false, description: 3, arguments: {} },
      { name: "extra-key", asynchronous: false, description: "", arguments: {}, extra: true },
    ];
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({
      success: true, data: candidates,
    }), { status: 200 })));
    const discovered = await discoverCommands();
    expect(discovered).toHaveLength(candidates.length);
    expect(discovered.every((descriptor) => !descriptor.supported)).toBe(true);
    expect(discovered.every((descriptor) =>
      descriptor.unsupportedReason === "command descriptor metadata is invalid")).toBe(true);
  });

  test("enforces exact target-schema and per-target metric bounds", () => {
    const boundedGraph = adaptGraphDocument({
      nodes: Array.from({ length: 2_049 }, (_, index) => ({
        id: `node-${String(index).padStart(4, "0")}`, type: "Node",
      })),
      edges: [],
    });
    const metric = (nodeIndex: number, metricIndex = 0) => ({
      target: { kind: "node", node_id: `node-${String(nodeIndex).padStart(4, "0")}` },
      graph_generation: 7,
      metric_id: `metric-${String(metricIndex).padStart(3, "0")}`,
      scalar_type: "number", unit: "items", semantics: "gauge", aggregation: "sum",
      availability: "available", reason: "", value: 1,
      sample_time: "2026-08-04T12:00:00.000Z",
    });
    const targetSchemas = Array.from({ length: 2_049 }, (_, index) => ({
      target: { ...metric(index).target, kind: "node" as const },
    }));
    expect(() => validateMetricTargetSchemaBounds(
      targetSchemas.slice(0, 2_048), boundedGraph,
    )).not.toThrow();
    expect(() => validateMetricTargetSchemaBounds(targetSchemas, boundedGraph))
      .toThrow("target schema bounds");
    expect(parseMetricsSnapshot(
      snapshot(Array.from({ length: 80 }, (_, index) => metric(0, index))),
      boundedGraph, 7,
    ).schemas).toHaveLength(80);
  });

  test("enforces independent encoded schema and value payload bounds", () => {
    const base = {
      target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
      metric_id: "depth", scalar_type: "number", unit: "items", semantics: "gauge",
      aggregation: "sum", availability: "available", reason: "", value: 1,
      sample_time: "2026-08-04T12:00:00.000Z",
    };
    const schemaHeavy = snapshot([base]) as {
      data: { schemas: Array<Record<string, unknown>> };
    };
    schemaHeavy.data.schemas = Array.from({ length: 1_100 }, (_, index) => ({
      ...schemaHeavy.data.schemas[0], metric_id: `schema-${index}`,
      ignored_padding: "x".repeat(256),
    }));
    expect(() => parseMetricsSnapshot(schemaHeavy, graph, 7))
      .toThrow("split payload bounds");
    const valueHeavy = snapshot([base]) as {
      data: { values: Array<Record<string, unknown>> };
    };
    valueHeavy.data.values = Array.from({ length: 1_600 }, (_, index) => ({
      ...valueHeavy.data.values[0], metric_id: `value-${index}`,
      ignored_padding: "x".repeat(256),
    }));
    expect(() => parseMetricsSnapshot(valueHeavy, graph, 7))
      .toThrow("split payload bounds");
  });

  test("rejects contradictory counter-rate state and accepts reset invalidation", () => {
    const counter = {
      target: { kind: "node", node_id: "same-label-a" }, graph_generation: 7,
      metric_id: "completed", scalar_type: "unsigned", unit: "items",
      semantics: "monotonic_counter", aggregation: "rate",
      availability: "available", reason: "", value: "8",
      sample_time: "2026-08-04T12:00:00.000Z",
      counter_epoch_encoding: "decimal_string", counter_epoch: "2",
    };
    expect(() => parseMetricsSnapshot(snapshot([{
      ...counter, rate: 1, rate_reason: "counter_epoch_changed",
    }]), graph, 7)).toThrow("counter metric state");
    expect(() => parseMetricsSnapshot(snapshot([{
      ...counter, rate: -1, rate_reason: "",
    }]), graph, 7)).toThrow("counter metric state");
    expect(() => parseMetricsSnapshot(snapshot([{
      ...counter, rate: null, rate_reason: "",
    }]), graph, 7)).toThrow("counter metric state");
    const reset = parseMetricsSnapshot(snapshot([{
      ...counter, rate: null, rate_reason: "counter_epoch_changed",
    }]), graph, 7).values[0];
    expect(reset).toMatchObject({ rate: null, rate_reason: "counter_epoch_changed" });
    const recovered = parseMetricsSnapshot(snapshot([{
      ...counter, rate: 3.5, rate_reason: "",
    }]), graph, 7).values[0];
    expect(recovered).toMatchObject({ rate: 3.5, rate_reason: "" });
    expect(() => parseMetricsSnapshot(snapshot([{
      ...counter, aggregation: "sum", rate: 3.5, rate_reason: "",
    }]), graph, 7)).toThrow("counter metric state");
  });

  test("reports explicit aggregation reasons for unsupported scalar and rate cases", () => {
    const metric = (nodeId: string, overrides: Record<string, unknown>) => ({
      target: { kind: "node", node_id: nodeId }, graph_generation: 7,
      metric_id: "state", scalar_type: "boolean", unit: "", semantics: "gauge",
      aggregation: "sum", availability: "available", reason: "", value: true,
      sample_time: "2026-08-04T12:00:00.000Z", ...overrides,
    });
    const booleanValues = parseMetricsSnapshot(snapshot([
      metric("same-label-a", {}), metric("same-label-b", {}),
    ]), graph, 7).values;
    expect(aggregateMetricText(booleanValues)[0]).toContain(
      "unavailable (sum aggregation does not support boolean values)",
    );
    const noAggregation = parseMetricsSnapshot(snapshot([
      metric("same-label-a", { aggregation: "none" }),
      metric("same-label-b", { aggregation: "none" }),
    ]), graph, 7).values;
    expect(aggregateMetricText(noAggregation)).toEqual([
      "state: 2/2 members available; unavailable (aggregation is declared none)",
    ]);
    const stringValues = parseMetricsSnapshot(snapshot([
      metric("same-label-a", { scalar_type: "string", aggregation: "average", value: "a" }),
      metric("same-label-b", { scalar_type: "string", aggregation: "average", value: "b" }),
    ]), graph, 7).values;
    expect(aggregateMetricText(stringValues)[0]).toContain(
      "unavailable (average aggregation does not support string values)",
    );
    const gaugeRate = parseMetricsSnapshot(snapshot([
      metric("same-label-a", { scalar_type: "number", aggregation: "rate", value: 1 }),
      metric("same-label-b", { scalar_type: "number", aggregation: "rate", value: 2 }),
    ]), graph, 7).values;
    expect(aggregateMetricText(gaugeRate)[0]).toContain(
      "unavailable (rate aggregation requires monotonic counters)",
    );
    const overflow = parseMetricsSnapshot(snapshot([
      metric("same-label-a", { scalar_type: "number", aggregation: "sum",
        value: Number.MAX_VALUE }),
      metric("same-label-b", { scalar_type: "number", aggregation: "sum",
        value: Number.MAX_VALUE }),
    ]), graph, 7).values;
    expect(aggregateMetricText(overflow)[0]).toContain(
      "unavailable (aggregate result is not finite)",
    );
    const missingRate = parseMetricsSnapshot(snapshot([
      metric("same-label-a", { scalar_type: "unsigned", semantics: "monotonic_counter",
        aggregation: "rate", value: "1", counter_epoch_encoding: "decimal_string",
        counter_epoch: "3", rate: null, rate_reason: "not_enough_samples" }),
      metric("same-label-b", { scalar_type: "unsigned", semantics: "monotonic_counter",
        aggregation: "rate", value: "2", counter_epoch_encoding: "decimal_string",
        counter_epoch: "3", rate: 2, rate_reason: "" }),
    ]), graph, 7).values;
    expect(aggregateMetricText(missingRate)[0]).toContain(
      "unavailable (one or more counter rates are unavailable)",
    );
  });

  test("rejects camel-case execution fields without rejecting benign near-misses", async () => {
    const names = ["commandLine", "shellCommand", "executablePath",
      "environmentVariable", "filesystemPath", "targetUrl"];
    for (const name of names) {
      vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ success: true, data: [{
        name: "bad", asynchronous: false, description: "",
        arguments: { properties: { [name]: { type: "string", maxLength: 8 } } },
      }] }), { status: 200 })));
      expect((await discoverCommands())[0].supported).toBe(false);
    }
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ success: true, data: [{
      name: "good", asynchronous: false, description: "",
      arguments: { properties: {
        commander: { type: "string", maxLength: 8 },
        pathway: { type: "string", maxLength: 8 },
      } },
    }] }), { status: 200 })));
    expect((await discoverCommands())[0].supported).toBe(true);
  });

  test("accepts the real asynchronous status and polls sequentially to terminal", async () => {
    vi.useFakeTimers();
    const responses = [
      { status: "running" },
      { status: "completed" },
    ];
    const fetchMock = vi.fn(async () => {
      const next = responses.shift()!;
      return new Response(JSON.stringify({ success: true, data: {
        command: "run", operation_id: "op-1", status: next.status,
        state: "RUNNING", coordinator_revision: 1, configured_revision: 1,
        active_revision: 1, graph_generation: 7, configuration_dirty: false,
      } }), { status: 200 });
    });
    vi.stubGlobal("fetch", fetchMock);
    const controller = new AbortController();
    const completion = pollOperation("/api/v1/execution/operations/op-1", controller.signal);
    await vi.advanceTimersByTimeAsync(500);
    await vi.advanceTimersByTimeAsync(500);
    await expect(completion).resolves.toMatchObject({ status: "completed" });
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  test("rejects uncorrelated command acceptance and operation polling", async () => {
    const accepted = {
      command: "run", operation_id: "op-expected", status: "accepted",
      state: "RUNNING", coordinator_revision: 1, configured_revision: 1,
      active_revision: 1, graph_generation: 7, configuration_dirty: false,
    };
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({
      success: true, data: accepted,
    }), { status: 202, headers: { "Content-Type": "application/json" } })));
    await expect(submitCommand("run")).rejects.toThrow("Location is inconsistent");

    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({
      success: true, data: { ...accepted, command: "stop" },
    }), { status: 202, headers: { "Content-Type": "application/json",
      Location: "/api/v1/execution/operations/op-expected" } })));
    await expect(submitCommand("run")).rejects.toThrow("Location is inconsistent");

    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({
      success: true, data: accepted,
    }), { status: 202, headers: { "Content-Type": "application/json",
      Location: "/api/v1/execution/operations/op-other" } })));
    await expect(submitCommand("run")).rejects.toThrow("Location is inconsistent");

    vi.useFakeTimers();
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ success: true, data: {
      ...accepted, operation_id: "op-other", status: "completed",
    } }), { status: 200 })));
    const controller = new AbortController();
    const polling = pollOperation(
      "/api/v1/execution/operations/op-expected", controller.signal, "run",
    );
    const rejection = expect(polling).rejects.toThrow("not correlated");
    await vi.advanceTimersByTimeAsync(500);
    await rejection;
  });

  test("balances operation-poll abort listeners across long runs and cancellation", async () => {
    vi.useFakeTimers();
    let polls = 0;
    vi.stubGlobal("fetch", vi.fn(async () => {
      polls += 1;
      return new Response(JSON.stringify({ success: true, data: {
        command: "run", operation_id: "op-long",
        status: polls === 40 ? "completed" : "running",
        state: "RUNNING", coordinator_revision: 1, configured_revision: 1,
        active_revision: 1, graph_generation: 7, configuration_dirty: false,
      } }), { status: 200, headers: { "Content-Type": "application/json" } });
    }));
    const controller = new AbortController();
    const add = vi.spyOn(controller.signal, "addEventListener");
    const remove = vi.spyOn(controller.signal, "removeEventListener");
    const completion = pollOperation(
      "/api/v1/execution/operations/op-long", controller.signal,
    );
    for (let index = 0; index < 40; ++index) {
      await vi.advanceTimersByTimeAsync(500);
    }
    await expect(completion).resolves.toMatchObject({ status: "completed" });
    expect(add).toHaveBeenCalledTimes(40);
    expect(remove).toHaveBeenCalledTimes(40);

    const cancelledController = new AbortController();
    const cancelledAdd = vi.spyOn(cancelledController.signal, "addEventListener");
    const cancelledRemove = vi.spyOn(cancelledController.signal, "removeEventListener");
    const cancelled = pollOperation(
      "/api/v1/execution/operations/op-cancel", cancelledController.signal,
    );
    const cancellation = expect(cancelled).rejects.toMatchObject({ name: "AbortError" });
    cancelledController.abort();
    await cancellation;
    expect(cancelledAdd).toHaveBeenCalledTimes(1);
    expect(cancelledRemove).toHaveBeenCalledTimes(1);
  });
});
