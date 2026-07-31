import type { Edge, Node, XYPosition } from "@xyflow/react";
import ELK from "elkjs/lib/elk.bundled.js";

import { HIERARCHY_LIMITS } from "./hierarchy";
import { compareCodeUnits } from "./identity";
import { rawCanvasBound } from "./presentation";
import { layoutDisplayGraph, type LayoutRunner } from "./layout";
import type {
  AuthoritativeSelection,
  CanvasNodeData,
  DisplayGraph,
  DisplayHierarchy,
  GroupCardData,
  GroupLayoutMode,
  HierarchyDiagnostic,
  NodeCardData,
  PresentationProjection,
  ProjectedEdge,
  ProjectedNode,
} from "./types";
import { groupContainsSelection } from "./presentation";

const elk = new ELK();
const runElkLayout: LayoutRunner = (graph) => elk.layout(graph);
const nodeWidth = 260;
const collapsedGroupWidth = 300;
const collapsedGroupHeight = 142;
const groupHeaderHeight = 66;
const groupPadding = 32;
const horizontalGap = 90;
const verticalGap = 70;

function nodeHeight(node: ProjectedNode): number {
  if (node.kind === "group") {
    return collapsedGroupHeight;
  }
  return Math.max(
    112,
    76 +
      Math.max(
        node.node?.inputPorts.length ?? 0,
        node.node?.outputPorts.length ?? 0,
      ) *
        28,
  );
}

interface Size {
  width: number;
  height: number;
}

interface DirectEdge {
  id: string;
  source: string;
  target: string;
  sourcePortId?: string;
  targetPortId?: string;
}

interface PositionResult {
  positions: Map<string, XYPosition>;
  width: number;
  height: number;
}

function gridPositions(items: ProjectedNode[], sizes: Map<string, Size>): PositionResult {
  if (items.length === 0) {
    return {
      positions: new Map(),
      width: groupPadding * 2,
      height: groupHeaderHeight + groupPadding,
    };
  }
  const ordered = [...items].sort((left, right) =>
    compareCodeUnits(left.id, right.id),
  );
  const columns = Math.max(1, Math.ceil(Math.sqrt(ordered.length)));
  const columnWidths = Array.from({ length: columns }, () => 0);
  const rows = Math.ceil(ordered.length / columns);
  const rowHeights = Array.from({ length: rows }, () => 0);
  ordered.forEach((item, index) => {
    const size = sizes.get(item.id)!;
    columnWidths[index % columns] = Math.max(
      columnWidths[index % columns],
      size.width,
    );
    rowHeights[Math.floor(index / columns)] = Math.max(
      rowHeights[Math.floor(index / columns)],
      size.height,
    );
  });
  const columnOffsets: number[] = [];
  const rowOffsets: number[] = [];
  columnWidths.forEach((width, index) => {
    columnOffsets[index] =
      index === 0
        ? groupPadding
        : columnOffsets[index - 1] +
          columnWidths[index - 1] +
          horizontalGap;
  });
  rowHeights.forEach((height, index) => {
    rowOffsets[index] =
      index === 0
        ? groupHeaderHeight
        : rowOffsets[index - 1] + rowHeights[index - 1] + verticalGap;
  });
  const positions = new Map<string, XYPosition>();
  ordered.forEach((item, index) => {
    positions.set(item.id, {
      x: columnOffsets[index % columns],
      y: rowOffsets[Math.floor(index / columns)],
    });
  });
  return {
    positions,
    width:
      groupPadding * 2 +
      columnWidths.reduce((sum, width) => sum + width, 0) +
      horizontalGap * Math.max(0, columns - 1),
    height:
      groupHeaderHeight +
      groupPadding +
      rowHeights.reduce((sum, height) => sum + height, 0) +
      verticalGap * Math.max(0, rows - 1),
  };
}

