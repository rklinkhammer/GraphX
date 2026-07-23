import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Background, Controls, Handle, MarkerType, MiniMap, Position, ReactFlow,
  useEdgesState, useNodesState, type Edge, type Node, type NodeProps,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { layoutTopology } from './layout';
import type { TopologyModel, TopologyNode } from './topology';

type GraphXFlowNode = Node<TopologyNode, 'graphx'>;

function GraphXNode({ data, selected }: NodeProps<GraphXFlowNode>) {
  const presentationGroup = data.type === 'FHSSPresentationGroup';
  return (
    <div className={`graph-node${presentationGroup ? ' presentation-group' : ''}${selected ? ' selected' : ''}`} aria-label={`${data.id}, ${data.type}`}>
      {data.inputPorts.map((port, index) => (
        <Handle key={`in-${port}`} id={`in-${port}`} type="target" position={Position.Left} style={{ top: 42 + index * 14 }} />
      ))}
      <strong>{presentationGroup ? String(data.configuration.label) : data.id}</strong>
      <span>{presentationGroup ? '64 structurally recognized GraphX nodes' : data.type}</span>
      <small>inputs: {data.inputPorts.join(', ') || 'none'} · outputs: {data.outputPorts.join(', ') || 'none'}</small>
      {presentationGroup && <small>channelizer outputs 0–63 · merge inputs 1–64</small>}
      {data.outputPorts.map((port, index) => (
        <Handle key={`out-${port}`} id={`out-${port}`} type="source" position={Position.Right} style={{ top: 42 + index * 14 }} />
      ))}
    </div>
  );
}

const nodeTypes = { graphx: GraphXNode };

export function toFlowElements(model: TopologyModel): { nodes: GraphXFlowNode[]; edges: Edge[] } {
  return {
    nodes: model.nodes.map((node, index) => ({ id: node.id, type: 'graphx', data: node, position: { x: (index % 8) * 260, y: Math.floor(index / 8) * 130 }, draggable: true, deletable: false, connectable: false, selectable: true })),
    edges: model.edges.map((edge) => ({ id: edge.id, source: edge.source_node_id, sourceHandle: edge.sourceHandle, target: edge.target_node_id, targetHandle: edge.targetHandle, deletable: false, reconnectable: false, selectable: true, markerEnd: { type: MarkerType.ArrowClosed } })),
  };
}

export interface Selection { kind: 'node' | 'edge'; id: string }

export function GraphView({ model, selection, onSelection, authoritativeCounts }: {
  model: TopologyModel; selection: Selection | null; onSelection: (selection: Selection) => void;
  authoritativeCounts?: { nodes: number; edges: number };
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
          edges={edges.map((edge) => ({ ...edge, selected: selection?.kind === 'edge' && selection.id === edge.id }))}
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
