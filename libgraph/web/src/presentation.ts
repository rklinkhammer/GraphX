import { HIERARCHY_LIMITS } from "./hierarchy";
import { compareCodeUnits } from "./identity";
import type {
  AuthoritativeSelection,
  BundleEdge,
  DisplayGraph,
  DisplayGroup,
  DisplayHierarchy,
  HierarchyDiagnostic,
  PresentationProjection,
  PresentationState,
  ProjectedEdge,
  ProjectedNode,
} from "./types";

const encodeString = (value: string): string => `${value.length}:${value}`;

function endpointIdentity(kind: "node" | "group", id: string): string {
  return `${kind === "node" ? "n" : "g"}${encodeString(id)}`;
}

export function bundleIdentity(
  sourceKind: "node" | "group",
  sourceId: string,
  targetKind: "node" | "group",
  targetId: string,
  memberEdgeIds: readonly string[],
): string {
  const sorted = [...new Set(memberEdgeIds)].sort(compareCodeUnits);
  return [
    "bundle",
    encodeString(endpointIdentity(sourceKind, sourceId)),
    encodeString(endpointIdentity(targetKind, targetId)),
    String(sorted.length),
    ...sorted.map(encodeString),
  ].join("|");
}

export function rawCanvasBound(
  model: DisplayGraph,
): HierarchyDiagnostic | null {
  const visibleDetail = model.nodes.length + model.edges.length;
  if (visibleDetail > HIERARCHY_LIMITS.visibleDetail) {
    return {
      code: "visible_detail_bound",
      entity: "raw topology",
      detail: `raw node/edge count ${visibleDetail} exceeds visible limit ${HIERARCHY_LIMITS.visibleDetail}; exact semantic data remains available without canvas layout`,
    };
  }
  if (visibleDetail > HIERARCHY_LIMITS.layoutItemsPerInvocation) {
    return {
      code: "layout_invocation_bound",
      entity: "raw topology",
      detail: `raw node/edge layout work ${visibleDetail} exceeds limit ${HIERARCHY_LIMITS.layoutItemsPerInvocation}; exact semantic data remains available without canvas layout`,
    };
  }
  return null;
}

function rawProjection(model: DisplayGraph): PresentationProjection {
  return {
    mode: "raw",
    nodes: model.nodes.map((node) => ({
      id: node.id,
      kind: "node",
      parentId: null,
      node,
    })),
    edges: model.edges.map((edge) => ({
      kind: "edge",
      id: edge.id,
      edge,
      sourceId: edge.sourceNodeId,
      sourceKind: "node",
      targetId: edge.targetNodeId,
      targetKind: "node",
    })),
    bundles: [],
    visibleNodeCount: model.nodes.length,
    visibleGroupCount: 0,
    hiddenNodeCount: 0,
    hiddenEdgeCount: 0,
    diagnostic: rawCanvasBound(model),
  };
}

function rawFallback(
  model: DisplayGraph,
  diagnostic: HierarchyDiagnostic,
): PresentationProjection {
  return { ...rawProjection(model), diagnostic };
}

interface VisibleEndpoint {
  id: string;
  kind: "node" | "group";
}

