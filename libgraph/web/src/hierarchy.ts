import type {
  DisplayGraph,
  DisplayGroup,
  DisplayHierarchy,
  GroupLayoutMode,
  HierarchyDiagnostic,
} from "./types";
import { compareCodeUnits } from "./identity";

export const HIERARCHY_LIMITS = Object.freeze({
  totalGroups: 256,
  parentDepth: 12,
  directMembersPerGroup: 2_048,
  totalDirectMemberships: 10_000,
  authoritativeEdgesPerBundle: 10_000,
  layoutItemsPerInvocation: 20_000,
  cumulativeLayoutWork: 100_000,
  visibleDetail: 25_000,
});

const groupFields = new Set([
  "id",
  "label",
  "members",
  "parent",
  "layout",
  "collapsed_by_default",
]);
const layoutModes = new Set<GroupLayoutMode>([
  "layered",
  "grid",
  "fanout",
  "fanin",
]);

const own = (value: object, key: string): boolean =>
  Object.prototype.hasOwnProperty.call(value, key);

function invalid(
  code: string,
  entity: string,
  detail: string,
): DisplayHierarchy {
  return {
    status: "invalid",
    groups: [],
    roots: [],
    nodeDirectGroupIds: {},
    diagnostics: [{ code, entity, detail }],
  };
}

function absent(): DisplayHierarchy {
  return {
    status: "absent",
    groups: [],
    roots: [],
    nodeDirectGroupIds: {},
    diagnostics: [],
  };
}

interface ParsedGroup {
  id: string;
  label: string;
  members: string[];
  parentId: string | null;
  layout: GroupLayoutMode;
  collapsedByDefault: boolean;
}

