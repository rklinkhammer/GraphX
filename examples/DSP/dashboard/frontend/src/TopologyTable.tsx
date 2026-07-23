import { useEffect, useRef } from 'react';
import { nextSelectionIndex, type TopologyModel } from './topology';
import type { Selection } from './GraphView';

export function TopologyTable({ model, selection, onSelection }: {
  model: TopologyModel; selection: Selection | null; onSelection: (selection: Selection) => void;
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
  const nodeSelections = model.nodes.map((node): Selection => ({ kind: 'node', id: node.id }));
  const edgeSelections = model.edges.map((edge): Selection => ({ kind: 'edge', id: edge.id }));
  return (
    <section className="semantic-card" aria-labelledby="semantic-heading">
      <h2 id="semantic-heading">Semantic topology</h2>
      <p>Keyboard-accessible text equivalent synchronized with the canvas. Use Up/Down, Home, and End.</p>
      <details open><summary>Nodes ({model.nodes.length})</summary>
        <div className="semantic-list" role="listbox" aria-label="GraphX nodes">
          {model.nodes.map((node, index) => <button key={node.id} type="button" role="option"
            aria-selected={selection?.kind === 'node' && selection.id === node.id}
            ref={selection?.kind === 'node' && selection.id === node.id ? selectedRef : undefined}
            onKeyDown={(event) => chooseByKey(event, nodeSelections, index)}
            onClick={() => onSelection({ kind: 'node', id: node.id })}>
            <strong>{node.id}</strong><span>{node.type}</span><small>inputs {node.inputPorts.join(', ') || 'none'}; outputs {node.outputPorts.join(', ') || 'none'}</small>
          </button>)}
        </div>
      </details>
      <details><summary>Edges ({model.edges.length})</summary>
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
