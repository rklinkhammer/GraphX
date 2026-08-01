import { formatPort } from "./adapter";
import { compareCodeUnits } from "./identity";
import type {
  AdapterDiagnostic,
  DisplayEdge,
  DisplayGraph,
  DisplayGroup,
  DisplayHierarchy,
  DisplayNode,
  HierarchyDiagnostic,
} from "./types";

export interface SemanticNodeRecord {
  node: DisplayNode;
  directGroupId: string | null;
  groupPath: DisplayGroup[];
  warnings: Array<AdapterDiagnostic | HierarchyDiagnostic>;
}

export interface SemanticEdgeRecord {
  edge: DisplayEdge;
  warnings: AdapterDiagnostic[];
}

export interface SemanticTopologyModel {
  nodes: SemanticNodeRecord[];
  edges: SemanticEdgeRecord[];
  primaryNodeIds: string[];
  rootGroupIds: string[];
  ungroupedNodeIds: string[];
  globalWarnings: Array<AdapterDiagnostic | HierarchyDiagnostic>;
}

export function semanticPrimaryNodeIds(
  graph: DisplayGraph,
  hierarchy: DisplayHierarchy,
): string[] {
  const stableNodeIds = graph.nodes
    .map((node) => node.id)
    .sort(compareCodeUnits);
  if (hierarchy.status !== "valid") {
    return stableNodeIds;
  }
  const knownNodeIds = new Set(stableNodeIds);
  const groupsById = new Map(hierarchy.groups.map((group) => [group.id, group]));
  const ordered: string[] = [];
  const visited = new Set<string>();
  const visitGroup = (groupId: string) => {
    const group = groupsById.get(groupId);
    if (!group) return;
    [...group.childIds].sort(compareCodeUnits).forEach(visitGroup);
    [...group.directMemberIds].sort(compareCodeUnits).forEach((nodeId) => {
      if (knownNodeIds.has(nodeId) && !visited.has(nodeId)) {
        visited.add(nodeId);
        ordered.push(nodeId);
      }
    });
  };
  [...hierarchy.roots].sort(compareCodeUnits).forEach(visitGroup);
  stableNodeIds.forEach((nodeId) => {
    if (!visited.has(nodeId)) ordered.push(nodeId);
  });
  return ordered;
}

function diagnosticNamesEntity(
  diagnostic: AdapterDiagnostic | HierarchyDiagnostic,
  identity: string,
): boolean {
  return (
    diagnostic.entity === identity ||
    diagnostic.entity.includes(`[${identity}]`) ||
    diagnostic.entity.includes(`.${identity}.`)
  );
}

export function semanticGroupPath(
  hierarchy: DisplayHierarchy,
  nodeId: string,
): DisplayGroup[] {
  if (hierarchy.status !== "valid") {
    return [];
  }
  const groups = new Map(hierarchy.groups.map((group) => [group.id, group]));
  const result: DisplayGroup[] = [];
  let current = groups.get(hierarchy.nodeDirectGroupIds[nodeId] ?? "");
  while (current) {
    result.push(current);
    current =
      current.parentId === null ? undefined : groups.get(current.parentId);
  }
  return result.reverse();
}

export function buildSemanticTopology(
  graph: DisplayGraph,
  hierarchy: DisplayHierarchy,
): SemanticTopologyModel {
  const hierarchyWarnings = hierarchy.diagnostics;
  const claimedWarnings = new Set<AdapterDiagnostic | HierarchyDiagnostic>();
  const nodes = graph.nodes.map((node) => {
    const warnings = [
      ...graph.diagnostics.filter((diagnostic) =>
        diagnosticNamesEntity(diagnostic, node.id),
      ),
      ...hierarchyWarnings.filter((diagnostic) =>
        diagnosticNamesEntity(diagnostic, node.id),
      ),
    ];
    warnings.forEach((warning) => claimedWarnings.add(warning));
    return {
      node,
      directGroupId:
        hierarchy.status === "valid"
          ? hierarchy.nodeDirectGroupIds[node.id] ?? null
          : null,
      groupPath: semanticGroupPath(hierarchy, node.id),
      warnings,
    };
  });
  const edges = graph.edges.map((edge) => {
    const warnings = graph.diagnostics.filter(
      (diagnostic) =>
        diagnosticNamesEntity(diagnostic, edge.id) ||
        diagnosticNamesEntity(diagnostic, edge.sourceNodeId) ||
        diagnosticNamesEntity(diagnostic, edge.targetNodeId),
    );
    warnings.forEach((warning) => claimedWarnings.add(warning));
    return { edge, warnings };
  });
  const groupedNodeIds = new Set(
    hierarchy.status === "valid"
      ? Object.keys(hierarchy.nodeDirectGroupIds)
      : [],
  );
  return {
    nodes: nodes.sort((left, right) =>
      compareCodeUnits(left.node.id, right.node.id),
    ),
    edges: edges.sort((left, right) =>
      compareCodeUnits(left.edge.id, right.edge.id),
    ),
    primaryNodeIds: semanticPrimaryNodeIds(graph, hierarchy),
    rootGroupIds:
      hierarchy.status === "valid"
        ? [...hierarchy.roots].sort(compareCodeUnits)
        : [],
    ungroupedNodeIds: graph.nodes
      .map((node) => node.id)
      .filter((id) => !groupedNodeIds.has(id))
      .sort(compareCodeUnits),
    globalWarnings: [...graph.diagnostics, ...hierarchyWarnings].filter(
      (warning) => !claimedWarnings.has(warning),
    ),
  };
}

export function semanticNodeSearchText(record: SemanticNodeRecord): string {
  return [
    record.node.id,
    record.node.type,
    ...record.groupPath.flatMap((group) => [group.id, group.label]),
    ...record.node.inputPorts.map((port) => formatPort(port.key)),
    ...record.node.outputPorts.map((port) => formatPort(port.key)),
  ]
    .join("\n")
    .toLocaleLowerCase();
}

export function semanticEdgeSearchText(record: SemanticEdgeRecord): string {
  const edge = record.edge;
  return [
    edge.id,
    edge.sourceNodeId,
    formatPort(edge.sourcePort),
    edge.targetNodeId,
    formatPort(edge.targetPort),
  ]
    .join("\n")
    .toLocaleLowerCase();
}
