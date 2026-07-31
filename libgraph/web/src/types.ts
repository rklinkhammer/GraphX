export type PortKey =
  | { kind: "index"; value: number }
  | { kind: "name"; value: string };

export interface DisplayPort {
  id: string;
  key: PortKey;
  direction: "input" | "output";
}

export interface GraphNodeDocument {
  id: string;
  type: string;
  node_config?: Record<string, unknown>;
  [key: string]: unknown;
}

export interface DisplayNode {
  id: string;
  graphNodeId: string;
  type: string;
  label: string;
  inputPorts: DisplayPort[];
  outputPorts: DisplayPort[];
  document: GraphNodeDocument;
}

export interface DisplayEdge {
  id: string;
  sourceNodeId: string;
  sourcePort: PortKey;
  sourceHandleId: string;
  targetNodeId: string;
  targetPort: PortKey;
  targetHandleId: string;
  document: Record<string, unknown>;
}

export interface AdapterDiagnostic {
  code:
    | "duplicate_edge_identity"
    | "duplicate_node_identity"
    | "invalid_edge"
    | "invalid_graph"
    | "invalid_node"
    | "invalid_port"
    | "missing_endpoint";
  entity: string;
  detail: string;
}

export interface DisplayGraph {
  document: Record<string, unknown>;
  nodes: DisplayNode[];
  edges: DisplayEdge[];
  diagnostics: AdapterDiagnostic[];
  rawNodes: unknown[];
  rawEdges: unknown[];
}

export type AuthoritativeSelection =
  | { kind: "node"; id: string }
  | { kind: "edge"; id: string }
  | null;

export type Selection = AuthoritativeSelection;

export type GroupLayoutMode = "layered" | "grid" | "fanout" | "fanin";

export interface HierarchyDiagnostic {
  code: string;
  entity: string;
  detail: string;
}

export interface DisplayGroup {
  id: string;
  label: string;
  directMemberIds: string[];
  memberNodeIds: string[];
  descendantGroupIds: string[];
  parentId: string | null;
  childIds: string[];
  depth: number;
  layout: GroupLayoutMode;
  collapsedByDefault: boolean;
  internalEdgeIds: string[];
  hiddenEdgeIds: string[];
}

export interface DisplayHierarchy {
  status: "absent" | "valid" | "invalid";
  groups: DisplayGroup[];
  roots: string[];
  nodeDirectGroupIds: Record<string, string>;
  diagnostics: HierarchyDiagnostic[];
}

export interface PresentationState {
  mode: "grouped" | "raw";
  collapsedGroupIds: ReadonlySet<string>;
  isolatedGroupId: string | null;
}

export type PresentationSelection =
  | { kind: "group"; id: string }
  | { kind: "bundle"; id: string }
  | null;

export interface BundleEdge {
  id: string;
  sourceId: string;
  sourceKind: "node" | "group";
  targetId: string;
  targetKind: "node" | "group";
  memberEdgeIds: string[];
}

export interface ProjectedNode {
  id: string;
  kind: "node" | "group";
  parentId: string | null;
  node?: DisplayNode;
  group?: DisplayGroup;
  collapsed?: boolean;
}

export type ProjectedEdge =
  | {
      kind: "edge";
      id: string;
      edge: DisplayEdge;
      sourceId: string;
      sourceKind: "node" | "group";
      targetId: string;
      targetKind: "node" | "group";
    }
  | { kind: "bundle"; id: string; bundle: BundleEdge };

export interface PresentationProjection {
  mode: "grouped" | "raw";
  nodes: ProjectedNode[];
  edges: ProjectedEdge[];
  bundles: BundleEdge[];
  visibleNodeCount: number;
  visibleGroupCount: number;
  hiddenNodeCount: number;
  hiddenEdgeCount: number;
  diagnostic: HierarchyDiagnostic | null;
}

export interface NodeCardData extends Record<string, unknown> {
  kind: "node";
  node: DisplayNode;
  selected: boolean;
  presentationBoundaryInput: boolean;
  presentationBoundaryOutput: boolean;
  onSelect?: (selection: Selection) => void;
}

export interface GroupCardData extends Record<string, unknown> {
  kind: "group";
  group: DisplayGroup;
  collapsed: boolean;
  containsSelection: boolean;
  presentationBoundaryInput: boolean;
  presentationBoundaryOutput: boolean;
  onSelect?: (selection: PresentationSelection) => void;
  onToggle?: (groupId: string) => void;
  onIsolate?: (groupId: string) => void;
}

export type CanvasNodeData = NodeCardData | GroupCardData;