function topologyLevels(
  items: ProjectedNode[],
  edges: DirectEdge[],
  reverse: boolean,
): Map<string, number> | null {
  const ids = new Set(items.map((item) => item.id));
  const outgoing = new Map<string, Set<string>>(
    items.map((item) => [item.id, new Set<string>()]),
  );
  const incomingCount = new Map<string, number>(
    items.map((item) => [item.id, 0]),
  );
  for (const edge of edges) {
    const source = reverse ? edge.target : edge.source;
    const target = reverse ? edge.source : edge.target;
    if (!ids.has(source) || !ids.has(target) || source === target) {
      continue;
    }
    const targets = outgoing.get(source)!;
    if (!targets.has(target)) {
      targets.add(target);
      incomingCount.set(target, (incomingCount.get(target) ?? 0) + 1);
    }
  }
  const roots = [...ids]
    .filter((id) => incomingCount.get(id) === 0)
    .sort(compareCodeUnits);
  if (roots.length !== 1) {
    return null;
  }
  const levels = new Map<string, number>([[roots[0], 0]]);
  const queue = [roots[0]];
  let visited = 0;
  while (queue.length > 0) {
    const source = queue.shift()!;
    ++visited;
    const level = levels.get(source) ?? 0;
    for (const target of [...(outgoing.get(source) ?? [])].sort(
      compareCodeUnits,
    )) {
      levels.set(target, Math.max(levels.get(target) ?? 0, level + 1));
      incomingCount.set(target, (incomingCount.get(target) ?? 0) - 1);
      if (incomingCount.get(target) === 0) {
        queue.push(target);
      }
    }
  }
  return visited === items.length ? levels : null;
}

function levelPositions(
  items: ProjectedNode[],
  sizes: Map<string, Size>,
  levels: Map<string, number>,
): PositionResult {
  const levelsToItems = new Map<number, ProjectedNode[]>();
  for (const item of items) {
    const level = levels.get(item.id) ?? 0;
    const values = levelsToItems.get(level) ?? [];
    values.push(item);
    levelsToItems.set(level, values);
  }
  const orderedLevels = [...levelsToItems.keys()].sort(
    (left, right) => left - right,
  );
  const positions = new Map<string, XYPosition>();
  let x = groupPadding;
  let maximumBottom = groupHeaderHeight;
  for (const level of orderedLevels) {
    const levelItems = levelsToItems
      .get(level)!
      .sort((left, right) => compareCodeUnits(left.id, right.id));
    const width = Math.max(...levelItems.map((item) => sizes.get(item.id)!.width));
    let y = groupHeaderHeight;
    for (const item of levelItems) {
      positions.set(item.id, { x, y });
      const size = sizes.get(item.id)!;
      y += size.height + verticalGap;
      maximumBottom = Math.max(maximumBottom, y - verticalGap);
    }
    x += width + horizontalGap;
  }
  return {
    positions,
    width: x - horizontalGap + groupPadding,
    height: maximumBottom + groupPadding,
  };
}

async function layeredPositions(
  items: ProjectedNode[],
  edges: DirectEdge[],
  sizes: Map<string, Size>,
  runLayout: LayoutRunner,
): Promise<PositionResult> {
  if (items.length === 0) {
    return gridPositions(items, sizes);
  }
  const result = await runLayout({
    id: "compound",
    layoutOptions: {
      "elk.algorithm": "layered",
      "elk.direction": "RIGHT",
      "elk.edgeRouting": "ORTHOGONAL",
      "elk.layered.cycleBreaking.strategy": "GREEDY",
      "elk.layered.nodePlacement.strategy": "NETWORK_SIMPLEX",
      "elk.layered.considerModelOrder.strategy": "NODES_AND_EDGES",
      "elk.spacing.nodeNode": String(verticalGap),
      "elk.layered.spacing.nodeNodeBetweenLayers": String(horizontalGap),
    },
    children: items
      .slice()
      .sort((left, right) => compareCodeUnits(left.id, right.id))
      .map((item) => ({
        id: item.id,
        width: sizes.get(item.id)!.width,
        height: sizes.get(item.id)!.height,
        layoutOptions: {
          "org.eclipse.elk.portConstraints": "FIXED_SIDE",
        },
        ports:
          item.kind === "node"
            ? [
                ...(item.node?.inputPorts ?? []).map((port) => ({
                  id: `${item.id.length}:${item.id}|${port.id.length}:${port.id}`,
                  width: 8,
                  height: 8,
                  layoutOptions: { "org.eclipse.elk.port.side": "WEST" },
                })),
                ...(item.node?.outputPorts ?? []).map((port) => ({
                  id: `${item.id.length}:${item.id}|${port.id.length}:${port.id}`,
                  width: 8,
                  height: 8,
                  layoutOptions: { "org.eclipse.elk.port.side": "EAST" },
                })),
              ]
            : [],
      })),
    edges: edges.map((edge) => ({
      id: edge.id,
      sources: [edge.sourcePortId ?? edge.source],
      targets: [edge.targetPortId ?? edge.target],
    })),
  });
  const positions = new Map<string, XYPosition>();
  let width = groupPadding * 2;
  let height = groupHeaderHeight + groupPadding;
  for (const item of result.children ?? []) {
    const x = (item.x ?? 0) + groupPadding;
    const y = (item.y ?? 0) + groupHeaderHeight;
    positions.set(item.id, { x, y });
    const size = sizes.get(item.id)!;
    width = Math.max(width, x + size.width + groupPadding);
    height = Math.max(height, y + size.height + groupPadding);
  }
  return { positions, width, height };
}

