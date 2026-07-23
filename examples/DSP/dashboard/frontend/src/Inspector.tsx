import type { Selection } from './GraphView';
import { StructuredValue } from './Operations';
import type { TopologyModel } from './topology';

export function Inspector({ model, selection }: { model: TopologyModel; selection: Selection | null }) {
  const node = selection?.kind === 'node' ? model.nodes.find((candidate) => candidate.id === selection.id) : undefined;
  const edge = selection?.kind === 'edge' ? model.edges.find((candidate) => candidate.id === selection.id) : undefined;
  return <aside className="inspector" aria-labelledby="inspector-heading">
    <h2 id="inspector-heading">Selection inspector</h2>
    {!selection && <p>Select a node or exact-port edge from either representation.</p>}
    {node && <><dl><dt>Stable configuration identity</dt><dd>{node.id}</dd><dt>Node type</dt><dd>{node.type}</dd><dt>Input ports</dt><dd>{node.inputPorts.join(', ') || 'none'}</dd><dt>Output ports</dt><dd>{node.outputPorts.join(', ') || 'none'}</dd></dl><details><summary>Configuration metadata</summary><StructuredValue value={node.configuration} /></details></>}
    {edge && <dl><dt>Stable port-aware identity</dt><dd>{edge.id}</dd><dt>Source</dt><dd>{edge.source_node_id}, port {edge.source_port}</dd><dt>Target</dt><dd>{edge.target_node_id}, port {edge.target_port}</dd></dl>}
    <p className="deferred">Runtime metric and diagnostic identity overlays are intentionally deferred to Phase 3.</p>
  </aside>;
}
