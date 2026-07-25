import { useEffect, useRef } from 'react';
import { nextSelectionIndex } from './topology';
import {
  isPresentationBundleEdge,
  type DisplayTopologyModel,
} from './topology';
import type { Selection } from './GraphView';
import type { EdgeActivity } from './activity';

export function TopologyTable({ model, selection, onSelection, authoritativeCounts, activity }: {
  model: DisplayTopologyModel; selection: Selection | null; onSelection: (selection: Selection) => void;
  authoritativeCounts?: { nodes: number; edges: number };
  activity?: ReadonlyMap<string, EdgeActivity>;
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
  const nodes = model.nodes;
  const nodeSelections = nodes.map((node): Selection => ({ kind: 'node', id: node.id }));
  const edgeSelections = model.edges.map((edge): Selection => ({ kind: 'edge', id: edge.id }));
  const containsBundles = model.edges.some(isPresentationBundleEdge);
  const nodeLabel = authoritativeCounts ? 'Display nodes' : 'Nodes';
  const edgeLabel = authoritativeCounts ? 'Display edges' : 'Edges';
  return (
    <section className="semantic-card" aria-labelledby="semantic-heading">
      <h2 id="semantic-heading">Semantic topology</h2>
      <p>Keyboard-accessible text equivalent synchronized with the canvas and detector grid. Use Up/Down, Home, and End. Grouping is presentation-only.</p>
      {authoritativeCounts && <p>{authoritativeCounts.nodes} authoritative nodes and {authoritativeCounts.edges} authoritative exact-port edges remain available in the advanced raw diagnostic.</p>}
      <details open><summary>{nodeLabel} ({nodes.length})</summary>
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
      <details><summary>{edgeLabel} ({model.edges.length})</summary>
        <div className="semantic-list" role="listbox"
          aria-label={containsBundles ? 'GraphX presentation edges' : 'GraphX exact-port edges'}>
          {model.edges.map((edge, index) => <button key={edge.id} type="button" role="option"
            aria-selected={selection?.kind === 'edge' && selection.id === edge.id}
            ref={selection?.kind === 'edge' && selection.id === edge.id ? selectedRef : undefined}
            onKeyDown={(event) => chooseByKey(event, edgeSelections, index)}
            onClick={() => onSelection({ kind: 'edge', id: edge.id })}>
            <strong>{edge.id}</strong><span>{isPresentationBundleEdge(edge)
              ? `${edge.label}; ${edge.authoritativeEdgeIds.length} authoritative mappings`
              : `${edge.source_node_id} port ${edge.source_port} → ${edge.target_node_id} port ${edge.target_port}`}</span>
            <ActivityText activity={activity?.get(edge.id)} />
          </button>)}
        </div>
      </details>
    </section>
  );
}

function ActivityText({ activity }: { activity: EdgeActivity | undefined }) {
  if (!activity || activity.availability === 'unavailable') {
    return <small className="activity-text unavailable">Activity unavailable: {activity?.unavailableReason ?? 'no compatible sample'}.
      Class: {activity?.messageClass ?? 'unknown/unclassified'}.</small>;
  }
  return <small className="activity-text available">Class: {activity.messageClass}. {activity.messages} messages
    over {activity.intervalMs} ms; {activity.messagesPerSecond?.toFixed(1)} message/s.
    {activity.memberCount > 1 && ` ${activity.availableMembers}/${activity.memberCount} authoritative members available.`}</small>;
}
