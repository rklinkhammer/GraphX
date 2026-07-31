import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, test } from "vitest";

import {
  adaptGraphDocument,
  edgeIdentity,
  portHandleIdentity,
} from "./adapter";
import cyclic from "./test/fixtures/cyclic.json";
import disconnected from "./test/fixtures/disconnected.json";
import empty from "./test/fixtures/empty.json";
import invalidGraph from "./test/fixtures/invalid_graph.json";
import malformed from "./test/fixtures/malformed.json";
import malformedBranches from "./test/fixtures/malformed_branches.json";
import namedPorts from "./test/fixtures/named_ports.json";
import numericPorts from "./test/fixtures/numeric_ports.json";
import splitMerge from "./test/fixtures/split_merge.json";
import type { PortKey } from "./types";

const sourceRoot = resolve(process.cwd(), "../..");

function loadRepositoryGraph(relativePath: string): Record<string, unknown> {
  return JSON.parse(
    readFileSync(`${sourceRoot}/${relativePath}`, "utf8"),
  ) as Record<string, unknown>;
}

function endpointTuple(edge: {
  sourceNodeId: string;
  sourcePort: PortKey;
  targetNodeId: string;
  targetPort: PortKey;
}): unknown[] {
  return [
    edge.sourceNodeId,
    edge.sourcePort.kind,
    edge.sourcePort.value,
    edge.targetNodeId,
    edge.targetPort.kind,
    edge.targetPort.value,
  ];
}

describe("generic graph adapter", () => {
  test("preserves an empty graph without inventing structure", () => {
    const model = adaptGraphDocument(empty);
    expect(model.nodes).toEqual([]);
    expect(model.edges).toEqual([]);
    expect(model.diagnostics).toEqual([]);
  });

  test("keeps numeric zero distinct from the named string zero", () => {
    const numeric = adaptGraphDocument(numericPorts);
    const named = adaptGraphDocument(namedPorts);
    expect(numeric.edges[0].sourcePort).toEqual({ kind: "index", value: 0 });
    expect(named.edges[0].sourcePort).toEqual({ kind: "name", value: "0" });
    expect(numeric.edges[0].sourceHandleId).not.toBe(
      named.edges[0].sourceHandleId,
    );
    expect(portHandleIdentity("output", { kind: "index", value: 0 })).not.toBe(
      portHandleIdentity("output", { kind: "name", value: "0" }),
    );
  });

  test("uses the full endpoint tuple for collision-free edge identity", () => {
    const source = { kind: "name", value: "a|b" } as const;
    const target = { kind: "name", value: "c" } as const;
    expect(edgeIdentity("x", source, "y", target)).not.toBe(
      edgeIdentity("x|a", { kind: "name", value: "b" }, "y", target),
    );
    expect(edgeIdentity("x", source, "y", target)).not.toBe(
      edgeIdentity("x", { kind: "index", value: 0 }, "y", target),
    );
  });

  test("reports malformed identities and ports instead of repairing them", () => {
    const model = adaptGraphDocument(malformed);
    expect(new Set(model.diagnostics.map((entry) => entry.code))).toEqual(
      new Set([
        "duplicate_node_identity",
        "invalid_node",
        "invalid_port",
      ]),
    );
    expect(model.rawNodes).toHaveLength(4);
    expect(model.rawEdges).toHaveLength(2);
  });

  test("reports invalid graph containers without inventing nodes or edges", () => {
    const nonObject = adaptGraphDocument("not a graph");
    expect(nonObject.nodes).toEqual([]);
    expect(nonObject.edges).toEqual([]);
    expect(nonObject.diagnostics).toEqual([
      expect.objectContaining({ code: "invalid_graph", entity: "graph" }),
    ]);

    const invalidCollections = adaptGraphDocument(invalidGraph);
    expect(invalidCollections.nodes).toEqual([]);
    expect(invalidCollections.edges).toEqual([]);
    expect(invalidCollections.diagnostics).toEqual([
      expect.objectContaining({ code: "invalid_graph", entity: "nodes" }),
      expect.objectContaining({ code: "invalid_graph", entity: "edges" }),
    ]);
  });

  test("covers every malformed edge, endpoint, node, and port branch without repair", () => {
    const model = adaptGraphDocument(malformedBranches);
    expect(model.rawNodes).toHaveLength(5);
    expect(model.rawEdges).toHaveLength(9);
    expect(model.nodes.map((node) => node.id)).toEqual(["a", "b"]);
    expect(model.edges).toHaveLength(1);
    expect(endpointTuple(model.edges[0])).toEqual([
      "a",
      "index",
      0,
      "b",
      "index",
      2,
    ]);
    expect(model.nodes[0].outputPorts.map((port) => port.key)).toEqual([
      { kind: "index", value: 0 },
    ]);
    expect(model.nodes[1].inputPorts.map((port) => port.key)).toEqual([
      { kind: "index", value: 2 },
    ]);

    const diagnostics = model.diagnostics.map(
      ({ code, entity, detail }) => `${code}|${entity}|${detail}`,
    );
    expect(diagnostics).toEqual(
      expect.arrayContaining([
        expect.stringMatching(/^invalid_node\|nodes\[2\]\|node must be an object/),
        expect.stringMatching(/^invalid_node\|nodes\[3\]\|node id/),
        expect.stringMatching(/^invalid_node\|nodes\[4\]\|node .* type/),
        expect.stringMatching(/^invalid_edge\|edges\[0\]\|edge must be an object/),
        expect.stringMatching(/^invalid_edge\|edges\[1\]\|edge node endpoints/),
        expect.stringMatching(/^missing_endpoint\|edges\[2\]\|unknown target "ghost"/),
        expect.stringMatching(/^duplicate_edge_identity\|edges\[4\]\|/),
        expect.stringMatching(/^invalid_port\|edges\[5\]\|source endpoint supplies both/),
        expect.stringMatching(/^invalid_port\|edges\[6\]\|source endpoint does not supply/),
        expect.stringMatching(/^invalid_port\|edges\[7\]\|source_port must be/),
        expect.stringMatching(/^invalid_port\|edges\[8\]\|target_port_name must be/),
      ]),
    );
  });

  test("displays disconnected and cyclic input", () => {
    const islands = adaptGraphDocument(disconnected);
    expect(islands.nodes.map((node) => node.id)).toEqual([
      "island-a",
      "island-b",
      "island-c",
    ]);
    expect(islands.edges).toHaveLength(0);
    expect(islands.diagnostics).toEqual([]);

    const cycle = adaptGraphDocument(cyclic);
    expect(cycle.nodes).toHaveLength(2);
    expect(cycle.edges).toHaveLength(2);
    expect(cycle.diagnostics).toEqual([]);
  });

  test("preserves combined fanout and fanin and converts deterministically", () => {
    const first = adaptGraphDocument(splitMerge);
    const second = adaptGraphDocument(structuredClone(splitMerge));
    expect(first.nodes).toHaveLength(6);
    expect(first.edges).toHaveLength(6);
    expect(first).toEqual(second);
    expect(first.nodes.find((node) => node.id === "split")?.outputPorts).toHaveLength(
      2,
    );
    expect(first.nodes.find((node) => node.id === "merge")?.inputPorts).toHaveLength(
      2,
    );
  });
});

