export const isPresentationBundleEdge = (edge) => 'kind' in edge && edge.kind === 'presentation-bundle';
export const sourceHandleId = (port) => `out-${port}`;
export const targetHandleId = (port) => `in-${port}`;
export const edgeIdentity = (edge) => `${edge.source_node_id}:${edge.source_port}->${edge.target_node_id}:${edge.target_port}`;
function sorted(values) {
    return [...values].sort((left, right) => left - right);
}
export function toTopology(graph) {
    const inputs = new Map();
    const outputs = new Map();
    for (const node of graph.nodes) {
        inputs.set(node.id, new Set());
        outputs.set(node.id, new Set());
    }
    const identities = new Set();
    const edges = graph.edges.map((edge) => {
        const id = edgeIdentity(edge);
        if (identities.has(id))
            throw new Error(`duplicate port-aware edge identity: ${id}`);
        identities.add(id);
        outputs.get(edge.source_node_id)?.add(edge.source_port);
        inputs.get(edge.target_node_id)?.add(edge.target_port);
        return { ...edge, id, sourceHandle: sourceHandleId(edge.source_port), targetHandle: targetHandleId(edge.target_port) };
    });
    const nodes = graph.nodes.map((node) => ({
        id: node.id,
        type: node.type,
        configuration: node.node_config ?? {},
        inputPorts: sorted(inputs.get(node.id) ?? new Set()),
        outputPorts: sorted(outputs.get(node.id) ?? new Set()),
    }));
    return { nodes, edges };
}
export function normalizeLayout(positions) {
    if (!positions.length)
        return [];
    const minX = Math.min(...positions.map(({ x }) => x));
    const minY = Math.min(...positions.map(({ y }) => y));
    return positions
        .map(({ id, x, y }) => ({ id, x: Math.round((x - minX) * 1000) / 1000, y: Math.round((y - minY) * 1000) / 1000 }))
        .sort((left, right) => left.id.localeCompare(right.id));
}
export function detectorMergeOffsets(model) {
    return Array.from({ length: 64 }, (_, channel) => model.edges.some((edge) => edge.id === `detector_${channel}:0->merge:${channel + 1}`)).every(Boolean);
}
export const isTopologyMutationAllowed = () => false;
export function nextSelectionIndex(current, count, key) {
    if (count <= 0)
        return -1;
    if (key === 'Home')
        return 0;
    if (key === 'End')
        return count - 1;
    if (key === 'ArrowDown')
        return (current + 1) % count;
    if (key === 'ArrowUp')
        return (current - 1 + count) % count;
    return current;
}