export function projectPresentation(
  model: DisplayGraph,
  hierarchy: DisplayHierarchy,
  state: PresentationState,
): PresentationProjection {
  if (state.mode === "raw" || hierarchy.status === "absent") {
    return rawProjection(model);
  }
  if (hierarchy.status === "invalid") {
    return rawFallback(
      model,
      hierarchy.diagnostics[0] ?? {
        code: "invalid_hierarchy",
        entity: "presentation.groups",
        detail: "presentation grouping is invalid",
      },
    );
  }

  const groupsById = new Map(
    hierarchy.groups.map((group) => [group.id, group]),
  );
  const isolate =
    state.isolatedGroupId === null
      ? null
      : groupsById.get(state.isolatedGroupId) ?? null;
  const scopeNodeIds =
    isolate === null ? null : new Set(isolate.memberNodeIds);
  const scopeGroupIds =
    isolate === null
      ? null
      : new Set(isolate.descendantGroupIds);
  const collapsed = new Set(
    [...state.collapsedGroupIds].filter((id) => groupsById.has(id)),
  );

  const ancestors = (group: DisplayGroup): DisplayGroup[] => {
    const result: DisplayGroup[] = [];
    let current: DisplayGroup | undefined = group;
    while (current) {
      result.push(current);
      current =
        current.parentId === null
          ? undefined
          : groupsById.get(current.parentId);
    }
    return result.reverse();
  };

  const visibleCollapsedAncestor = (
    nodeId: string,
  ): DisplayGroup | undefined => {
    const directId = hierarchy.nodeDirectGroupIds[nodeId];
    const direct = directId ? groupsById.get(directId) : undefined;
    if (!direct) {
      return undefined;
    }
    return ancestors(direct).find(
      (group) =>
        group.id !== isolate?.id &&
        collapsed.has(group.id) &&
        (scopeGroupIds === null || scopeGroupIds.has(group.id)),
    );
  };

  const visibleGroups = hierarchy.groups.filter((group) => {
    if (group.id === isolate?.id) {
      return false;
    }
    if (scopeGroupIds !== null && !scopeGroupIds.has(group.id)) {
      return false;
    }
    return !ancestors(group)
      .slice(0, -1)
      .some((ancestor) => collapsed.has(ancestor.id));
  });
  const visibleGroupIds = new Set(visibleGroups.map((group) => group.id));
  const projectedNodes: ProjectedNode[] = visibleGroups.map((group) => ({
    id: group.id,
    kind: "group",
    parentId:
      group.parentId !== null && visibleGroupIds.has(group.parentId)
        ? group.parentId
        : null,
    group,
    collapsed: collapsed.has(group.id),
  }));

  const visibleNodes = model.nodes.filter((node) => {
    if (scopeNodeIds !== null && !scopeNodeIds.has(node.id)) {
      return false;
    }
    return visibleCollapsedAncestor(node.id) === undefined;
  });
  for (const node of visibleNodes) {
    const directGroupId = hierarchy.nodeDirectGroupIds[node.id];
    projectedNodes.push({
      id: node.id,
      kind: "node",
      parentId:
        directGroupId !== undefined && visibleGroupIds.has(directGroupId)
          ? directGroupId
          : null,
      node,
    });
  }
  projectedNodes.sort((left, right) => {
    if (left.kind !== right.kind) {
      return left.kind === "group" ? -1 : 1;
    }
    return compareCodeUnits(left.id, right.id);
  });
  if (projectedNodes.length > HIERARCHY_LIMITS.visibleDetail) {
    return rawFallback(model, {
      code: "visible_detail_bound",
      entity: "presentation",
      detail: `visible node/edge/bundle count ${projectedNodes.length} exceeds limit ${HIERARCHY_LIMITS.visibleDetail}`,
    });
  }

  const endpoints = new Map<string, VisibleEndpoint>();
  for (const node of model.nodes) {
    if (scopeNodeIds !== null && !scopeNodeIds.has(node.id)) {
      continue;
    }
    const collapsedAncestor = visibleCollapsedAncestor(node.id);
    endpoints.set(
      node.id,
      collapsedAncestor
        ? { id: collapsedAncestor.id, kind: "group" }
        : { id: node.id, kind: "node" },
    );
  }

  const authoritative: ProjectedEdge[] = [];
  const edgesById = new Map(model.edges.map((edge) => [edge.id, edge]));
  const bundleMembers = new Map<
    string,
    {
      source: VisibleEndpoint;
      target: VisibleEndpoint;
      members: string[];
    }
  >();
  for (const edge of model.edges) {
    const source = endpoints.get(edge.sourceNodeId);
    const target = endpoints.get(edge.targetNodeId);
    if (!source || !target) {
      continue;
    }
    if (source.id === target.id && source.kind === target.kind) {
      continue;
    }
    if (source.kind === "node" && target.kind === "node") {
      authoritative.push({
        kind: "edge",
        id: edge.id,
        edge,
        sourceId: source.id,
        sourceKind: source.kind,
        targetId: target.id,
        targetKind: target.kind,
      });
      const visibleDetail =
        projectedNodes.length + authoritative.length + bundleMembers.size;
      if (visibleDetail > HIERARCHY_LIMITS.visibleDetail) {
        return rawFallback(model, {
          code: "visible_detail_bound",
          entity: "presentation",
          detail: `visible node/edge/bundle count ${visibleDetail} exceeds limit ${HIERARCHY_LIMITS.visibleDetail}`,
        });
      }
      continue;
    }
    const key = `${encodeString(
      endpointIdentity(source.kind, source.id),
    )}${encodeString(endpointIdentity(target.kind, target.id))}`;
    const bucket = bundleMembers.get(key);
    if (bucket) {
      if (
        bucket.members.length >=
        HIERARCHY_LIMITS.authoritativeEdgesPerBundle
      ) {
        return rawFallback(model, {
          code: "bundle_member_bound",
          entity: `${source.kind}:${source.id}->${target.kind}:${target.id}`,
          detail: `bundle authoritative member count ${bucket.members.length + 1} exceeds limit ${HIERARCHY_LIMITS.authoritativeEdgesPerBundle}`,
        });
      }
      bucket.members.push(edge.id);
    } else {
      bundleMembers.set(key, { source, target, members: [edge.id] });
      const visibleDetail =
        projectedNodes.length + authoritative.length + bundleMembers.size;
      if (visibleDetail > HIERARCHY_LIMITS.visibleDetail) {
        return rawFallback(model, {
          code: "visible_detail_bound",
          entity: "presentation",
          detail: `visible node/edge/bundle count ${visibleDetail} exceeds limit ${HIERARCHY_LIMITS.visibleDetail}`,
        });
      }
    }
  }

  const bundles: BundleEdge[] = [];
  for (const { source, target, members } of [...bundleMembers.values()].sort(
    (left, right) => {
      const leftKey = `${encodeString(
        endpointIdentity(left.source.kind, left.source.id),
      )}${encodeString(endpointIdentity(left.target.kind, left.target.id))}`;
      const rightKey = `${encodeString(
        endpointIdentity(right.source.kind, right.source.id),
      )}${encodeString(endpointIdentity(right.target.kind, right.target.id))}`;
      return compareCodeUnits(leftKey, rightKey);
    },
  )) {
    const memberEdgeIds = [...new Set(members)].sort(compareCodeUnits);
    if (
      memberEdgeIds.length >
      HIERARCHY_LIMITS.authoritativeEdgesPerBundle
    ) {
      return rawFallback(model, {
        code: "bundle_member_bound",
        entity: `${source.kind}:${source.id}->${target.kind}:${target.id}`,
        detail: `bundle authoritative member count ${memberEdgeIds.length} exceeds limit ${HIERARCHY_LIMITS.authoritativeEdgesPerBundle}`,
      });
    }
    if (memberEdgeIds.length === 1) {
      const edge = edgesById.get(memberEdgeIds[0])!;
      authoritative.push({
        kind: "edge",
        id: edge.id,
        edge,
        sourceId: source.id,
        sourceKind: source.kind,
        targetId: target.id,
        targetKind: target.kind,
      });
      continue;
    }
    bundles.push({
      id: bundleIdentity(
        source.kind,
        source.id,
        target.kind,
        target.id,
        memberEdgeIds,
      ),
      sourceId: source.id,
      sourceKind: source.kind,
      targetId: target.id,
      targetKind: target.kind,
      memberEdgeIds,
    });
  }
  const projectedEdges: ProjectedEdge[] = [
    ...authoritative,
    ...bundles.map((bundle) => ({
      kind: "bundle" as const,
      id: bundle.id,
      bundle,
    })),
  ].sort((left, right) => compareCodeUnits(left.id, right.id));

  const visibleDetail = projectedNodes.length + projectedEdges.length;
  if (visibleDetail > HIERARCHY_LIMITS.visibleDetail) {
    return rawFallback(model, {
      code: "visible_detail_bound",
      entity: "presentation",
      detail: `visible node/edge/bundle count ${visibleDetail} exceeds limit ${HIERARCHY_LIMITS.visibleDetail}`,
    });
  }

  const visibleAuthoritativeNodeIds = new Set(
    visibleNodes.map((node) => node.id),
  );
  const visibleAuthoritativeEdgeIds = new Set(
    authoritative.map((entry) => entry.id),
  );
  return {
    mode: "grouped",
    nodes: projectedNodes,
    edges: projectedEdges,
    bundles,
    visibleNodeCount: visibleNodes.length,
    visibleGroupCount: visibleGroups.length,
    hiddenNodeCount: model.nodes.length - visibleAuthoritativeNodeIds.size,
    hiddenEdgeCount: model.edges.length - visibleAuthoritativeEdgeIds.size,
    diagnostic: null,
  };
}

export function groupContainsSelection(
  group: DisplayGroup,
  selection: AuthoritativeSelection,
): boolean {
  if (selection === null) {
    return false;
  }
  return selection.kind === "node"
    ? group.memberNodeIds.includes(selection.id)
    : group.hiddenEdgeIds.includes(selection.id);
}

export function reconcileCollapsedGroups(
  hierarchy: DisplayHierarchy,
  current: ReadonlySet<string> | null,
): Set<string> {
  if (hierarchy.status !== "valid") {
    return new Set();
  }
  if (current === null) {
    return new Set(
      hierarchy.groups
        .filter((group) => group.collapsedByDefault)
        .map((group) => group.id),
    );
  }
  const validIds = new Set(hierarchy.groups.map((group) => group.id));
  return new Set([...current].filter((id) => validIds.has(id)));
}
