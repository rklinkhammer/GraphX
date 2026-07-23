import ELK from 'elkjs/lib/elk.bundled.js';
import type { TopologyModel } from './topology';
import { normalizeLayout } from './topology';

const elk = new ELK();

export async function layoutTopology(model: TopologyModel): Promise<Map<string, { x: number; y: number }>> {
  const result = await elk.layout({
    id: 'graphx',
    layoutOptions: {
      'elk.algorithm': 'layered',
      'elk.direction': 'RIGHT',
      'elk.layered.considerModelOrder.strategy': 'NODES_AND_EDGES',
      'elk.portConstraints': 'FIXED_ORDER',
      'elk.spacing.nodeNode': '35',
      'elk.layered.spacing.nodeNodeBetweenLayers': '100',
    },
    children: model.nodes.map((node) => ({
      id: node.id, width: 220, height: Math.max(86, 54 + Math.max(node.inputPorts.length, node.outputPorts.length) * 14),
      ports: [
        ...node.inputPorts.map((port) => ({ id: `${node.id}:in-${port}`, layoutOptions: { 'elk.port.side': 'WEST', 'elk.port.index': String(port) } })),
        ...node.outputPorts.map((port) => ({ id: `${node.id}:out-${port}`, layoutOptions: { 'elk.port.side': 'EAST', 'elk.port.index': String(port) } })),
      ],
      layoutOptions: { 'elk.portConstraints': 'FIXED_ORDER' },
    })),
    edges: model.edges.map((edge) => ({ id: edge.id, sources: [`${edge.source_node_id}:${edge.sourceHandle}`], targets: [`${edge.target_node_id}:${edge.targetHandle}`] })),
  });
  const normalized = normalizeLayout((result.children ?? []).map((node) => ({ id: node.id, x: node.x ?? 0, y: node.y ?? 0 })));
  return new Map(normalized.map(({ id, x, y }) => [id, { x, y }]));
}
