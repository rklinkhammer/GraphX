import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, test, vi } from "vitest";

import { adaptGraphDocument } from "./adapter";
import { adaptPresentationGroups, HIERARCHY_LIMITS } from "./hierarchy";
import { layoutPresentationGraph } from "./hierarchyLayout";
import { projectPresentation } from "./presentation";

const sourceRoot = resolve(process.cwd(), "../..");
const load = (path: string): Record<string, unknown> =>
  JSON.parse(readFileSync(resolve(sourceRoot, path), "utf8")) as Record<
    string,
    unknown
  >;

function grouped(document: Record<string, unknown>) {
  const model = adaptGraphDocument(document);
  const hierarchy = adaptPresentationGroups(model);
  const projection = projectPresentation(model, hierarchy, {
    mode: "grouped",
    collapsedGroupIds: new Set(),
    isolatedGroupId: null,
  });
  return { model, hierarchy, projection };
}

function directionalFixture(layout: "fanout" | "fanin") {
  return {
    name: `${layout}-fixture`,
    presentation: {
      groups: [
        {
          id: "directional",
          label: "Directional",
          members: ["a", "b", "c"],
          layout,
          collapsed_by_default: false,
        },
      ],
    },
    nodes: [
      { id: "a", type: "Node" },
      { id: "b", type: "Node" },
      { id: "c", type: "Node" },
    ],
    edges:
      layout === "fanout"
        ? [
            {
              source_node_id: "a",
              source_port: 0,
              target_node_id: "b",
              target_port: 0,
            },
            {
              source_node_id: "a",
              source_port: 1,
              target_node_id: "c",
              target_port: 0,
            },
          ]
        : [
            {
              source_node_id: "a",
              source_port: 0,
              target_node_id: "c",
              target_port: 0,
            },
            {
              source_node_id: "b",
              source_port: 0,
              target_node_id: "c",
              target_port: 1,
            },
          ],
  };
}

