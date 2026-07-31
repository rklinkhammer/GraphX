import type {
  AdapterDiagnostic,
  DisplayEdge,
  DisplayGraph,
  DisplayNode,
  DisplayPort,
  GraphNodeDocument,
  PortKey,
} from "./types";
import { compareCodeUnits } from "./identity";

const own = (value: object, key: string): boolean =>
  Object.prototype.hasOwnProperty.call(value, key);

const encodeString = (value: string): string => `${value.length}:${value}`;

export function portIdentity(key: PortKey): string {
  return key.kind === "index"
    ? `index:${key.value}`
    : `name:${encodeString(key.value)}`;
}

export function portHandleIdentity(
  direction: "input" | "output",
  key: PortKey,
): string {
  return `${direction}|${portIdentity(key)}`;
}

export function edgeIdentity(
  sourceNodeId: string,
  sourcePort: PortKey,
  targetNodeId: string,
  targetPort: PortKey,
): string {
  return [
    "edge",
    encodeString(sourceNodeId),
    portIdentity(sourcePort),
    encodeString(targetNodeId),
    portIdentity(targetPort),
  ].join("|");
}

export function formatPort(key: PortKey): string {
  return key.kind === "index" ? `index ${key.value}` : `name “${key.value}”`;
}

function parsePort(
  edge: Record<string, unknown>,
  side: "source" | "target",
  entity: string,
  diagnostics: AdapterDiagnostic[],
): PortKey | null {
  const numericField = `${side}_port`;
  const namedField = `${side}_port_name`;
  const hasNumeric = own(edge, numericField);
  const hasNamed = own(edge, namedField);
  if (hasNumeric === hasNamed) {
    diagnostics.push({
      code: "invalid_port",
      entity,
      detail: hasNumeric
        ? `${side} endpoint supplies both numeric and named ports`
        : `${side} endpoint does not supply a port`,
    });
    return null;
  }
  if (hasNumeric) {
    const value = edge[numericField];
    if (
      typeof value !== "number" ||
      !Number.isSafeInteger(value) ||
      value < 0
    ) {
      diagnostics.push({
        code: "invalid_port",
        entity,
        detail: `${numericField} must be a non-negative safe integer`,
      });
      return null;
    }
    return { kind: "index", value };
  }
  const value = edge[namedField];
  if (typeof value !== "string" || value.length === 0) {
    diagnostics.push({
      code: "invalid_port",
      entity,
      detail: `${namedField} must be a non-empty string`,
    });
    return null;
  }
  return { kind: "name", value };
}

function comparePorts(left: DisplayPort, right: DisplayPort): number {
  return compareCodeUnits(portIdentity(left.key), portIdentity(right.key));
}

function addPort(node: DisplayNode, port: DisplayPort): void {
  const ports = port.direction === "input" ? node.inputPorts : node.outputPorts;
  if (!ports.some((candidate) => candidate.id === port.id)) {
    ports.push(port);
    ports.sort(comparePorts);
  }
}