describe("independent repository graph oracles", () => {
  test("minimal graph has its independently specified endpoint tuple", () => {
    const graph = loadRepositoryGraph(
      "libgraph/test/config/topologies/minimal_graph.json",
    );
    const model = adaptGraphDocument(graph);
    expect(model.nodes.map((node) => node.id)).toEqual(["sink_1", "source_1"]);
    expect(model.edges).toHaveLength(1);
    expect(endpointTuple(model.edges[0])).toEqual([
      "source_1",
      "index",
      0,
      "sink_1",
      "index",
      0,
    ]);
  });

  test("split and merge fixtures preserve every independently listed named port", () => {
    const split = adaptGraphDocument(
      loadRepositoryGraph("libgraph/test/config/topologies/split_simple.json"),
    );
    expect(split.nodes).toHaveLength(4);
    expect(split.edges.map(endpointTuple)).toEqual([
      ["split_1", "name", "Out0", "sink_1", "name", "State"],
      ["split_1", "name", "Out1", "sink_2", "name", "State"],
      ["source_1", "name", "Data", "split_1", "name", "In"],
    ]);

    const merge = adaptGraphDocument(
      loadRepositoryGraph("libgraph/test/config/topologies/merge_simple.json"),
    );
    expect(merge.nodes).toHaveLength(4);
    expect(merge.edges.map(endpointTuple)).toEqual([
      ["merge_1", "name", "Out", "sink_1", "name", "State"],
      ["source_1", "name", "Data", "merge_1", "name", "In0"],
      ["source_2", "name", "Data", "merge_1", "name", "In1"],
    ]);
  });

  test("complex and representative imaging graphs use source-authoritative counts", () => {
    const complex = adaptGraphDocument(
      loadRepositoryGraph("libgraph/test/config/topologies/complex_network.json"),
    );
    expect([complex.nodes.length, complex.edges.length]).toEqual([9, 9]);
    const imaging = adaptGraphDocument(
      loadRepositoryGraph("examples/SAR/config/sar_stripmap_fanout.json"),
    );
    expect([imaging.nodes.length, imaging.edges.length]).toEqual([21, 23]);
    expect(imaging.edges.map(endpointTuple)).toContainEqual([
        "fanout",
        "index",
        0,
        "split_tile0",
        "index",
        0,
      ]);
  });

  test("large receiver acceptance graph has 75 nodes, 137 edges, and exact bank mappings", () => {
    const model = adaptGraphDocument(
      loadRepositoryGraph("libdsp/config/fhss_phase2_binary_iq_receiver.json"),
    );
    expect(model.diagnostics).toEqual([]);
    expect(model.nodes).toHaveLength(75);
    expect(model.edges).toHaveLength(137);
    const tuples = new Set(model.edges.map((edge) => JSON.stringify(endpointTuple(edge))));
    for (let index = 0; index < 64; ++index) {
      expect(
        tuples.has(
          JSON.stringify([
            "channelizer",
            "index",
            index,
            `detector_${index}`,
            "index",
            0,
          ]),
        ),
      ).toBe(true);
      expect(
        tuples.has(
          JSON.stringify([
            `detector_${index}`,
            "index",
            0,
            "merge",
            "index",
            index + 1,
          ]),
        ),
      ).toBe(true);
    }
  });
});
