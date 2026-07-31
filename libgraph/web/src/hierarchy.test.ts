import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, test } from "vitest";

import { adaptGraphDocument, edgeIdentity } from "./adapter";
import {
  adaptPresentationGroups,
  groupBreadcrumbs,
  HIERARCHY_LIMITS,
} from "./hierarchy";
import { compareCodeUnits } from "./identity";
import { validateLayoutWork } from "./hierarchyLayout";
import {
  bundleIdentity,
  groupContainsSelection,
  projectPresentation,
  rawCanvasBound,
  reconcileCollapsedGroups,
} from "./presentation";
import {
  directMembersGraph,
  graphAtGroupBoundary,
  graphWithNodes,
  nestedGraphAtDepth,
  totalMembershipGraph,
} from "./test/boundaryFixtures";
import invalidGroups from "./test/fixtures/invalid_groups.json";
import numericPorts from "./test/fixtures/numeric_ports.json";

const sourceRoot = resolve(process.cwd(), "../..");
const load = (path: string): Record<string, unknown> =>
  JSON.parse(readFileSync(resolve(sourceRoot, path), "utf8")) as Record<
    string,
    unknown
  >;

function hierarchyOf(document: unknown) {
  return adaptPresentationGroups(adaptGraphDocument(document));
}

function validGroup(
  overrides: Record<string, unknown> = {},
): Record<string, unknown> {
  return {
    id: "group",
    label: "Group",
    members: ["numeric-sink"],
    layout: "grid",
    collapsed_by_default: false,
    ...overrides,
  };
}

function withGroups(groups: unknown, presentationExtras = {}) {
  return {
    ...structuredClone(numericPorts),
    presentation: { groups, ...presentationExtras },
  };
}

function diagnosticCode(document: unknown): string | null {
  return hierarchyOf(document).diagnostics[0]?.code ?? null;
}

