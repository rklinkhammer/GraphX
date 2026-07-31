import type { Edge, Node, XYPosition } from "@xyflow/react";
import ELK from "elkjs/lib/elk.bundled.js";

import type { DisplayGraph, NodeCardData } from "./types";

const elk = new ELK();
const nodeWidth = 260;
type ElkLayoutInput = Parameters<typeof elk.layout>[0];
type ElkLayoutOutput = Awaited<ReturnType<typeof elk.layout>>;
export type LayoutRunner = (
  graph: ElkLayoutInput,
) => Promise<ElkLayoutOutput>;
const runElkLayout: LayoutRunner = (graph) => elk.layout(graph);

function nodeHeight(inputCount: number, outputCount: number): number {
  return Math.max(112, 76 + Math.max(inputCount, outputCount) * 28);
}

function elkPortId(nodeId: string, handleId: string): string {
  return `${nodeId.length}:${nodeId}|${handleId.length}:${handleId}`;
}

function accessiblePort(key: { kind: "index" | "name"; value: number | string }) {
  return `${key.kind} ${key.value}`;
}

function fallbackLayout(model: DisplayGraph): Node<NodeCardData>[] {
  const columns = Math.max(1, Math.ceil(Math.sqrt(model.nodes.length)));
  return model.nodes.map((node, index) => ({
    id: node.id,
    type: "graphNode",
    width: nodeWidth,
    height: nodeHeight(node.inputPorts.length, node.outputPorts.length),
    position: {
      x: (index % columns) * 340,
      y: Math.floor(index / columns) * 220,
    },
    data: {
      kind: "node",
      node,
      selected: false,
      presentationBoundaryInput: false,
      presentationBoundaryOutput: false,
    },
    draggable: false,
    connectable: false,
  }));
}

export interface LayoutResult {
  nodes: Node<NodeCardData>[];
  edges: Edge[];
  diagnostic: string | null;
}

export async function layoutDisplayGraph(
  model: DisplayGraph,
  runLayout: LayoutRunner = runElkLayout,
): Promise<LayoutResult> {
  const edges: Edge[] = model.edges.map((edge) => ({
    id: edge.id,
    source: edge.sourceNodeId,
    sourceHandle: edge.sourceHandleId,
    target: edge.targetNodeId,
    targetHandle: edge.targetHandleId,
    type: "smoothstep",
    data: { edge },
    ariaLabel:
      `Edge from ${edge.sourceNodeId} ${accessiblePort(edge.sourcePort)} ` +
      `to ${edge.targetNodeId} ${accessiblePort(edge.targetPort)}. ` +
      "Press Enter or Space to select.",
  }));
  if (model.nodes.length === 0) {
    return { nodes: [], edges, diagnostic: null };
  }

  const children = model.nodes.map((node) => ({
    id: node.id,
    width: nodeWidth,
    height: nodeHeight(node.inputPorts.length, node.outputPorts.length),
    layoutOptions: {
      "org.eclipse.elk.portConstraints": "FIXED_SIDE",
    },
    ports: [
      ...node.inputPorts.map((port) => ({
        id: elkPortId(node.id, port.id),
        width: 8,
        height: 8,
        layoutOptions: { "org.eclipse.elk.port.side": "WEST" },
      })),
      ...node.outputPorts.map((port) => ({
        id: elkPortId(node.id, port.id),
        width: 8,
        height: 8,
        layoutOptions: { "org.eclipse.elk.port.side": "EAST" },
      })),
    ],
  }));
  const elkEdges = model.edges.map((edge) => ({
    id: edge.id,
    sources: [elkPortId(edge.sourceNodeId, edge.sourceHandleId)],
    targets: [elkPortId(edge.targetNodeId, edge.targetHandleId)],
  }));

  try {
    const result = await runLayout({
      id: "root",
      layoutOptions: {
        "elk.algorithm": "layered",
        "elk.direction": "RIGHT",
        "elk.edgeRouting": "ORTHOGONAL",
        "elk.layered.cycleBreaking.strategy": "GREEDY",
        "elk.layered.nodePlacement.strategy": "NETWORK_SIMPLEX",
        "elk.layered.considerModelOrder.strategy": "NODES_AND_EDGES",
        "elk.spacing.nodeNode": "70",
        "elk.layered.spacing.nodeNodeBetweenLayers": "130",
      },
      children,
      edges: elkEdges,
    });
    const positions = new Map<string, XYPosition>(
      (result.children ?? []).map(
        (node: { id: string; x?: number; y?: number }) => [
          node.id,
          { x: node.x ?? 0, y: node.y ?? 0 },
        ],
      ),
    );
    return {
      nodes: model.nodes.map((node) => ({
        id: node.id,
        type: "graphNode",
        width: nodeWidth,
        height: nodeHeight(node.inputPorts.length, node.outputPorts.length),
        position: positions.get(node.id) ?? { x: 0, y: 0 },
        data: {
          kind: "node",
          node,
          selected: false,
          presentationBoundaryInput: false,
          presentationBoundaryOutput: false,
        },
        draggable: false,
        connectable: false,
      })),
      edges,
      diagnostic: null,
    };
  } catch (error) {
    return {
      nodes: fallbackLayout(model),
      edges,
      diagnostic: `Layered layout failed; showing deterministic grid fallback: ${
        error instanceof Error ? error.message : String(error)
      }`,
    };
  }
}
