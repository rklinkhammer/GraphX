import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Background, Controls, Handle, MarkerType, MiniMap, Position, ReactFlow,
  useEdgesState, useNodesState, type Edge, type Node, type NodeProps,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { layoutTopology } from './layout';
import {
  isPresentationBundleEdge,
  type DisplayTopologyModel,
  type TopologyNode,
} from './topology';
import type { EdgeActivity } from './activity';

type GraphXFlowNode = Node<TopologyNode, 'graphx'>;

function GraphXNode({ data, selected }: NodeProps<GraphXFlowNode>) {
  const presentationGroup = data.type === 'FHSSPresentationGroup';
  const groupMember = data.presentationRole === 'group-member';
  const expandedGroup = presentationGroup && data.configuration.expanded === true;
  return (
    <div className={`graph-node${presentationGroup ? ' presentation-group' : ''}${expandedGroup ? ' expanded-group' : ''}${groupMember ? ' group-member' : ''}${selected ? ' selected' : ''}`} aria-label={`${data.id}, ${data.type}`}>
      {data.inputPorts.map((port, index) => (
        <Handle key={`in-${port}`} id={`in-${port}`} type="target" position={Position.Left} style={{ top: 42 + index * 14 }} />
      ))}
      <strong>{presentationGroup ? String(data.configuration.label) : data.id}</strong>
      <span>{presentationGroup
        ? expandedGroup ? 'Hierarchical parent · 64 GraphX detector children' : 'Collapsed presentation group · 64 GraphX detectors'
        : data.type}</span>
      {!expandedGroup && <small>inputs: {data.inputPorts.join(', ') || 'none'} · outputs: {data.outputPorts.join(', ') || 'none'}</small>}
      {presentationGroup && <small>{expandedGroup
        ? 'Exact edges connect channelizer outputs 0–63 through detector children to merge inputs 1–64'
        : 'Two visual bundles retain 128 authoritative exact-port edge identities'}</small>}
      {data.outputPorts.map((port, index) => (
        <Handle key={`out-${port}`} id={`out-${port}`} type="source" position={Position.Right} style={{ top: 42 + index * 14 }} />
      ))}
    </div>
  );
}

const nodeTypes = { graphx: GraphXNode };

export function toFlowElements(model: DisplayTopologyModel): { nodes: GraphXFlowNode[]; edges: Edge[] } {
  const orderedNodes = [
    ...model.nodes.filter(({ parentId }) => !parentId),
    ...model.nodes.filter(({ parentId }) => parentId),
  ];
  const childOrder = new Map(model.nodes.filter(({ parentId }) => parentId)
    .map((node, index) => [node.id, index]));
  return {
    nodes: orderedNodes.map((node, index) => ({
      id: node.id,
      type: 'graphx',
      data: node,
      parentId: node.parentId,
      extent: node.parentId ? 'parent' as const : undefined,
      position: node.parentId
        ? {
            x: 45 + ((childOrder.get(node.id) ?? 0) % 8) * 170,
            y: 90 + Math.floor((childOrder.get(node.id) ?? 0) / 8) * 98,
          }
        : { x: (index % 8) * 260, y: Math.floor(index / 8) * 130 },
      style: node.presentationRole === 'group' && node.configuration.expanded
        ? { width: 1420, height: 900 } : undefined,
      draggable: true,
      deletable: false,
      connectable: false,
      selectable: true,
    })),
    edges: model.edges.map((edge) => ({
      id: edge.id,
      source: edge.source_node_id,
      sourceHandle: edge.sourceHandle,
      target: edge.target_node_id,
      targetHandle: edge.targetHandle,
      data: isPresentationBundleEdge(edge)
        ? {
            presentation_only: true,
            authoritative_edge_ids: edge.authoritativeEdgeIds,
            mappings: edge.mappings,
          } : undefined,
      label: isPresentationBundleEdge(edge) ? edge.label : undefined,
      className: isPresentationBundleEdge(edge) ? 'presentation-bundle-edge' : undefined,
      deletable: false,
      reconnectable: false,
      selectable: true,
      markerEnd: { type: MarkerType.ArrowClosed },
    })),
  };
}

export interface Selection { kind: 'node' | 'edge'; id: string }

function activityLabel(activity: EdgeActivity | undefined): string {
  if (!activity || activity.availability === 'unavailable') {
    return `activity unavailable: ${activity?.unavailableReason ?? 'no compatible sample'}`;
  }
  return `${activity.messageClass} · ${activity.messages} messages/${activity.intervalMs} ms · ${activity.messagesPerSecond?.toFixed(1)} message/s`;
}