describe("locked presentation.groups schema", () => {
  test("absent presentation and absent groups preserve exact Phase 1 display", () => {
    expect(hierarchyOf(numericPorts)).toEqual({
      status: "absent",
      groups: [],
      roots: [],
      nodeDirectGroupIds: {},
      diagnostics: [],
    });
    expect(
      hierarchyOf({ ...structuredClone(numericPorts), presentation: {} }).status,
    ).toBe("absent");
  });

  test("normalizes a nested generic forest with direct/transitive membership", () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const oracle = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.oracle.json",
    );
    const hierarchy = hierarchyOf(document);
    expect(hierarchy.status).toBe("valid");
    expect(hierarchy.roots).toEqual(["pipeline"]);
    expect(hierarchy.groups.map(({ id, layout }) => [id, layout])).toEqual([
      ["inputs", "grid"],
      ["outputs", "fanin"],
      ["parallel-stage", "fanout"],
      ["pipeline", "layered"],
    ]);
    const oracleGroups = oracle.groups as Record<
      string,
      Record<string, unknown>
    >;
    for (const group of hierarchy.groups) {
      const expected = oracleGroups[group.id];
      expect(group.parentId).toEqual(expected.parent);
      expect(group.directMemberIds).toEqual(expected.direct_members);
      if (expected.transitive_members) {
        expect(group.memberNodeIds).toEqual(expected.transitive_members);
      }
      if (expected.children) {
        expect(group.childIds).toEqual(expected.children);
      }
    }
    expect(groupBreadcrumbs(hierarchy, "parallel-stage").map(({ id }) => id)).toEqual(
      ["pipeline", "parallel-stage"],
    );
  });

  test.each([
    ["presentation shape", { ...numericPorts, presentation: [] }, "invalid_presentation"],
    ["groups shape", withGroups({}), "invalid_groups"],
    [
      "presentation unknown field",
      withGroups([validGroup()], { local_positions: {} }),
      "unknown_presentation_field",
    ],
    ["group shape", withGroups([null]), "invalid_group"],
    ["unknown group field", withGroups([validGroup({ color: "blue" })]), "unknown_group_field"],
    ["empty id", withGroups([validGroup({ id: "" })]), "invalid_group_id"],
    [
      "duplicate id",
      withGroups([validGroup(), validGroup({ members: ["numeric-source"] })]),
      "duplicate_group_id",
    ],
    [
      "node id collision",
      withGroups([validGroup({ id: "numeric-source" })]),
      "group_node_id_collision",
    ],
    ["empty label", withGroups([validGroup({ label: "" })]), "invalid_group_label"],
    ["empty members", withGroups([validGroup({ members: [] })]), "invalid_group_members"],
    [
      "invalid member shape",
      withGroups([validGroup({ members: [4] })]),
      "invalid_group_member",
    ],
    [
      "duplicate member",
      withGroups([validGroup({ members: ["numeric-sink", "numeric-sink"] })]),
      "duplicate_group_member",
    ],
    [
      "unknown member",
      withGroups([validGroup({ members: ["unknown"] })]),
      "unknown_group_member",
    ],
    [
      "overlap",
      withGroups([
        validGroup(),
        validGroup({ id: "other", label: "Other" }),
      ]),
      "overlapping_group_member",
    ],
    [
      "unknown parent",
      withGroups([validGroup({ parent: "unknown" })]),
      "unknown_group_parent",
    ],
    [
      "self parent",
      withGroups([validGroup({ parent: "group" })]),
      "self_parent_group",
    ],
    [
      "parent shape",
      withGroups([validGroup({ parent: 3 })]),
      "invalid_group_parent",
    ],
    [
      "layout",
      withGroups([validGroup({ layout: "circle" })]),
      "invalid_group_layout",
    ],
    [
      "collapsed boolean",
      withGroups([validGroup({ collapsed_by_default: 0 })]),
      "invalid_collapsed_default",
    ],
  ])("atomically rejects %s", (_name, document, code) => {
    const hierarchy = hierarchyOf(document);
    expect(hierarchy.status).toBe("invalid");
    expect(hierarchy.groups).toEqual([]);
    expect(hierarchy.nodeDirectGroupIds).toEqual({});
    expect(hierarchy.diagnostics).toHaveLength(1);
    expect(hierarchy.diagnostics[0].code).toBe(code);
  });

  test("rejects overlap/cycle fixture deterministically without partial state", () => {
    const first = hierarchyOf(invalidGroups);
    const second = hierarchyOf(structuredClone(invalidGroups));
    expect(first).toEqual(second);
    expect(first.status).toBe("invalid");
    expect(first.groups).toEqual([]);
    expect(first.diagnostics[0].code).toBe("overlapping_group_member");

    const cycle = withGroups([
      validGroup({ id: "a", members: ["numeric-source"], parent: "b" }),
      validGroup({ id: "b", members: ["numeric-sink"], parent: "a" }),
    ]);
    expect(diagnosticCode(cycle)).toBe("group_parent_cycle");
  });

  test.each([
    [
      "libgraph/test/config/topologies/generic_grouped_invalid_overlap.json",
      "overlapping_group_member",
      3,
      2,
    ],
    [
      "libgraph/test/config/topologies/generic_grouped_invalid_cycle.json",
      "group_parent_cycle",
      2,
      1,
    ],
    [
      "libgraph/test/config/topologies/generic_grouped_invalid_unknown_member.json",
      "unknown_group_member",
      2,
      1,
    ],
    [
      "libgraph/test/config/topologies/generic_grouped_over_group_bound.json",
      "group_count_bound",
      2,
      1,
    ],
  ])(
    "repository invalid fixture %s rejects all grouping and preserves exact raw topology",
    (path, code, nodeCount, edgeCount) => {
      const document = load(path);
      const model = adaptGraphDocument(document);
      const hierarchy = adaptPresentationGroups(model);
      const projection = projectPresentation(model, hierarchy, {
        mode: "grouped",
        collapsedGroupIds: new Set(["first", "second"]),
        isolatedGroupId: "first",
      });
      expect(hierarchy.status).toBe("invalid");
      expect(hierarchy.groups).toEqual([]);
      expect(hierarchy.nodeDirectGroupIds).toEqual({});
      expect(hierarchy.diagnostics).toHaveLength(1);
      expect(hierarchy.diagnostics[0].code).toBe(code);
      expect(projection.mode).toBe("raw");
      expect(projection.nodes).toHaveLength(nodeCount);
      expect(projection.edges).toHaveLength(edgeCount);
      expect(projection.bundles).toEqual([]);
      expect(projection.nodes.map(({ id }) => id).sort()).toEqual(
        (document.nodes as Array<{ id: string }>).map(({ id }) => id).sort(),
      );
      expect(projection.edges.map(({ id }) => id)).toEqual(
        model.edges.map(({ id }) => id),
      );
    },
  );
});

