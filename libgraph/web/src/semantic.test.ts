import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, test } from "vitest";

import { adaptGraphDocument, formatPort } from "./adapter";
import { adaptPresentationGroups } from "./hierarchy";
import { buildSemanticTopology } from "./semantic";

const repositoryRoot = resolve(process.cwd(), "../..");
const readJson = (path: string): Record<string, unknown> =>
  JSON.parse(readFileSync(resolve(repositoryRoot, path), "utf8")) as Record<string, unknown>;

const groupedComplex = readJson(
  "libgraph/test/config/topologies/generic_grouped_split_merge.json",
);
const genericNested = readJson(
  "libgraph/test/config/topologies/generic_nested_semantic.json",
);
const complexNetwork = readJson(
  "libgraph/test/config/topologies/complex_network.json",
);

describe("independent semantic topology oracle", () => {
  test("lists every generic node once in stable identity order with exact authored group paths", () => {
    const graph = adaptGraphDocument(groupedComplex);
    const hierarchy = adaptPresentationGroups(graph);
    const semantic = buildSemanticTopology(graph, hierarchy);

    expect(semantic.nodes.map((record) => record.node.id)).toEqual([
      "interior_1",
      "interior_2",
      "merge_1",
      "merge_2",
      "sink_1",
      "sink_2",
      "source_1",
      "source_2",
      "split_1",
    ]);
    expect(
      Object.fromEntries(
        semantic.nodes.map((record) => [
          record.node.id,
          record.groupPath.map((group) => group.id),
        ]),
      ),
    ).toEqual({
      interior_1: ["pipeline", "parallel-stage"],
      interior_2: ["pipeline", "parallel-stage"],
      merge_1: ["pipeline"],
      merge_2: ["pipeline", "outputs"],
      sink_1: ["pipeline", "outputs"],
      sink_2: ["pipeline", "outputs"],
      source_1: ["pipeline", "inputs"],
      source_2: ["pipeline", "inputs"],
      split_1: ["pipeline"],
    });
    expect(semantic.rootGroupIds).toEqual(["pipeline"]);
    expect(semantic.ungroupedNodeIds).toEqual([]);
    expect(new Set(semantic.nodes.map((record) => record.node.id)).size).toBe(9);
  });

  test("lists every generic edge once with independently authored endpoint and port tuples", () => {
    const graph = adaptGraphDocument(groupedComplex);
    const semantic = buildSemanticTopology(graph, adaptPresentationGroups(graph));
    const tuples = semantic.edges
      .map(({ edge }) =>
        [
          edge.sourceNodeId,
          formatPort(edge.sourcePort),
          edge.targetNodeId,
          formatPort(edge.targetPort),
        ].join(" -> "),
      )
      .sort();
    expect(tuples).toEqual([
      "interior_1 -> name “Output” -> merge_2 -> name “In0”",
      "interior_2 -> name “Output” -> merge_2 -> name “In1”",
      "merge_1 -> name “Out” -> split_1 -> name “In”",
      "merge_2 -> name “Out” -> sink_1 -> name “State”",
      "source_1 -> name “Data” -> merge_1 -> name “In0”",
      "source_2 -> name “Data” -> merge_1 -> name “In1”",
      "split_1 -> name “Out0” -> interior_1 -> name “Input”",
      "split_1 -> name “Out0” -> sink_2 -> name “State”",
      "split_1 -> name “Out1” -> interior_2 -> name “Input”",
    ].sort());
    expect(new Set(semantic.edges.map(({ edge }) => edge.id)).size).toBe(9);
  });

  test("proves the independent 4-node/3-edge nested generic identity, path, order, and full-port oracle", () => {
    const graph = adaptGraphDocument(genericNested);
    const semantic = buildSemanticTopology(graph, adaptPresentationGroups(graph));

    expect(semantic.nodes.map((record) => record.node.id)).toEqual([
      "sink",
      "source",
      "transform_a",
      "transform_b",
    ]);
    expect(semantic.primaryNodeIds).toEqual([
      "transform_a",
      "transform_b",
      "sink",
      "source",
    ]);
    expect(
      Object.fromEntries(
        semantic.nodes.map((record) => [
          record.node.id,
          record.groupPath.map((group) => group.id),
        ]),
      ),
    ).toEqual({
      sink: ["pipeline"],
      source: ["pipeline"],
      transform_a: ["pipeline", "processing"],
      transform_b: ["pipeline", "processing"],
    });
    expect(
      semantic.edges.map(({ edge }) => [
        edge.sourceNodeId,
        formatPort(edge.sourcePort),
        edge.targetNodeId,
        formatPort(edge.targetPort),
      ]),
    ).toEqual([
      ["transform_a", "index 0", "transform_b", "name “Input”"],
      ["transform_b", "name “Output”", "sink", "index 0"],
      ["source", "name “Data”", "transform_a", "index 0"],
    ]);
    expect(semantic.rootGroupIds).toEqual(["pipeline"]);
    expect(semantic.ungroupedNodeIds).toEqual([]);
  });

  test("separately proves complex_network.json 9/9 node order and full edge tuples", () => {
    const graph = adaptGraphDocument(complexNetwork);
    const semantic = buildSemanticTopology(graph, adaptPresentationGroups(graph));
    expect(semantic.nodes.map((record) => record.node.id)).toEqual([
      "interior_1",
      "interior_2",
      "merge_1",
      "merge_2",
      "sink_1",
      "sink_2",
      "source_1",
      "source_2",
      "split_1",
    ]);
    expect(
      semantic.edges
        .map(({ edge }) =>
          [
            edge.sourceNodeId,
            formatPort(edge.sourcePort),
            edge.targetNodeId,
            formatPort(edge.targetPort),
          ].join(" -> "),
        )
        .sort(),
    ).toEqual([
      "interior_1 -> name “Output” -> merge_2 -> name “In0”",
      "interior_2 -> name “Output” -> merge_2 -> name “In1”",
      "merge_1 -> name “Out” -> split_1 -> name “In”",
      "merge_2 -> name “Out” -> sink_1 -> name “State”",
      "source_1 -> name “Data” -> merge_1 -> name “In0”",
      "source_2 -> name “Data” -> merge_1 -> name “In1”",
      "split_1 -> name “Out0” -> interior_1 -> name “Input”",
      "split_1 -> name “Out0” -> sink_2 -> name “State”",
      "split_1 -> name “Out1” -> interior_2 -> name “Input”",
    ].sort());
    expect(semantic.edges).toHaveLength(9);
  });

  test.each([
    ["libgraph/test/config/topologies/minimal_graph.json", 2, 1],
    ["libgraph/test/config/topologies/generic_nested_semantic.json", 4, 3],
    ["libgraph/test/config/topologies/complex_network.json", 9, 9],
    ["examples/SAR/config/sar_stripmap_fanout.json", 21, 23],
    ["libdsp/config/fhss_phase2_binary_iq_receiver.json", 75, 137],
  ])("matches direct source JSON inventory for %s", (path, nodeCount, edgeCount) => {
    const document = readJson(path);
    const sourceNodeIds = (document.nodes as Array<{ id: string }>)
      .map((node) => node.id)
      .sort();
    const graph = adaptGraphDocument(document);
    const semantic = buildSemanticTopology(graph, adaptPresentationGroups(graph));
    expect(semantic.nodes.map((record) => record.node.id)).toEqual(sourceNodeIds);
    expect(semantic.nodes).toHaveLength(nodeCount);
    expect(semantic.edges).toHaveLength(edgeCount);
  });

  test("keeps valid records plus one associated/global warning inventory on invalid input", () => {
    const document = {
      nodes: [
        { id: "valid", type: "Source" },
        { id: "valid", type: "Duplicate" },
        { id: "sink", type: "Sink" },
      ],
      edges: [
        {
          source_node_id: "valid",
          source_port: 0,
          target_node_id: "sink",
          target_port: 0,
        },
        {
          source_node_id: "missing",
          source_port: 1,
          target_node_id: "sink",
          target_port: 1,
        },
      ],
      presentation: { groups: [{ id: "bad" }] },
    };
    const graph = adaptGraphDocument(document);
    const semantic = buildSemanticTopology(graph, adaptPresentationGroups(graph));
    expect(semantic.nodes.map((record) => record.node.id)).toEqual(["sink", "valid"]);
    expect(semantic.edges).toHaveLength(1);
    expect(
      [...semantic.globalWarnings, ...semantic.nodes.flatMap((record) => record.warnings)]
        .map((warning) => warning.code),
    ).toEqual(expect.arrayContaining([
      "duplicate_node_identity",
      "missing_endpoint",
      "invalid_group_label",
    ]));
  });
});