export function GraphView({
  model, selection, onSelection, authoritativeCounts, activity, animatedEdges,
  activitySpeed = 1, motionDisabled = false,
}: {
  model: DisplayTopologyModel; selection: Selection | null; onSelection: (selection: Selection) => void;
  authoritativeCounts?: { nodes: number; edges: number };
  activity?: ReadonlyMap<string, EdgeActivity>;
  animatedEdges?: ReadonlySet<string>;
  activitySpeed?: number;
  motionDisabled?: boolean;
}) {
  const elements = useMemo(() => toFlowElements(model), [model]);
  const initialNodes = elements.nodes;
  const initialEdges = elements.edges;
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges);
  const [layoutStatus, setLayoutStatus] = useState('ELK layout pending');

  const resetLayout = useCallback(async () => {
    setLayoutStatus('Computing deterministic ELK layout…');
    try {
      const positions = await layoutTopology(model);
      setNodes((current) => current.map((node) => ({ ...node, position: positions.get(node.id) ?? node.position })));
      setLayoutStatus('Deterministic ELK layered layout');
    } catch (error) {
      setLayoutStatus(`Layout unavailable: ${String(error)}`);
    }
  }, [model, setNodes]);

  useEffect(() => { setNodes(initialNodes); setEdges(initialEdges); void resetLayout(); }, [initialEdges, initialNodes, resetLayout, setEdges, setNodes]);

  return (
    <section className="topology-card" aria-labelledby="topology-heading">
      <div className="section-heading">
        <div><h2 id="topology-heading">Read-only GraphX topology</h2><p>{authoritativeCounts
          ? `${authoritativeCounts.nodes} authoritative nodes · ${authoritativeCounts.edges} authoritative exact-port edges · ${model.nodes.length} display objects`
          : `${model.nodes.length} nodes · ${model.edges.length} exact-port edges`}</p></div>
        <button type="button" onClick={() => void resetLayout()}>Reset layout</button>
      </div>
      <p className="presentation-note">Dragging changes local presentation only; it never changes GraphX execution. Connection, reconnection, and deletion are disabled.</p>
      <div className="flow-canvas" aria-label="Interactive read-only GraphX topology">
        <ReactFlow
          nodes={nodes.map((node) => ({ ...node, selected: selection?.kind === 'node' && selection.id === node.id }))}
          edges={edges.map((edge) => {
            const edgeActivity = activity?.get(edge.id);
            const available = edgeActivity?.availability === 'available';
            const active = available && (edgeActivity.messages ?? 0) > 0;
            const messageClass = edgeActivity?.messageClass ?? 'unknown/unclassified';
            return {
              ...edge,
              selected: selection?.kind === 'edge' && selection.id === edge.id,
              animated: !motionDisabled && (animatedEdges?.has(edge.id) ?? false),
              label: `${edge.label ? `${String(edge.label)} · ` : ''}${activityLabel(edgeActivity)}`,
              className: [
                edge.className, 'activity-edge', `message-${messageClass.replaceAll('/', '-')}`,
                available ? active ? 'activity-active' : 'activity-idle' : 'activity-unavailable',
              ].filter(Boolean).join(' '),
              style: {
                ...edge.style,
                strokeWidth: available ? Math.min(7, 2 + Math.log2(1 + (edgeActivity.messagesPerSecond ?? 0))) : 2,
                '--activity-duration': `${Math.max(0.3, 1.5 / activitySpeed)}s`,
              } as React.CSSProperties,
            };
          })}
          nodeTypes={nodeTypes} onNodesChange={onNodesChange} onEdgesChange={onEdgesChange}
          nodesConnectable={false} nodesDraggable elementsSelectable
          edgesReconnectable={false} deleteKeyCode={null} onConnect={() => undefined}
          onNodeClick={(_, node) => onSelection({ kind: 'node', id: node.id })}
          onEdgeClick={(_, edge) => onSelection({ kind: 'edge', id: edge.id })}
          fitView minZoom={0.05} maxZoom={2} attributionPosition="bottom-left">
          <Background /><MiniMap pannable zoomable aria-label="Topology minimap" />
          <Controls showInteractive={false} aria-label="Topology pan and zoom controls" />
        </ReactFlow>
      </div>
      <p className="layout-status" role="status">{layoutStatus}</p>
    </section>
  );
}