describe("every locked numeric boundary", () => {
  test.each([-1, 0, 1] as const)("group count boundary delta %s", (delta) => {
    const hierarchy = hierarchyOf(graphAtGroupBoundary(delta));
    expect(hierarchy.status).toBe(delta === 1 ? "invalid" : "valid");
    expect(hierarchy.diagnostics[0]?.code).toBe(
      delta === 1 ? "group_count_bound" : undefined,
    );
  });

  test.each([-1, 0, 1] as const)("parent depth boundary delta %s", (delta) => {
    const hierarchy = hierarchyOf(
      nestedGraphAtDepth(HIERARCHY_LIMITS.parentDepth + delta),
    );
    expect(hierarchy.status).toBe(delta === 1 ? "invalid" : "valid");
    expect(hierarchy.diagnostics[0]?.code).toBe(
      delta === 1 ? "parent_depth_bound" : undefined,
    );
  });

  test.each([-1, 0, 1] as const)("direct member boundary delta %s", (delta) => {
    const hierarchy = hierarchyOf(
      directMembersGraph(HIERARCHY_LIMITS.directMembersPerGroup + delta),
    );
    expect(hierarchy.status).toBe(delta === 1 ? "invalid" : "valid");
    expect(hierarchy.diagnostics[0]?.code).toBe(
      delta === 1 ? "direct_member_bound" : undefined,
    );
  });

  test.each([-1, 0, 1] as const)("total membership boundary delta %s", (delta) => {
    const hierarchy = hierarchyOf(
      totalMembershipGraph(HIERARCHY_LIMITS.totalDirectMemberships + delta),
    );
    expect(hierarchy.status).toBe(delta === 1 ? "invalid" : "valid");
    expect(hierarchy.diagnostics[0]?.code).toBe(
      delta === 1 ? "total_membership_bound" : undefined,
    );
  });

  test.each([-1, 0, 1] as const)(
    "bundle member boundary delta %s",
    (delta) => {
      const edgeCount = HIERARCHY_LIMITS.authoritativeEdgesPerBundle + delta;
      const document = {
        name: "bundle-boundary",
        presentation: {
          groups: [
            {
              id: "collapsed",
              label: "Collapsed",
              members: ["sink"],
              layout: "grid",
              collapsed_by_default: true,
            },
          ],
        },
        nodes: [
          { id: "source", type: "Source" },
          { id: "sink", type: "Sink" },
        ],
        edges: Array.from({ length: edgeCount }, (_, index) => ({
          source_node_id: "source",
          source_port: index,
          target_node_id: "sink",
          target_port: index,
        })),
      };
      const model = adaptGraphDocument(document);
      const hierarchy = adaptPresentationGroups(model);
      const projection = projectPresentation(model, hierarchy, {
        mode: "grouped",
        collapsedGroupIds: new Set(["collapsed"]),
        isolatedGroupId: null,
      });
      expect(projection.diagnostic?.code).toBe(
        delta === 1 ? "bundle_member_bound" : undefined,
      );
      expect(projection.mode).toBe(delta === 1 ? "raw" : "grouped");
    },
    30_000,
  );

  test("layout invocation and cumulative work boundaries are deterministic", () => {
    for (const delta of [-1, 0, 1]) {
      const perCall = validateLayoutWork([
        HIERARCHY_LIMITS.layoutItemsPerInvocation + delta,
      ]);
      expect(perCall?.code).toBe(
        delta === 1 ? "layout_invocation_bound" : undefined,
      );
      const cumulative = validateLayoutWork(
        delta === 1
          ? [
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              1,
            ]
          : [
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation,
              HIERARCHY_LIMITS.layoutItemsPerInvocation + delta,
            ],
      );
      expect(cumulative?.code).toBe(
        delta === 1 ? "cumulative_layout_bound" : undefined,
      );
    }
  });

  test.each([-1, 0, 1] as const)("visible detail boundary delta %s", (delta) => {
    const nodeCount = HIERARCHY_LIMITS.visibleDetail - 1 + delta;
    const document = graphWithNodes(nodeCount);
    document.presentation = {
      groups: [
        {
          id: "one-group",
          label: "One group",
          members: ["node-0"],
          layout: "grid",
          collapsed_by_default: false,
        },
      ],
    };
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(),
      isolatedGroupId: null,
    });
    expect(projection.diagnostic?.code).toBe(
      delta === 1 ? "visible_detail_bound" : undefined,
    );
  });
});