async function arrange(
  mode: GroupLayoutMode,
  items: ProjectedNode[],
  edges: DirectEdge[],
  sizes: Map<string, Size>,
  runLayout: LayoutRunner,
): Promise<PositionResult> {
  if (mode === "layered") {
    return layeredPositions(items, edges, sizes, runLayout);
  }
  if (mode === "grid") {
    return gridPositions(items, sizes);
  }
  const levels = topologyLevels(items, edges, mode === "fanin");
  if (levels !== null && mode === "fanin") {
    const maximum = Math.max(...levels.values());
    for (const [id, level] of levels) {
      levels.set(id, maximum - level);
    }
  }
  return levels === null
    ? gridPositions(items, sizes)
    : levelPositions(items, sizes, levels);
}

function edgeEndpoints(edge: ProjectedEdge): [string, string] {
  return edge.kind === "edge"
    ? [edge.sourceId, edge.targetId]
    : [edge.bundle.sourceId, edge.bundle.targetId];
}

function directChildId(
  id: string,
  containerId: string | null,
  nodesById: Map<string, ProjectedNode>,
): string | null {
  let current = nodesById.get(id);
  if (!current) {
    return null;
  }
  while (current.parentId !== containerId) {
    if (current.parentId === null) {
      return null;
    }
    current = nodesById.get(current.parentId);
    if (!current) {
      return null;
    }
  }
  return current.id;
}

function directEdges(
  containerId: string | null,
  projection: PresentationProjection,
  nodesById: Map<string, ProjectedNode>,
): DirectEdge[] {
  const result: DirectEdge[] = [];
  for (const edge of projection.edges) {
    const [sourceId, targetId] = edgeEndpoints(edge);
    const source = directChildId(sourceId, containerId, nodesById);
    const target = directChildId(targetId, containerId, nodesById);
    if (source && target && source !== target) {
      result.push({
        id: edge.id,
        source,
        target,
        ...(edge.kind === "edge" &&
        edge.sourceKind === "node" &&
        edge.targetKind === "node" &&
        source === edge.edge.sourceNodeId &&
        target === edge.edge.targetNodeId
          ? {
              sourcePortId: `${source.length}:${source}|${edge.edge.sourceHandleId.length}:${edge.edge.sourceHandleId}`,
              targetPortId: `${target.length}:${target}|${edge.edge.targetHandleId.length}:${edge.edge.targetHandleId}`,
            }
          : {}),
      });
    }
  }
  return result.sort((left, right) => compareCodeUnits(left.id, right.id));
}

export interface HierarchyLayoutResult {
  nodes: Node<CanvasNodeData>[];
  edges: Edge[];
  diagnostic: HierarchyDiagnostic | null;
  fellBackToRaw: boolean;
}

export function validateLayoutWork(
  invocationWork: readonly number[],
): HierarchyDiagnostic | null {
  let cumulative = 0;
  for (let index = 0; index < invocationWork.length; ++index) {
    const work = invocationWork[index];
    if (work > HIERARCHY_LIMITS.layoutItemsPerInvocation) {
      return {
        code: "layout_invocation_bound",
        entity: `layout.invocations[${index}]`,
        detail: `layout invocation work ${work} exceeds limit ${HIERARCHY_LIMITS.layoutItemsPerInvocation}`,
      };
    }
    cumulative += work;
    if (cumulative > HIERARCHY_LIMITS.cumulativeLayoutWork) {
      return {
        code: "cumulative_layout_bound",
        entity: `layout.invocations[${index}]`,
        detail: `cumulative compound-layout work ${cumulative} exceeds limit ${HIERARCHY_LIMITS.cumulativeLayoutWork}`,
      };
    }
  }
  return null;
}