export function adaptGraphDocument(document: unknown): DisplayGraph {
  const diagnostics: AdapterDiagnostic[] = [];
  if (
    document === null ||
    typeof document !== "object" ||
    Array.isArray(document)
  ) {
    return {
      document: {},
      nodes: [],
      edges: [],
      diagnostics: [
        {
          code: "invalid_graph",
          entity: "graph",
          detail: "graph response data must be an object",
        },
      ],
      rawNodes: [],
      rawEdges: [],
    };
  }

  const graph = document as Record<string, unknown>;
  const rawNodes = Array.isArray(graph.nodes) ? graph.nodes : [];
  const rawEdges = Array.isArray(graph.edges) ? graph.edges : [];
  if (!Array.isArray(graph.nodes)) {
    diagnostics.push({
      code: "invalid_graph",
      entity: "nodes",
      detail: "graph nodes must be an array",
    });
  }
  if (!Array.isArray(graph.edges)) {
    diagnostics.push({
      code: "invalid_graph",
      entity: "edges",
      detail: "graph edges must be an array",
    });
  }

  const nodes: DisplayNode[] = [];
  const nodesById = new Map<string, DisplayNode>();
  rawNodes.forEach((candidate, index) => {
    const entity = `nodes[${index}]`;
    if (
      candidate === null ||
      typeof candidate !== "object" ||
      Array.isArray(candidate)
    ) {
      diagnostics.push({
        code: "invalid_node",
        entity,
        detail: "node must be an object",
      });
      return;
    }
    const raw = candidate as Record<string, unknown>;
    if (typeof raw.id !== "string" || raw.id.length === 0) {
      diagnostics.push({
        code: "invalid_node",
        entity,
        detail: "node id must be a non-empty string",
      });
      return;
    }
    if (typeof raw.type !== "string" || raw.type.length === 0) {
      diagnostics.push({
        code: "invalid_node",
        entity,
        detail: `node ${JSON.stringify(raw.id)} type must be a non-empty string`,
      });
      return;
    }
    if (nodesById.has(raw.id)) {
      diagnostics.push({
        code: "duplicate_node_identity",
        entity,
        detail: `node id ${JSON.stringify(raw.id)} occurs more than once`,
      });
      return;
    }
    const node: DisplayNode = {
      id: raw.id,
      graphNodeId: raw.id,
      type: raw.type,
      label: raw.id,
      inputPorts: [],
      outputPorts: [],
      document: raw as GraphNodeDocument,
    };
    nodesById.set(node.id, node);
    nodes.push(node);
  });

  const edges: DisplayEdge[] = [];
  const edgeIds = new Set<string>();
  rawEdges.forEach((candidate, index) => {
    const entity = `edges[${index}]`;
    if (
      candidate === null ||
      typeof candidate !== "object" ||
      Array.isArray(candidate)
    ) {
      diagnostics.push({
        code: "invalid_edge",
        entity,
        detail: "edge must be an object",
      });
      return;
    }
    const raw = candidate as Record<string, unknown>;
    if (
      typeof raw.source_node_id !== "string" ||
      raw.source_node_id.length === 0 ||
      typeof raw.target_node_id !== "string" ||
      raw.target_node_id.length === 0
    ) {
      diagnostics.push({
        code: "invalid_edge",
        entity,
        detail: "edge node endpoints must be non-empty strings",
      });
      return;
    }
    const sourcePort = parsePort(raw, "source", entity, diagnostics);
    const targetPort = parsePort(raw, "target", entity, diagnostics);
    if (!sourcePort || !targetPort) {
      return;
    }
    const sourceNode = nodesById.get(raw.source_node_id);
    const targetNode = nodesById.get(raw.target_node_id);
    if (!sourceNode || !targetNode) {
      diagnostics.push({
        code: "missing_endpoint",
        entity,
        detail: [
          !sourceNode ? `unknown source ${JSON.stringify(raw.source_node_id)}` : "",
          !targetNode ? `unknown target ${JSON.stringify(raw.target_node_id)}` : "",
        ]
          .filter(Boolean)
          .join("; "),
      });
      return;
    }
    const id = edgeIdentity(
      raw.source_node_id,
      sourcePort,
      raw.target_node_id,
      targetPort,
    );
    if (edgeIds.has(id)) {
      diagnostics.push({
        code: "duplicate_edge_identity",
        entity,
        detail:
          "the full authoritative endpoint tuple occurs more than once; " +
          "an authoritative edge identity is required",
      });
      return;
    }
    edgeIds.add(id);
    const sourceHandleId = portHandleIdentity("output", sourcePort);
    const targetHandleId = portHandleIdentity("input", targetPort);
    addPort(sourceNode, {
      id: sourceHandleId,
      key: sourcePort,
      direction: "output",
    });
    addPort(targetNode, {
      id: targetHandleId,
      key: targetPort,
      direction: "input",
    });
    edges.push({
      id,
      sourceNodeId: sourceNode.id,
      sourcePort,
      sourceHandleId,
      targetNodeId: targetNode.id,
      targetPort,
      targetHandleId,
      document: raw,
    });
  });

  nodes.sort((left, right) => compareCodeUnits(left.id, right.id));
  edges.sort((left, right) => compareCodeUnits(left.id, right.id));
  return { document: graph, nodes, edges, diagnostics, rawNodes, rawEdges };
}
