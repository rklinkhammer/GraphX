import { HIERARCHY_LIMITS } from "../hierarchy";

export function graphWithNodes(count: number): Record<string, unknown> {
  return {
    name: `boundary-${count}`,
    nodes: Array.from({ length: count }, (_, index) => ({
      id: `node-${index}`,
      type: "BoundaryNode",
    })),
    edges: [],
  };
}

export function groupsAtCount(count: number): Record<string, unknown>[] {
  return Array.from({ length: count }, (_, index) => ({
    id: `group-${index}`,
    label: `Group ${index}`,
    members: [`node-${index}`],
    layout: "grid",
    collapsed_by_default: false,
  }));
}

export function graphAtGroupBoundary(delta: -1 | 0 | 1): Record<string, unknown> {
  const count = HIERARCHY_LIMITS.totalGroups + delta;
  return {
    ...graphWithNodes(count),
    presentation: { groups: groupsAtCount(count) },
  };
}

export function nestedGraphAtDepth(depth: number): Record<string, unknown> {
  return {
    ...graphWithNodes(depth),
    presentation: {
      groups: Array.from({ length: depth }, (_, index) => ({
        id: `level-${index}`,
        label: `Level ${index}`,
        members: [`node-${index}`],
        ...(index === 0 ? {} : { parent: `level-${index - 1}` }),
        layout: "grid",
        collapsed_by_default: false,
      })),
    },
  };
}

export function directMembersGraph(count: number): Record<string, unknown> {
  return {
    ...graphWithNodes(count),
    presentation: {
      groups: [
        {
          id: "many-members",
          label: "Many members",
          members: Array.from({ length: count }, (_, index) => `node-${index}`),
          layout: "grid",
          collapsed_by_default: false,
        },
      ],
    },
  };
}

export function totalMembershipGraph(
  count: number,
): Record<string, unknown> {
  const groupSize = Math.min(
    HIERARCHY_LIMITS.directMembersPerGroup,
    Math.ceil(count / 5),
  );
  const groups: Record<string, unknown>[] = [];
  let member = 0;
  while (member < count) {
    const size = Math.min(groupSize, count - member);
    groups.push({
      id: `membership-${groups.length}`,
      label: `Membership ${groups.length}`,
      members: Array.from({ length: size }, (_, offset) => `node-${member + offset}`),
      layout: "grid",
      collapsed_by_default: false,
    });
    member += size;
  }
  return {
    ...graphWithNodes(count),
    presentation: { groups },
  };
}