export async function layoutPresentationGraph(
  model: DisplayGraph,
  hierarchy: DisplayHierarchy,
  projection: PresentationProjection,
  selection: AuthoritativeSelection,
  runLayout: LayoutRunner = runElkLayout,
): Promise<HierarchyLayoutResult> {
  if (projection.mode === "raw") {
    const rawBound = rawCanvasBound(model);
    if (rawBound !== null) {
      return {
        nodes: [],
        edges: [],
        diagnostic: projection.diagnostic ?? rawBound,
        fellBackToRaw: true,
      };
    }
    const raw = await layoutDisplayGraph(model, runLayout);
    return {
      nodes: raw.nodes,
      edges: raw.edges,
      diagnostic:
        projection.diagnostic ??
        (raw.diagnostic
          ? {
              code: "raw_layout_fallback",
              entity: "topology",
              detail: raw.diagnostic,
            }
          : null),
      fellBackToRaw: projection.diagnostic !== null,
    };
  }

  const nodesById = new Map(
    projection.nodes.map((node) => [node.id, node]),
  );
  const groupById = new Map(
    hierarchy.groups.map((group) => [group.id, group]),
  );
  const presentationBoundaryInputs = new Set<string>();
  const presentationBoundaryOutputs = new Set<string>();
  for (const edge of projection.edges) {
    if (edge.kind === "bundle") {
      presentationBoundaryOutputs.add(edge.bundle.sourceId);
      presentationBoundaryInputs.add(edge.bundle.targetId);
    } else {
      if (edge.sourceKind === "group") {
        presentationBoundaryOutputs.add(edge.sourceId);
      }
      if (edge.targetKind === "group") {
        presentationBoundaryInputs.add(edge.targetId);
      }
    }
  }
  const sizes = new Map<string, Size>();
  const positions = new Map<string, XYPosition>();
  const layoutInvocationWork: number[] = [];

  const layoutContainer = async (
    containerId: string | null,
    mode: GroupLayoutMode,
  ): Promise<PositionResult | HierarchyDiagnostic> => {
    const items = projection.nodes.filter(
      (node) => node.parentId === containerId,
    );
    const edges = directEdges(containerId, projection, nodesById);
    const work = items.length + edges.length;
    layoutInvocationWork.push(work);
    const boundDiagnostic = validateLayoutWork(layoutInvocationWork);
    if (boundDiagnostic) {
      return {
        ...boundDiagnostic,
        entity: containerId ?? "presentation.root",
      };
    }
    try {
      return await arrange(mode, items, edges, sizes, runLayout);
    } catch (error) {
      return {
        code: "compound_layout_failed",
        entity: containerId ?? "presentation.root",
        detail: `compound layout failed: ${
          error instanceof Error ? error.message : String(error)
        }`,
      };
    }
  };

  for (const projected of projection.nodes) {
    if (projected.kind === "node") {
      sizes.set(projected.id, {
        width: nodeWidth,
        height: nodeHeight(projected),
      });
    } else if (projected.collapsed) {
      sizes.set(projected.id, {
        width: collapsedGroupWidth,
        height: collapsedGroupHeight,
      });
    }
  }
  const visibleExpandedGroups = projection.nodes
    .filter(
      (node): node is ProjectedNode & { group: NonNullable<ProjectedNode["group"]> } =>
        node.kind === "group" && !node.collapsed && node.group !== undefined,
    )
    .sort((left, right) => {
      const depthDifference = right.group.depth - left.group.depth;
      return depthDifference !== 0
        ? depthDifference
        : compareCodeUnits(left.id, right.id);
    });
  for (const projected of visibleExpandedGroups) {
    const arranged = await layoutContainer(
      projected.id,
      projected.group.layout,
    );
    if ("code" in arranged) {
      if (
        arranged.code === "layout_invocation_bound" ||
        arranged.code === "cumulative_layout_bound"
      ) {
        const rawBound = rawCanvasBound(model);
        if (rawBound === null) {
          const raw = await layoutDisplayGraph(model, runLayout);
          return {
            nodes: raw.nodes,
            edges: raw.edges,
            diagnostic: arranged,
            fellBackToRaw: true,
          };
        }
        return {
          nodes: [],
          edges: [],
          diagnostic: rawBound,
          fellBackToRaw: true,
        };
      }
      const raw = await layoutDisplayGraph(model, runLayout);
      return {
        nodes: raw.nodes,
        edges: raw.edges,
        diagnostic: arranged,
        fellBackToRaw: true,
      };
    }
    sizes.set(projected.id, {
      width: Math.max(collapsedGroupWidth, arranged.width),
      height: Math.max(collapsedGroupHeight, arranged.height),
    });
    for (const [id, position] of arranged.positions) {
      positions.set(id, position);
    }
  }

  const root = await layoutContainer(null, "layered");
  if ("code" in root) {
    if (
      root.code === "layout_invocation_bound" ||
      root.code === "cumulative_layout_bound"
    ) {
      const rawBound = rawCanvasBound(model);
      if (rawBound === null) {
        const raw = await layoutDisplayGraph(model, runLayout);
        return {
          nodes: raw.nodes,
          edges: raw.edges,
          diagnostic: root,
          fellBackToRaw: true,
        };
      }
      return {
        nodes: [],
        edges: [],
        diagnostic: rawBound,
        fellBackToRaw: true,
      };
    }
    const raw = await layoutDisplayGraph(model, runLayout);
    return {
      nodes: raw.nodes,
      edges: raw.edges,
      diagnostic: root,
      fellBackToRaw: true,
    };
  }
  for (const [id, position] of root.positions) {
    positions.set(id, position);
  }

  const renderedNodes: Node<CanvasNodeData>[] = projection.nodes
    .slice()
    .sort((left, right) => {
      if (left.kind === "group" && right.kind === "group") {
        const leftDepth = left.group?.depth ?? 0;
        const rightDepth = right.group?.depth ?? 0;
        return leftDepth !== rightDepth
          ? leftDepth - rightDepth
          : compareCodeUnits(left.id, right.id);
      }
      if (left.kind !== right.kind) {
        return left.kind === "group" ? -1 : 1;
      }
      return compareCodeUnits(left.id, right.id);
    })
    .map((projected) => {
      const size = sizes.get(projected.id) ?? {
        width: nodeWidth,
        height: nodeHeight(projected),
      };
      if (projected.kind === "node") {
        const data: NodeCardData = {
          kind: "node",
          node: projected.node!,
          selected:
            selection?.kind === "node" && selection.id === projected.id,
          presentationBoundaryInput: presentationBoundaryInputs.has(
            projected.id,
          ),
          presentationBoundaryOutput: presentationBoundaryOutputs.has(
            projected.id,
          ),
        };
        return {
          id: projected.id,
          type: "graphNode",
          parentId: projected.parentId ?? undefined,
          position: positions.get(projected.id) ?? { x: 0, y: 0 },
          width: size.width,
          height: size.height,
          data,
          draggable: false,
          connectable: false,
        };
      }
      const group = projected.group ?? groupById.get(projected.id)!;
      const data: GroupCardData = {
        kind: "group",
        group,
        collapsed: projected.collapsed ?? false,
        containsSelection: groupContainsSelection(group, selection),
        presentationBoundaryInput: presentationBoundaryInputs.has(
          projected.id,
        ),
        presentationBoundaryOutput: presentationBoundaryOutputs.has(
          projected.id,
        ),
      };
      return {
        id: projected.id,
        type: "groupNode",
        parentId: projected.parentId ?? undefined,
        position: positions.get(projected.id) ?? { x: 0, y: 0 },
        width: size.width,
        height: size.height,
        data,
        draggable: false,
        connectable: false,
        selectable: true,
        style: { width: size.width, height: size.height },
      };
    });

  const renderedEdges: Edge[] = projection.edges.map((projected) => {
    if (projected.kind === "edge") {
      const edge = projected.edge;
      return {
        id: edge.id,
        source: projected.sourceId,
        sourceHandle:
          projected.sourceKind === "node"
            ? edge.sourceHandleId
            : "presentation-boundary-output",
        target: projected.targetId,
        targetHandle:
          projected.targetKind === "node"
            ? edge.targetHandleId
            : "presentation-boundary-input",
        type: "smoothstep",
        data: { edge },
        ariaLabel:
          `Edge from ${edge.sourceNodeId} ${edge.sourcePort.kind} ${edge.sourcePort.value} ` +
          `to ${edge.targetNodeId} ${edge.targetPort.kind} ${edge.targetPort.value}. ` +
          "Press Enter or Space to select.",
      };
    }
    const bundle = projected.bundle;
    return {
      id: bundle.id,
      source: bundle.sourceId,
      sourceHandle: "presentation-boundary-output",
      target: bundle.targetId,
      targetHandle: "presentation-boundary-input",
      type: "smoothstep",
      className: "bundle-edge",
      data: { bundle },
      label: `${bundle.memberEdgeIds.length} edges`,
      ariaLabel:
        `Presentation bundle from ${bundle.sourceId} to ${bundle.targetId}, ` +
        `${bundle.memberEdgeIds.length} authoritative edges. Press Enter or Space to inspect.`,
    };
  });
  return {
    nodes: renderedNodes,
    edges: renderedEdges,
    diagnostic: null,
    fellBackToRaw: false,
  };
}
