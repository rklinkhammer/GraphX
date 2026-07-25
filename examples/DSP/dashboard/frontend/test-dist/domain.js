export function parseGraphResource(value) {
    if (!value || typeof value !== 'object')
        throw new Error('graph response must be an object');
    const candidate = value;
    if (Object.keys(candidate).some((field) => !['schema', 'owner', 'config_revision', 'etag', 'graph'].includes(field))) {
        throw new Error('graph response contains an unexpected field');
    }
    if (candidate.schema !== 'graphx.dashboard.graph.v1')
        throw new Error('graph response schema is invalid');
    if (typeof candidate.owner !== 'string' || !candidate.owner) {
        throw new Error('graph response owner is invalid');
    }
    if (!Number.isSafeInteger(candidate.config_revision) || Number(candidate.config_revision) < 0)
        throw new Error('graph response config_revision is invalid');
    if (typeof candidate.etag !== 'string' || candidate.etag.length === 0)
        throw new Error('graph response etag is invalid');
    if (!candidate.graph || typeof candidate.graph !== 'object') {
        throw new Error('graph response is missing graph');
    }
    const graph = candidate.graph;
    const graphFields = [
        'name', 'fhss_graph_role', 'canonical_fhss_graph', 'reference_only',
        'description', 'execution_backend', 'backend_fallback_policy',
        'resolver_diagnostics', 'active_frequency_indices', 'preamble_pulses',
        'dashboard_derived', 'nodes', 'edges',
    ];
    if (Object.keys(graph).some((field) => !graphFields.includes(field))) {
        throw new Error('graph response graph contains an unexpected field');
    }
    if (!Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
        throw new Error('graph response requires node and edge arrays');
    }
    if (graph.nodes.length > 256 || graph.edges.length > 512) {
        throw new Error('graph response exceeds bounded topology limits');
    }
    const nodes = graph.nodes.map((node, index) => {
        if (!node || typeof node !== 'object')
            throw new Error(`node ${index} must be an object`);
        const item = node;
        if (Object.keys(item).some((field) => !['id', 'type', 'node_config'].includes(field))) {
            throw new Error(`node ${index} contains an unexpected field`);
        }
        if (typeof item.id !== 'string' || !item.id || typeof item.type !== 'string' || !item.type) {
            throw new Error(`node ${index} requires stable id and type`);
        }
        if (item.node_config !== undefined && (!item.node_config || typeof item.node_config !== 'object' || Array.isArray(item.node_config))) {
            throw new Error(`node ${item.id} has malformed node_config`);
        }
        return { id: item.id, type: item.type, ...(item.node_config ? { node_config: item.node_config } : {}) };
    });
    const ids = new Set(nodes.map((node) => node.id));
    if (ids.size !== nodes.length)
        throw new Error('graph contains duplicate stable node ids');
    const edges = graph.edges.map((edge, index) => {
        if (!edge || typeof edge !== 'object')
            throw new Error(`edge ${index} must be an object`);
        const item = edge;
        if (Object.keys(item).some((field) => !['source_node_id', 'source_port', 'target_node_id', 'target_port'].includes(field))) {
            throw new Error(`edge ${index} contains an unexpected field`);
        }
        const portsValid = Number.isSafeInteger(item.source_port) && Number.isSafeInteger(item.target_port)
            && Number(item.source_port) >= 0 && Number(item.target_port) >= 0;
        if (typeof item.source_node_id !== 'string' || typeof item.target_node_id !== 'string' || !portsValid) {
            throw new Error(`edge ${index} requires endpoints and non-negative integer ports`);
        }
        if (!ids.has(item.source_node_id) || !ids.has(item.target_node_id)) {
            throw new Error(`edge ${index} references an unknown node`);
        }
        return {
            source_node_id: item.source_node_id,
            source_port: Number(item.source_port),
            target_node_id: item.target_node_id,
            target_port: Number(item.target_port),
        };
    });
    const edgeIdentities = new Set(edges.map((edge) => `${edge.source_node_id}:${edge.source_port}->${edge.target_node_id}:${edge.target_port}`));
    if (edgeIdentities.size !== edges.length) {
        throw new Error('graph contains duplicate port-aware edge identities');
    }
    return {
        schema: candidate.schema,
        owner: candidate.owner,
        config_revision: Number(candidate.config_revision),
        etag: candidate.etag,
        graph: { nodes, edges },
    };
}
