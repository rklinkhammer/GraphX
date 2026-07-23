import type { GraphContract, GraphEdgeContract, GraphNodeContract } from './domain';

export interface TopologyNode {
  [key: string]: unknown;
  id: string;
  type: string;
  configuration: Record<string, unknown>;
  inputPorts: number[];
  outputPorts: number[];
}

export interface TopologyEdge extends GraphEdgeContract {
  id: string;
  sourceHandle: string;
  targetHandle: string;
}

export interface TopologyModel {
  nodes: TopologyNode[];
  edges: TopologyEdge[];
}

export const sourceHandleId = (port: number): string => `out-${port}`;
export const targetHandleId = (port: number): string => `in-${port}`;
export const edgeIdentity = (edge: GraphEdgeContract): string =>
  `${edge.source_node_id}:${edge.source_port}->${edge.target_node_id}:${edge.target_port}`;

function sorted(values: Set<number>): number[] {
  return [...values].sort((left, right) => left - right);
}

export function toTopology(graph: GraphContract): TopologyModel {
  const inputs = new Map<string, Set<number>>();
  const outputs = new Map<string, Set<number>>();
  for (const node of graph.nodes) {
    inputs.set(node.id, new Set());
    outputs.set(node.id, new Set());
  }
  const identities = new Set<string>();
  const edges = graph.edges.map((edge): TopologyEdge => {
    const id = edgeIdentity(edge);
    if (identities.has(id)) throw new Error(`duplicate port-aware edge identity: ${id}`);
    identities.add(id);
    outputs.get(edge.source_node_id)?.add(edge.source_port);
    inputs.get(edge.target_node_id)?.add(edge.target_port);
    return { ...edge, id, sourceHandle: sourceHandleId(edge.source_port), targetHandle: targetHandleId(edge.target_port) };
  });
  const nodes = graph.nodes.map((node: GraphNodeContract): TopologyNode => ({
    id: node.id,
    type: node.type,
    configuration: node.node_config ?? {},
    inputPorts: sorted(inputs.get(node.id) ?? new Set()),
    outputPorts: sorted(outputs.get(node.id) ?? new Set()),
  }));
  return { nodes, edges };
}

export interface NormalizedPosition { id: string; x: number; y: number }

export function normalizeLayout(positions: NormalizedPosition[]): NormalizedPosition[] {
  if (!positions.length) return [];
  const minX = Math.min(...positions.map(({ x }) => x));
  const minY = Math.min(...positions.map(({ y }) => y));
  return positions
    .map(({ id, x, y }) => ({ id, x: Math.round((x - minX) * 1000) / 1000, y: Math.round((y - minY) * 1000) / 1000 }))
    .sort((left, right) => left.id.localeCompare(right.id));
}

export function detectorMergeOffsets(model: TopologyModel): boolean {
  return Array.from({ length: 64 }, (_, channel) =>
    model.edges.some((edge) => edge.id === `detector_${channel}:0->merge:${channel + 1}`)).every(Boolean);
}

export const isTopologyMutationAllowed = (): false => false;

export function nextSelectionIndex(current: number, count: number, key: string): number {
  if (count <= 0) return -1;
  if (key === 'Home') return 0;
  if (key === 'End') return count - 1;
  if (key === 'ArrowDown') return (current + 1) % count;
  if (key === 'ArrowUp') return (current - 1 + count) % count;
  return current;
}