describe("deterministic bounded compound layout", () => {
  test("an over-bound absent-hierarchy raw graph retains semantics without invoking ELK", async () => {
    const count = HIERARCHY_LIMITS.layoutItemsPerInvocation + 1;
    const document = {
      nodes: Array.from({ length: count }, (_, index) => ({
        id: `raw-${index}`,
        type: "Node",
      })),
      edges: [],
    };
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "raw",
      collapsedGroupIds: new Set(),
      isolatedGroupId: null,
    });
    const runLayout = vi.fn();
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
      runLayout,
    );
    expect(hierarchy.status).toBe("absent");
    expect(projection.mode).toBe("raw");
    expect(projection.nodes).toHaveLength(count);
    expect(projection.diagnostic?.code).toBe("layout_invocation_bound");
    expect(runLayout).not.toHaveBeenCalled();
    expect(layout.nodes).toEqual([]);
    expect(layout.edges).toEqual([]);
    expect(model.rawNodes).toEqual(document.nodes);
  });

  test("renders nested compounds and all four authored modes deterministically", async () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const { model, hierarchy, projection } = grouped(document);
    expect(hierarchy.groups.map(({ layout }) => layout).sort()).toEqual([
      "fanin",
      "fanout",
      "grid",
      "layered",
    ]);
    const first = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    );
    const second = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    );
    expect(first.diagnostic).toBeNull();
    expect(first.fellBackToRaw).toBe(false);
    expect(first.nodes).toHaveLength(13);
    expect(first.edges).toHaveLength(9);
    expect(
      first.nodes
        .filter(({ type }) => type === "groupNode")
        .map(({ id }) => id)
        .sort(),
    ).toEqual(["inputs", "outputs", "parallel-stage", "pipeline"]);
    expect(first.nodes.find(({ id }) => id === "source_1")?.parentId).toBe(
      "inputs",
    );
    expect(first.nodes.find(({ id }) => id === "inputs")?.parentId).toBe(
      "pipeline",
    );
    expect(
      first.nodes.map(({ id, parentId, position }) => ({
        id,
        parentId: parentId ?? null,
        position,
      })),
    ).toEqual(
      second.nodes.map(({ id, parentId, position }) => ({
        id,
        parentId: parentId ?? null,
        position,
      })),
    );
  });

  test("fanout places a unique source before consumers", async () => {
    const { model, hierarchy, projection } = grouped(
      directionalFixture("fanout"),
    );
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    );
    const x = Object.fromEntries(
      layout.nodes
        .filter(({ type }) => type === "graphNode")
        .map(({ id, position }) => [id, position.x]),
    );
    expect(x.a).toBeLessThan(x.b);
    expect(x.a).toBeLessThan(x.c);
  });

  test("fanin places a unique sink after producers", async () => {
    const { model, hierarchy, projection } = grouped(
      directionalFixture("fanin"),
    );
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    );
    const x = Object.fromEntries(
      layout.nodes
        .filter(({ type }) => type === "graphNode")
        .map(({ id, position }) => [id, position.x]),
    );
    expect(x.c).toBeGreaterThan(x.a);
    expect(x.c).toBeGreaterThan(x.b);
  });

  test("a compound ELK failure falls back atomically to exact raw topology", async () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const { model, hierarchy, projection } = grouped(document);
    const forcedFailure = vi.fn(async () => {
      throw new Error("forced compound layout failure");
    });
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
      forcedFailure,
    );
    expect(layout.fellBackToRaw).toBe(true);
    expect(layout.diagnostic?.code).toBe("compound_layout_failed");
    expect(layout.nodes).toHaveLength(9);
    expect(layout.edges).toHaveLength(9);
  });

  test("a grouped visible-detail bound falls back atomically to safely bounded raw layout", async () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const { model, hierarchy } = grouped(document);
    const raw = projectPresentation(model, hierarchy, {
      mode: "raw",
      collapsedGroupIds: new Set(),
      isolatedGroupId: null,
    });
    const runLayout = vi.fn(async () => ({
      id: "root",
      width: 100,
      height: 100,
      children: model.nodes.map((node, index) => ({
        id: node.id,
        x: index * 10,
        y: 0,
        width: 100,
        height: 80,
      })),
      edges: [],
    }));
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      {
        ...raw,
        diagnostic: {
          code: "visible_detail_bound",
          entity: "presentation",
          detail: "test bound",
        },
      },
      null,
      runLayout,
    );
    expect(runLayout).toHaveBeenCalledOnce();
    expect(layout.fellBackToRaw).toBe(true);
    expect(layout.diagnostic?.code).toBe("visible_detail_bound");
    expect(layout.nodes).toHaveLength(model.nodes.length);
    expect(layout.edges).toHaveLength(model.edges.length);
    expect(model.rawNodes).toEqual(document.nodes);
    expect(model.rawEdges).toEqual(document.edges);
  });

  test("renders a singleton crossing with exact edge identity and a presentation boundary handle", async () => {
    const document = directionalFixture("fanout");
    document.presentation.groups[0].members = ["b", "c"];
    document.presentation.groups[0].collapsed_by_default = true;
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["directional"]),
      isolatedGroupId: null,
    });
    const layout = await layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    );
    expect(projection.bundles).toHaveLength(1);
    expect(projection.bundles[0].memberEdgeIds).toHaveLength(2);

    const singletonDocument = {
      ...document,
      edges: [document.edges[0]],
    };
    const singletonModel = adaptGraphDocument(singletonDocument);
    const singletonHierarchy = adaptPresentationGroups(singletonModel);
    const singletonProjection = projectPresentation(
      singletonModel,
      singletonHierarchy,
      {
        mode: "grouped",
        collapsedGroupIds: new Set(["directional"]),
        isolatedGroupId: null,
      },
    );
    const singletonLayout = await layoutPresentationGraph(
      singletonModel,
      singletonHierarchy,
      singletonProjection,
      null,
    );
    expect(layout.diagnostic).toBeNull();
    expect(singletonLayout.edges).toHaveLength(1);
    expect(singletonLayout.edges[0]).toMatchObject({
      id: singletonModel.edges[0].id,
      source: "a",
      sourceHandle: singletonModel.edges[0].sourceHandleId,
      target: "directional",
      targetHandle: "presentation-boundary-input",
    });
  });
});
