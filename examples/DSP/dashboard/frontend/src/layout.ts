import ELK from 'elkjs/lib/elk.bundled.js';
import type { DisplayTopologyModel } from './topology';
import { isPresentationBundleEdge } from './topology';
import { normalizeLayout } from './topology';

const elk = new ELK();

export async function layoutTopology(model: DisplayTopologyModel): Promise<Map<string, { x: number; y: number }>> {
  const parents = new Map(model.nodes.filter(({ parentId }) => !parentId).map((node) => [node.id, node]));
  const parentOf = new Map(model.nodes.filter(({ parentId }) => parentId)
    .map((node) => [node.id, node.parentId!]));
  const rootId = (nodeId: string) => parentOf.get(nodeId) ?? nodeId;
  const rootEdges = new Map<string, { id: string; source: string; target: string }>();
  for (const edge of model.edges) {
    const sourceRoot = rootId(edge.source_node_id);
    const targetRoot = rootId(edge.target_node_id);
    if (sourceRoot === targetRoot) continue;
    const crossesHierarchy = sourceRoot !== edge.source_node_id || targetRoot !== edge.target_node_id;
    const bundled = isPresentationBundleEdge(edge);
    const id = crossesHierarchy || bundled ? `${sourceRoot}->${targetRoot}` : edge.id;
    const source = crossesHierarchy || bundled
      ? sourceRoot : `${edge.source_node_id}:${edge.sourceHandle}`;
    const target = crossesHierarchy || bundled
      ? targetRoot : `${edge.target_node_id}:${edge.targetHandle}`;
    if (!rootEdges.has(id)) rootEdges.set(id, { id, source, target });
  }
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
    children: [...parents.values()].map((node) => ({
      id: node.id,
      width: node.presentationRole === 'group' && node.configuration.expanded ? 1420 : 220,
      height: node.presentationRole === 'group' && node.configuration.expanded
        ? 900 : Math.max(86, 54 + Math.max(node.inputPorts.length, node.outputPorts.length) * 14),
      ports: [
        ...node.inputPorts.map((port) => ({ id: `${node.id}:in-${port}`, layoutOptions: { 'elk.port.side': 'WEST', 'elk.port.index': String(port) } })),
        ...node.outputPorts.map((port) => ({ id: `${node.id}:out-${port}`, layoutOptions: { 'elk.port.side': 'EAST', 'elk.port.index': String(port) } })),
      ],
      layoutOptions: { 'elk.portConstraints': 'FIXED_ORDER' },
    })),
    edges: [...rootEdges.values()].map((edge) => ({
      id: edge.id,
      sources: [edge.source],
      targets: [edge.target],
    })),
  });
  const normalized = normalizeLayout((result.children ?? []).map((node) => ({ id: node.id, x: node.x ?? 0, y: node.y ?? 0 })));
  const positions = new Map(normalized.map(({ id, x, y }) => [id, { x, y }]));
  const children = model.nodes.filter(({ parentId }) => parentId);
  children.forEach((node, index) => {
    positions.set(node.id, {
      x: 45 + (index % 8) * 170,
      y: 90 + Math.floor(index / 8) * 98,
    });
  });
  return positions;
}
