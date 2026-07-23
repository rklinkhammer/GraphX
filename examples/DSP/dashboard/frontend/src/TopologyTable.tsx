import { useEffect, useRef } from 'react';
import { nextSelectionIndex, type TopologyModel } from './topology';
import type { Selection } from './GraphView';
import type { DetectorBankGroup } from './fhssPresentation';

export function TopologyTable({ model, selection, onSelection, detectorBank, expanded, authoritativeCounts }: {
  model: TopologyModel; selection: Selection | null; onSelection: (selection: Selection) => void;
  detectorBank?: DetectorBankGroup;
  expanded?: boolean;
  authoritativeCounts?: { nodes: number; edges: number };
}) {
  const selectedRef = useRef<HTMLButtonElement>(null);
  useEffect(() => { selectedRef.current?.focus({ preventScroll: true }); }, [selection]);
  const chooseByKey = (event: React.KeyboardEvent, items: Selection[], current: number) => {
    if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const next = nextSelectionIndex(current, items.length, event.key);
    const item = items[next];
    if (item) onSelection(item);
  };
  const nodes = detectorBank && expanded
    ? model.nodes.flatMap((node) => node.id === detectorBank.id ? detectorBank.members.map((entry) => entry.node) : [node])
    : model.nodes;
  const nodeSelections = nodes.map((node): Selection => ({ kind: 'node', id: node.id }));
  const edgeSelections = model.edges.map((edge): Selection => ({ kind: 'edge', id: edge.id }));
  return (
    <section className="semantic-card" aria-labelledby="semantic-heading">
      <h2 id="semantic-heading">Semantic topology</h2>
      <p>Keyboard-accessible text equivalent synchronized with the canvas and detector grid. Use Up/Down, Home, and End. Grouping is presentation-only.</p>
      {authoritativeCounts && <p>{authoritativeCounts.nodes} authoritative nodes and {authoritativeCounts.edges} authoritative exact-port edges remain available in the advanced raw diagnostic.</p>}
      <details open><summary>{detectorBank ? 'Display nodes' : 'Nodes'} ({nodes.length})</summary>
        <div className="semantic-list" role="listbox" aria-label="GraphX nodes">
          {nodes.map((node, index) => <button key={node.id} type="button" role="option"
            aria-selected={selection?.kind === 'node' && selection.id === node.id}
            ref={selection?.kind === 'node' && selection.id === node.id ? selectedRef : undefined}
            onKeyDown={(event) => chooseByKey(event, nodeSelections, index)}
            onClick={() => onSelection({ kind: 'node', id: node.id })}>
            <strong>{node.id}</strong><span>{node.type}</span><small>inputs {node.inputPorts.join(', ') || 'none'}; outputs {node.outputPorts.join(', ') || 'none'}</small>
          </button>)}
        </div>
      </details>
      <details><summary>{detectorBank ? 'Display edges' : 'Edges'} ({model.edges.length})</summary>
        <div className="semantic-list" role="listbox" aria-label="GraphX exact-port edges">
          {model.edges.map((edge, index) => <button key={edge.id} type="button" role="option"
            aria-selected={selection?.kind === 'edge' && selection.id === edge.id}
            ref={selection?.kind === 'edge' && selection.id === edge.id ? selectedRef : undefined}
            onKeyDown={(event) => chooseByKey(event, edgeSelections, index)}
            onClick={() => onSelection({ kind: 'edge', id: edge.id })}>
            <strong>{edge.id}</strong><span>{edge.source_node_id} port {edge.source_port} → {edge.target_node_id} port {edge.target_port}</span>
          </button>)}
        </div>
      </details>
    </section>
  );
}