describe("bundle, raw, and selection semantics", () => {
  test.each([-1, 0, 1] as const)(
    "enforces the raw canvas layout boundary at delta %s",
    (delta) => {
      const count = HIERARCHY_LIMITS.layoutItemsPerInvocation + delta;
      const model = {
        nodes: Array.from({ length: count }),
        edges: [],
      } as unknown as ReturnType<typeof adaptGraphDocument>;
      expect(rawCanvasBound(model)?.code).toBe(
        delta === 1 ? "layout_invocation_bound" : undefined,
      );
    },
  );

  test("uses collision-free visible endpoint and sorted member identity", () => {
    const members = [
      edgeIdentity(
        "a",
        { kind: "name", value: "b|c" },
        "d",
        { kind: "index", value: 0 },
      ),
      edgeIdentity(
        "a|b",
        { kind: "name", value: "c" },
        "d",
        { kind: "index", value: 0 },
      ),
    ];
    const first = bundleIdentity("group", "x|y", "node", "z", members);
    const second = bundleIdentity("group", "x", "node", "y|z", members);
    expect(first).not.toBe(second);
    expect(first).toBe(
      bundleIdentity("group", "x|y", "node", "z", members.reverse()),
    );
  });

  test("orders composed and decomposed Unicode identities by exact code units", () => {
    const decomposed = "e\u0301";
    const composed = "\u00e9";
    const memberIds = [`edge-${composed}`, `edge-${decomposed}`];
    expect(
      bundleIdentity("group", "unicode", "node", "sink", memberIds),
    ).toBe(
      bundleIdentity(
        "group",
        "unicode",
        "node",
        "sink",
        [...memberIds].reverse(),
      ),
    );
    expect([...memberIds].sort(compareCodeUnits)).toEqual([
      `edge-${decomposed}`,
      `edge-${composed}`,
    ]);

    const document = {
      nodes: [
        { id: `node-${composed}`, type: "Node" },
        { id: `node-${decomposed}`, type: "Node" },
      ],
      edges: [],
      presentation: {
        groups: [
          {
            id: `group-${composed}`,
            label: "Composed",
            members: [`node-${composed}`],
            layout: "grid",
            collapsed_by_default: false,
          },
          {
            id: `group-${decomposed}`,
            label: "Decomposed",
            members: [`node-${decomposed}`],
            layout: "grid",
            collapsed_by_default: false,
          },
        ],
      },
    };
    const forward = hierarchyOf(document);
    const reverse = hierarchyOf({
      ...document,
      nodes: [...document.nodes].reverse(),
      presentation: { groups: [...document.presentation.groups].reverse() },
    });
    expect(forward.roots).toEqual([
      `group-${decomposed}`,
      `group-${composed}`,
    ]);
    expect(reverse).toEqual(forward);
  });

  test("keeps a singleton collapsed crossing as the exact authoritative edge", () => {
    const document = {
      name: "singleton-crossing",
      presentation: {
        groups: [
          {
            id: "collapsed",
            label: "Collapsed",
            members: ["inside"],
            layout: "grid",
            collapsed_by_default: true,
          },
        ],
      },
      nodes: [
        { id: "outside", type: "Source" },
        { id: "inside", type: "Sink" },
      ],
      edges: [
        {
          source_node_id: "outside",
          source_port_name: "Out",
          target_node_id: "inside",
          target_port_name: "In",
        },
      ],
    };
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["collapsed"]),
      isolatedGroupId: null,
    });
    expect(projection.bundles).toEqual([]);
    expect(projection.edges).toHaveLength(1);
    expect(projection.edges[0]).toMatchObject({
      kind: "edge",
      id: model.edges[0].id,
      sourceId: "outside",
      sourceKind: "node",
      targetId: "collapsed",
      targetKind: "group",
    });
    expect(projection.hiddenEdgeCount).toBe(0);
  });

  test("matches the independent generic collapsed bundle oracle exactly", () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const oracle = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.oracle.json",
    );
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["parallel-stage"]),
      isolatedGroupId: null,
    });
    expect(projection.mode).toBe("grouped");
    expect(projection.hiddenNodeCount).toBe(2);
    expect(projection.bundles).toHaveLength(2);
    const tuplesById = new Map(
      model.edges.map((edge) => [
        edge.id,
        [
          edge.sourceNodeId,
          edge.sourcePort.kind,
          edge.sourcePort.value,
          edge.targetNodeId,
          edge.targetPort.kind,
          edge.targetPort.value,
        ],
      ]),
    );
    const actual = projection.bundles
      .map((bundle) =>
        bundle.memberEdgeIds.map((id) => tuplesById.get(id)),
      )
      .sort((left, right) =>
        compareCodeUnits(JSON.stringify(left), JSON.stringify(right)),
      );
    const expected = (
      (oracle.collapsed_parallel_stage as Record<string, unknown>)
        .bundle_members as unknown[]
    ).sort((left, right) =>
      compareCodeUnits(JSON.stringify(left), JSON.stringify(right)),
    );
    expect(actual).toEqual(expected);
  });

  test("keeps every fully collapsed pipeline edge as exact internal membership without a self-bundle", () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const pipeline = hierarchy.groups.find(({ id }) => id === "pipeline")!;
    expect(pipeline.internalEdgeIds).toEqual(
      model.edges.map(({ id }) => id).sort(compareCodeUnits),
    );
    const projection = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["pipeline"]),
      isolatedGroupId: null,
    });
    expect(projection.visibleGroupCount).toBe(1);
    expect(projection.visibleNodeCount).toBe(0);
    expect(projection.hiddenNodeCount).toBe(9);
    expect(projection.hiddenEdgeCount).toBe(9);
    expect(projection.edges).toEqual([]);
    expect(projection.bundles).toEqual([]);
  });

  test("raw mode equals authoritative source and collapse is idempotent", () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const raw = projectPresentation(model, hierarchy, {
      mode: "raw",
      collapsedGroupIds: new Set(["parallel-stage"]),
      isolatedGroupId: "parallel-stage",
    });
    expect(model.document).toEqual(document);
    expect(raw.nodes.map(({ id }) => id).sort()).toEqual(
      (document.nodes as Array<{ id: string }>).map(({ id }) => id).sort(),
    );
    expect(raw.edges.map(({ id }) => id)).toEqual(
      model.edges.map(({ id }) => id),
    );
    const defaults = reconcileCollapsedGroups(hierarchy, null);
    const repeated = reconcileCollapsedGroups(hierarchy, defaults);
    expect([...repeated]).toEqual([...defaults]);
  });

  test("authoritative selection remains owned separately from a bundle", () => {
    const document = load(
      "libgraph/test/config/topologies/generic_grouped_split_merge.json",
    );
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    const group = hierarchy.groups.find(({ id }) => id === "parallel-stage")!;
    expect(
      groupContainsSelection(group, { kind: "node", id: "interior_1" }),
    ).toBe(true);
    expect(
      groupContainsSelection(group, {
        kind: "edge",
        id: group.hiddenEdgeIds[0],
      }),
    ).toBe(true);
    expect(groupContainsSelection(group, { kind: "node", id: "source_1" })).toBe(
      false,
    );
  });

  test("FHSS authored metadata independently retains all 64 members and 128 bank mappings", () => {
    const document = load(
      "libdsp/config/fhss_phase2_binary_iq_receiver.json",
    );
    const model = adaptGraphDocument(document);
    const hierarchy = adaptPresentationGroups(model);
    expect([model.nodes.length, model.edges.length]).toEqual([75, 137]);
    expect(hierarchy.status).toBe("valid");
    expect(hierarchy.groups).toHaveLength(1);
    const bank = hierarchy.groups[0];
    const expectedMembers = Array.from(
      { length: 64 },
      (_, index) => `detector_${index}`,
    ).sort(compareCodeUnits);
    expect(bank.id).toBe("detector-bank");
    expect(bank.memberNodeIds).toEqual(expectedMembers);
    expect(bank.hiddenEdgeIds).toHaveLength(128);

    const collapsed = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(["detector-bank"]),
      isolatedGroupId: null,
    });
    expect(collapsed.visibleNodeCount).toBe(11);
    expect(collapsed.visibleGroupCount).toBe(1);
    expect(collapsed.hiddenNodeCount).toBe(64);
    expect(collapsed.hiddenEdgeCount).toBe(128);
    expect(collapsed.bundles).toHaveLength(2);
    expect(collapsed.bundles.map(({ memberEdgeIds }) => memberEdgeIds.length)).toEqual(
      [64, 64],
    );
    const memberIds = new Set(
      collapsed.bundles.flatMap(({ memberEdgeIds }) => memberEdgeIds),
    );
    const tuples = new Map(
      model.edges.map((edge) => [
        edge.id,
        [
          edge.sourceNodeId,
          edge.sourcePort.kind,
          edge.sourcePort.value,
          edge.targetNodeId,
          edge.targetPort.kind,
          edge.targetPort.value,
        ],
      ]),
    );
    for (let index = 0; index < 64; ++index) {
      const inbound = model.edges.find(
        (edge) =>
          edge.sourceNodeId === "channelizer" &&
          edge.sourcePort.kind === "index" &&
          edge.sourcePort.value === index &&
          edge.targetNodeId === `detector_${index}` &&
          edge.targetPort.kind === "index" &&
          edge.targetPort.value === 0,
      );
      const outbound = model.edges.find(
        (edge) =>
          edge.sourceNodeId === `detector_${index}` &&
          edge.sourcePort.kind === "index" &&
          edge.sourcePort.value === 0 &&
          edge.targetNodeId === "merge" &&
          edge.targetPort.kind === "index" &&
          edge.targetPort.value === index + 1,
      );
      expect(inbound && memberIds.has(inbound.id)).toBe(true);
      expect(outbound && memberIds.has(outbound.id)).toBe(true);
      expect(inbound && tuples.get(inbound.id)).toEqual([
        "channelizer",
        "index",
        index,
        `detector_${index}`,
        "index",
        0,
      ]);
      expect(outbound && tuples.get(outbound.id)).toEqual([
        `detector_${index}`,
        "index",
        0,
        "merge",
        "index",
        index + 1,
      ]);
    }

    const expanded = projectPresentation(model, hierarchy, {
      mode: "grouped",
      collapsedGroupIds: new Set(),
      isolatedGroupId: null,
    });
    expect(expanded.visibleNodeCount).toBe(75);
    expect(expanded.hiddenNodeCount).toBe(0);
    expect(expanded.bundles).toHaveLength(0);
    expect(expanded.edges).toHaveLength(137);
  });
});
