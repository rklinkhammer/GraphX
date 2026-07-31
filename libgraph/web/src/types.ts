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

export type Selection =
  | { kind: "node"; id: string }
  | { kind: "edge"; id: string }
  | null;

export interface NodeCardData extends Record<string, unknown> {
  node: DisplayNode;
  selected: boolean;
  onSelect?: (selection: Selection) => void;
}