export function adaptPresentationGroups(model: DisplayGraph): DisplayHierarchy {
  const document = model.document;
  if (!own(document, "presentation")) {
    return absent();
  }
  const candidate = document.presentation;
  if (
    candidate === null ||
    typeof candidate !== "object" ||
    Array.isArray(candidate)
  ) {
    return invalid(
      "invalid_presentation",
      "presentation",
      "presentation must be an object",
    );
  }
  const presentation = candidate as Record<string, unknown>;
  const unknownPresentationField = Object.keys(presentation)
    .sort()
    .find((field) => field !== "groups");
  if (unknownPresentationField) {
    return invalid(
      "unknown_presentation_field",
      `presentation.${unknownPresentationField}`,
      `unknown presentation field ${JSON.stringify(unknownPresentationField)}`,
    );
  }
  if (!own(presentation, "groups")) {
    return absent();
  }
  if (!Array.isArray(presentation.groups)) {
    return invalid(
      "invalid_groups",
      "presentation.groups",
      "presentation.groups must be an array",
    );
  }
  if (presentation.groups.length > HIERARCHY_LIMITS.totalGroups) {
    return invalid(
      "group_count_bound",
      "presentation.groups",
      `group count ${presentation.groups.length} exceeds limit ${HIERARCHY_LIMITS.totalGroups}`,
    );
  }

  const nodeIds = new Set(model.nodes.map((node) => node.id));
  const parsed: ParsedGroup[] = [];
  const ids = new Set<string>();
  let totalMemberships = 0;
  for (let index = 0; index < presentation.groups.length; ++index) {
    const entity = `presentation.groups[${index}]`;
    const value = presentation.groups[index];
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
      return invalid("invalid_group", entity, "group must be an object");
    }
    const raw = value as Record<string, unknown>;
    const unknownField = Object.keys(raw)
      .sort()
      .find((field) => !groupFields.has(field));
    if (unknownField) {
      return invalid(
        "unknown_group_field",
        `${entity}.${unknownField}`,
        `unknown group field ${JSON.stringify(unknownField)}`,
      );
    }
    if (typeof raw.id !== "string" || raw.id.length === 0) {
      return invalid(
        "invalid_group_id",
        `${entity}.id`,
        "group id must be a non-empty string",
      );
    }
    if (ids.has(raw.id)) {
      return invalid(
        "duplicate_group_id",
        `${entity}.id`,
        `group id ${JSON.stringify(raw.id)} occurs more than once`,
      );
    }
    if (nodeIds.has(raw.id)) {
      return invalid(
        "group_node_id_collision",
        `${entity}.id`,
        `group id ${JSON.stringify(raw.id)} collides with a graph node id`,
      );
    }
    ids.add(raw.id);
    if (typeof raw.label !== "string" || raw.label.length === 0) {
      return invalid(
        "invalid_group_label",
        `${entity}.label`,
        `group ${JSON.stringify(raw.id)} label must be a non-empty string`,
      );
    }
    if (!Array.isArray(raw.members) || raw.members.length === 0) {
      return invalid(
        "invalid_group_members",
        `${entity}.members`,
        `group ${JSON.stringify(raw.id)} members must be a non-empty array`,
      );
    }
    if (raw.members.length > HIERARCHY_LIMITS.directMembersPerGroup) {
      return invalid(
        "direct_member_bound",
        `${entity}.members`,
        `group ${JSON.stringify(raw.id)} direct member count ${raw.members.length} exceeds limit ${HIERARCHY_LIMITS.directMembersPerGroup}`,
      );
    }
    totalMemberships += raw.members.length;
    if (totalMemberships > HIERARCHY_LIMITS.totalDirectMemberships) {
      return invalid(
        "total_membership_bound",
        "presentation.groups",
        `total direct membership count ${totalMemberships} exceeds limit ${HIERARCHY_LIMITS.totalDirectMemberships}`,
      );
    }
    const members: string[] = [];
    const memberSet = new Set<string>();
    for (let memberIndex = 0; memberIndex < raw.members.length; ++memberIndex) {
      const member = raw.members[memberIndex];
      const memberEntity = `${entity}.members[${memberIndex}]`;
      if (typeof member !== "string" || member.length === 0) {
        return invalid(
          "invalid_group_member",
          memberEntity,
          `group ${JSON.stringify(raw.id)} member must be a non-empty string`,
        );
      }
      if (memberSet.has(member)) {
        return invalid(
          "duplicate_group_member",
          memberEntity,
          `group ${JSON.stringify(raw.id)} repeats member ${JSON.stringify(member)}`,
        );
      }
      if (!nodeIds.has(member)) {
        return invalid(
          "unknown_group_member",
          memberEntity,
          `group ${JSON.stringify(raw.id)} references unknown node ${JSON.stringify(member)}`,
        );
      }
      memberSet.add(member);
      members.push(member);
    }
    const parentId =
      own(raw, "parent") && raw.parent !== undefined
        ? typeof raw.parent === "string" && raw.parent.length > 0
          ? raw.parent
          : null
        : null;
    if (own(raw, "parent") && parentId === null) {
      return invalid(
        "invalid_group_parent",
        `${entity}.parent`,
        `group ${JSON.stringify(raw.id)} parent must be a non-empty string`,
      );
    }
    if (
      typeof raw.layout !== "string" ||
      !layoutModes.has(raw.layout as GroupLayoutMode)
    ) {
      return invalid(
        "invalid_group_layout",
        `${entity}.layout`,
        `group ${JSON.stringify(raw.id)} layout must be layered, grid, fanout, or fanin`,
      );
    }
    if (typeof raw.collapsed_by_default !== "boolean") {
      return invalid(
        "invalid_collapsed_default",
        `${entity}.collapsed_by_default`,
        `group ${JSON.stringify(raw.id)} collapsed_by_default must be boolean`,
      );
    }
    parsed.push({
      id: raw.id,
      label: raw.label,
      members: members.sort(compareCodeUnits),
      parentId,
      layout: raw.layout as GroupLayoutMode,
      collapsedByDefault: raw.collapsed_by_default,
    });
  }

  const directMembership = new Map<string, string>();
  for (const group of parsed) {
    for (const member of group.members) {
      const previous = directMembership.get(member);
      if (previous) {
        return invalid(
          "overlapping_group_member",
          `presentation.groups.${group.id}.members`,
          `node ${JSON.stringify(member)} belongs directly to both ${JSON.stringify(previous)} and ${JSON.stringify(group.id)}`,
        );
      }
      directMembership.set(member, group.id);
    }
  }

  const parsedById = new Map(parsed.map((group) => [group.id, group]));
  for (const group of parsed) {
    if (group.parentId === group.id) {
      return invalid(
        "self_parent_group",
        `presentation.groups.${group.id}.parent`,
        `group ${JSON.stringify(group.id)} cannot parent itself`,
      );
    }
    if (group.parentId !== null && !parsedById.has(group.parentId)) {
      return invalid(
        "unknown_group_parent",
        `presentation.groups.${group.id}.parent`,
        `group ${JSON.stringify(group.id)} references unknown parent ${JSON.stringify(group.parentId)}`,
      );
    }
  }

  const depthById = new Map<string, number>();
  const visiting = new Set<string>();
  const depthOf = (id: string): number | null => {
    const known = depthById.get(id);
    if (known !== undefined) {
      return known;
    }
    if (visiting.has(id)) {
      return null;
    }
    visiting.add(id);
    const group = parsedById.get(id)!;
    const parentDepth =
      group.parentId === null ? 0 : depthOf(group.parentId);
    if (parentDepth === null) {
      return null;
    }
    const depth = parentDepth + 1;
    visiting.delete(id);
    depthById.set(id, depth);
    return depth;
  };
  for (const id of [...ids].sort(compareCodeUnits)) {
    const depth = depthOf(id);
    if (depth === null) {
      return invalid(
        "group_parent_cycle",
        `presentation.groups.${id}.parent`,
        `group parent forest contains a cycle involving ${JSON.stringify(id)}`,
      );
    }
    if (depth > HIERARCHY_LIMITS.parentDepth) {
      return invalid(
        "parent_depth_bound",
        `presentation.groups.${id}.parent`,
        `group ${JSON.stringify(id)} depth ${depth} exceeds limit ${HIERARCHY_LIMITS.parentDepth}`,
      );
    }
  }

  const childIds = new Map<string, string[]>(
    parsed.map((group) => [group.id, []]),
  );
  for (const group of parsed) {
    if (group.parentId !== null) {
      childIds.get(group.parentId)!.push(group.id);
    }
  }
  for (const children of childIds.values()) {
    children.sort(compareCodeUnits);
  }

  const descendantsById = new Map<string, string[]>();
  const memberNodesById = new Map<string, string[]>();
  const collect = (id: string): { descendants: string[]; members: string[] } => {
    const group = parsedById.get(id)!;
    const descendants: string[] = [];
    const members = new Set(group.members);
    for (const childId of childIds.get(id) ?? []) {
      descendants.push(childId);
      const child = collect(childId);
      descendants.push(...child.descendants);
      for (const member of child.members) {
        members.add(member);
      }
    }
    const result = {
      descendants: [...new Set(descendants)].sort(compareCodeUnits),
      members: [...members].sort(compareCodeUnits),
    };
    descendantsById.set(id, result.descendants);
    memberNodesById.set(id, result.members);
    return result;
  };
  const roots = parsed
    .filter((group) => group.parentId === null)
    .map((group) => group.id)
    .sort(compareCodeUnits);
  for (const root of roots) {
    collect(root);
  }

  const groups: DisplayGroup[] = parsed
    .map((group) => {
      const members = memberNodesById.get(group.id) ?? [];
      const memberSet = new Set(members);
      return {
        id: group.id,
        label: group.label,
        directMemberIds: group.members,
        memberNodeIds: members,
        descendantGroupIds: descendantsById.get(group.id) ?? [],
        parentId: group.parentId,
        childIds: childIds.get(group.id) ?? [],
        depth: depthById.get(group.id) ?? 1,
        layout: group.layout,
        collapsedByDefault: group.collapsedByDefault,
        internalEdgeIds: model.edges
          .filter(
            (edge) =>
              memberSet.has(edge.sourceNodeId) &&
              memberSet.has(edge.targetNodeId),
          )
          .map((edge) => edge.id)
          .sort(compareCodeUnits),
        hiddenEdgeIds: model.edges
          .filter(
            (edge) =>
              memberSet.has(edge.sourceNodeId) ||
              memberSet.has(edge.targetNodeId),
          )
          .map((edge) => edge.id)
          .sort(compareCodeUnits),
      };
    })
    .sort((left, right) => compareCodeUnits(left.id, right.id));

  const nodeDirectGroupIds = Object.fromEntries(
    [...directMembership.entries()].sort(([left], [right]) =>
      compareCodeUnits(left, right),
    ),
  );
  return {
    status: "valid",
    groups,
    roots,
    nodeDirectGroupIds,
    diagnostics: [],
  };
}

export function groupBreadcrumbs(
  hierarchy: DisplayHierarchy,
  groupId: string | null,
): DisplayGroup[] {
  if (groupId === null || hierarchy.status !== "valid") {
    return [];
  }
  const byId = new Map(hierarchy.groups.map((group) => [group.id, group]));
  const result: DisplayGroup[] = [];
  let current = byId.get(groupId);
  while (current) {
    result.push(current);
    current =
      current.parentId === null ? undefined : byId.get(current.parentId);
  }
  return result.reverse();
}

export function hierarchyDiagnosticText(
  diagnostic: HierarchyDiagnostic,
): string {
  return `${diagnostic.code} at ${diagnostic.entity}: ${diagnostic.detail}`;
}
