import {
  Background,
  Handle,
  MiniMap,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useReactFlow,
  type Edge,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import {
  memo,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type KeyboardEvent as ReactKeyboardEvent,
} from "react";

import { adaptGraphDocument, formatPort } from "./adapter";
import {
  adaptPresentationGroups,
  groupBreadcrumbs,
  hierarchyDiagnosticText,
} from "./hierarchy";
import { layoutPresentationGraph } from "./hierarchyLayout";
import {
  groupContainsSelection,
  projectPresentation,
  reconcileCollapsedGroups,
} from "./presentation";
import type {
  AuthoritativeSelection,
  BundleEdge,
  CanvasNodeData,
  DisplayEdge,
  DisplayGraph,
  DisplayGroup,
  DisplayHierarchy,
  DisplayNode,
  GroupCardData,
  HierarchyDiagnostic,
  NodeCardData,
  PresentationProjection,
  PresentationSelection,
  PresentationState,
  Selection,
} from "./types";

const apiBase = "/api/v1";

interface ApiEnvelope {
  success: boolean;
  data?: unknown;
  message?: string;
}

interface ExecutionState {
  state: string;
  coordinator_revision: number;
  configured_revision: number | null;
  active_revision: number | null;
  graph_generation: number;
  configuration_dirty: boolean;
}

async function responseEnvelope(response: Response): Promise<ApiEnvelope> {
  const body = (await response.json()) as ApiEnvelope;
  if (!response.ok || !body.success) {
    throw new Error(body.message ?? `HTTP ${response.status}`);
  }
  return body;
}

function NodeCard({ data, selected }: NodeProps<Node<NodeCardData>>) {
  const node = data.node;
  return (
    <article
      className={`graph-node-card${selected ? " selected" : ""}`}
      data-testid="graph-node-card"
      aria-label={`Node ${node.id}, type ${node.type}`}
      tabIndex={0}
      onKeyDown={(event) => {
        if (event.currentTarget !== event.target) {
          return;
        }
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          data.onSelect?.({ kind: "node", id: node.id });
        }
      }}
    >
      <header>
        <strong>{node.id}</strong>
        <span>{node.type}</span>
        <button
          type="button"
          className="node-keyboard-select nodrag nopan"
          aria-label={`Select node ${node.id}`}
          onClick={() => data.onSelect?.({ kind: "node", id: node.id })}
        >
          Select
        </button>
      </header>
      <div className="ports">
        <div className="port-column inputs" aria-label="Input ports">
          {node.inputPorts.map((port, index) => (
            <div
              className="port-row"
              key={port.id}
              data-testid="input-port"
            >
              <Handle
                id={port.id}
                type="target"
                position={Position.Left}
                style={{ top: 76 + index * 28 }}
                isConnectable={false}
              />
              <span>{formatPort(port.key)}</span>
            </div>
          ))}
        </div>
        <div className="port-column outputs" aria-label="Output ports">
          {node.outputPorts.map((port, index) => (
            <div
              className="port-row"
              key={port.id}
              data-testid="output-port"
            >
              <span>{formatPort(port.key)}</span>
              <Handle
                id={port.id}
                type="source"
                position={Position.Right}
                style={{ top: 76 + index * 28 }}
                isConnectable={false}
              />
            </div>
          ))}
        </div>
      </div>
      {data.presentationBoundaryInput && (
        <Handle
          id="presentation-boundary-input"
          className="presentation-boundary-handle"
          type="target"
          position={Position.Left}
          isConnectable={false}
        />
      )}
      {data.presentationBoundaryOutput && (
        <Handle
          id="presentation-boundary-output"
          className="presentation-boundary-handle"
          type="source"
          position={Position.Right}
          isConnectable={false}
        />
      )}
    </article>
  );
}

function GroupCard({ data, selected }: NodeProps<Node<GroupCardData>>) {
  const group = data.group;
  return (
    <article
      className={[
        "graph-group-card",
        data.collapsed ? "collapsed" : "expanded",
        selected ? "selected" : "",
        data.containsSelection ? "contains-selection" : "",
      ]
        .filter(Boolean)
        .join(" ")}
      data-testid="graph-group-card"
      aria-label={`Group ${group.label}, ${group.memberNodeIds.length} authoritative members, ${
        data.collapsed ? "collapsed" : "expanded"
      }${data.containsSelection ? ", contains authoritative selection" : ""}`}
      tabIndex={0}
      onKeyDown={(event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          data.onSelect?.({ kind: "group", id: group.id });
        }
      }}
    >
      <header className="group-card-header">
        <div>
          <strong>{group.label}</strong>
          <span>{group.id}</span>
        </div>
        <div className="group-card-counts">
          {group.memberNodeIds.length} members · {group.internalEdgeIds.length} internal
          edges · {group.layout}
        </div>
        <div className="group-card-actions nodrag nopan">
          <button
            type="button"
            aria-label={`${data.collapsed ? "Expand" : "Collapse"} group ${group.id}`}
            onKeyDown={(event) => event.stopPropagation()}
            onClick={(event) => {
              event.stopPropagation();
              data.onToggle?.(group.id);
            }}
          >
            {data.collapsed ? "Expand" : "Collapse"}
          </button>
          <button
            type="button"
            aria-label={`Isolate group ${group.id}`}
            onKeyDown={(event) => event.stopPropagation()}
            onClick={(event) => {
              event.stopPropagation();
              data.onIsolate?.(group.id);
            }}
          >
            Isolate
          </button>
          <button
            type="button"
            aria-label={`Inspect group ${group.id}`}
            onKeyDown={(event) => event.stopPropagation()}
            onClick={(event) => {
              event.stopPropagation();
              data.onSelect?.({ kind: "group", id: group.id });
            }}
          >
            Inspect
          </button>
        </div>
      </header>
      {!data.collapsed && (
        <p className="group-expanded-note">Expanded compound group</p>
      )}
      {data.presentationBoundaryInput && (
        <Handle
          id="presentation-boundary-input"
          className="presentation-boundary-handle"
          type="target"
          position={Position.Left}
          isConnectable={false}
        />
      )}
      {data.presentationBoundaryOutput && (
        <Handle
          id="presentation-boundary-output"
          className="presentation-boundary-handle"
          type="source"
          position={Position.Right}
          isConnectable={false}
        />
      )}
    </article>
  );
}

const nodeTypes = { graphNode: NodeCard, groupNode: GroupCard };

interface TopologyCanvasProps {
  model: DisplayGraph;
  hierarchy: DisplayHierarchy;
  projection: PresentationProjection;
  authoritativeSelection: AuthoritativeSelection;
  presentationSelection: PresentationSelection;
  onAuthoritativeSelect: (selection: AuthoritativeSelection) => void;
  onPresentationSelect: (selection: PresentationSelection) => void;
  onToggleGroup: (groupId: string) => void;
  onIsolateGroup: (groupId: string) => void;
  onClearSelection: () => void;
  onLayoutFallback: (diagnostic: HierarchyDiagnostic | null) => void;
}

function TopologyCanvasBody({
  model,
  hierarchy,
  projection,
  authoritativeSelection,
  presentationSelection,
  onAuthoritativeSelect,
  onPresentationSelect,
  onToggleGroup,
  onIsolateGroup,
  onClearSelection,
  onLayoutFallback,
}: TopologyCanvasProps) {
  const [nodes, setNodes] = useState<Node<CanvasNodeData>[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [layoutDiagnostic, setLayoutDiagnostic] = useState<string | null>(null);
  const [layoutRevision, setLayoutRevision] = useState(0);
  const [layoutInvocationCount, setLayoutInvocationCount] = useState(0);
  const authoritativeSelectionRef = useRef(authoritativeSelection);
  const presentationSelectionRef = useRef(presentationSelection);
  authoritativeSelectionRef.current = authoritativeSelection;
  presentationSelectionRef.current = presentationSelection;
  const { fitView, getViewport, setViewport, zoomIn, zoomOut } = useReactFlow();
  const selectedNodeId =
    authoritativeSelection?.kind === "node"
      ? authoritativeSelection.id
      : null;
  const selectedGroupId =
    presentationSelection?.kind === "group"
      ? presentationSelection.id
      : null;
  const containedSelection =
    projection.mode === "grouped" ? authoritativeSelection : null;

  useEffect(() => {
    let current = true;
    setLayoutInvocationCount((count) => count + 1);
    void layoutPresentationGraph(
      model,
      hierarchy,
      projection,
      null,
    ).then((layout) => {
      if (!current) {
        return;
      }
      onLayoutFallback(
        projection.mode === "grouped" && layout.fellBackToRaw
          ? layout.diagnostic
          : null,
      );
      const currentAuthoritativeSelection = authoritativeSelectionRef.current;
      const currentPresentationSelection = presentationSelectionRef.current;
      setNodes(
        layout.nodes.map((node) => ({
          ...node,
          data:
            node.data.kind === "node"
              ? {
                  ...node.data,
                  selected:
                    currentAuthoritativeSelection?.kind === "node" &&
                    currentAuthoritativeSelection.id === node.id,
                  onSelect: onAuthoritativeSelect,
                }
              : {
                  ...node.data,
                  containsSelection: groupContainsSelection(
                    node.data.group,
                    currentAuthoritativeSelection,
                  ),
                  onSelect: onPresentationSelect,
                  onToggle: onToggleGroup,
                  onIsolate: onIsolateGroup,
                },
          selected:
            (node.data.kind === "node" &&
              currentAuthoritativeSelection?.kind === "node" &&
              currentAuthoritativeSelection.id === node.id) ||
            (node.data.kind === "group" &&
              currentPresentationSelection?.kind === "group" &&
              currentPresentationSelection.id === node.id),
        })),
      );
      setEdges(
        layout.edges.map((edge) => ({
          ...edge,
          selected:
            (edge.data?.edge !== undefined &&
              currentAuthoritativeSelection?.kind === "edge" &&
              currentAuthoritativeSelection.id === edge.id) ||
            (edge.data?.bundle !== undefined &&
              currentPresentationSelection?.kind === "bundle" &&
              currentPresentationSelection.id === edge.id),
        })),
      );
      setLayoutDiagnostic(
        layout.diagnostic ? hierarchyDiagnosticText(layout.diagnostic) : null,
      );
      requestAnimationFrame(() => {
        void fitView({ padding: 0.16, duration: 0 });
      });
    });
    return () => {
      current = false;
    };
  }, [
    fitView,
    hierarchy,
    layoutRevision,
    model,
    onAuthoritativeSelect,
    onIsolateGroup,
    onLayoutFallback,
    onPresentationSelect,
    onToggleGroup,
    projection,
  ]);

  useEffect(() => {
    setNodes((current) =>
      current.map((node) => ({
        ...node,
        data:
          node.data.kind === "group"
            ? {
                  ...node.data,
                  containsSelection: groupContainsSelection(
                    node.data.group,
                    containedSelection,
                  ),
              }
            : {
                ...node.data,
                selected:
                  selectedNodeId === node.id,
              },
        selected:
          (node.data.kind === "node" &&
            selectedNodeId === node.id) ||
          (node.data.kind === "group" &&
            selectedGroupId === node.id),
      })),
    );
  }, [containedSelection, selectedGroupId, selectedNodeId]);

  const handleMinimapKeyDown = useCallback(
    (event: ReactKeyboardEvent<HTMLDivElement>) => {
      const viewport = getViewport();
      const panStep = 72;
      if (event.key === "ArrowLeft") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, x: viewport.x + panStep },
          { duration: 0 },
        );
      } else if (event.key === "ArrowRight") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, x: viewport.x - panStep },
          { duration: 0 },
        );
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, y: viewport.y + panStep },
          { duration: 0 },
        );
      } else if (event.key === "ArrowDown") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, y: viewport.y - panStep },
          { duration: 0 },
        );
      } else if (event.key === "+" || event.key === "=") {
        event.preventDefault();
        event.stopPropagation();
        void zoomIn({ duration: 0 });
      } else if (event.key === "-" || event.key === "_") {
        event.preventDefault();
        event.stopPropagation();
        void zoomOut({ duration: 0 });
      }
    },
    [getViewport, setViewport, zoomIn, zoomOut],
  );

  const handleTopologyKeyDown = useCallback(
    (event: ReactKeyboardEvent<HTMLDivElement>) => {
      if (event.key !== "Enter" && event.key !== " ") {
        return;
      }
      const target = event.target;
      if (!(target instanceof Element)) {
        return;
      }
      const edgeElement = target.closest(".react-flow__edge");
      const edgeId = edgeElement?.getAttribute("data-id");
      if (
        !edgeElement ||
        !edgeId ||
        !event.currentTarget.contains(edgeElement) ||
        !edges.some((edge) => edge.id === edgeId)
      ) {
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      const edge = edges.find((candidate) => candidate.id === edgeId);
      if (edge?.data?.bundle !== undefined) {
        onPresentationSelect({ kind: "bundle", id: edgeId });
      } else {
        onPresentationSelect(null);
        onAuthoritativeSelect({ kind: "edge", id: edgeId });
      }
    },
    [edges, onAuthoritativeSelect, onPresentationSelect],
  );

  return (
    <section className="topology-shell" aria-labelledby="topology-heading">
      <div className="topology-toolbar" role="toolbar" aria-label="Topology view controls">
        <button type="button" onClick={() => void zoomIn()} aria-label="Zoom in topology">
          Zoom in
        </button>
        <button type="button" onClick={() => void zoomOut()} aria-label="Zoom out topology">
          Zoom out
        </button>
        <button
          type="button"
          onClick={() => void fitView({ padding: 0.16, duration: 0 })}
          aria-label="Fit topology to view"
        >
          Fit to view
        </button>
        <button
          type="button"
          onClick={() => {
            setLayoutRevision((revision) => revision + 1);
            void setViewport({ x: 0, y: 0, zoom: 1 }, { duration: 0 });
          }}
          aria-label="Reset deterministic topology layout"
        >
          Reset layout
        </button>
        <button
          type="button"
          onClick={onClearSelection}
          disabled={
            authoritativeSelection === null && presentationSelection === null
          }
          aria-label="Clear topology selection"
        >
          Clear selection
        </button>
      </div>
      {layoutDiagnostic && (
        <div className="diagnostic-panel" role="alert">
          <p>{layoutDiagnostic}</p>
          <details>
            <summary>Exact authoritative raw topology</summary>
            <h3>Nodes ({model.rawNodes.length})</h3>
            <pre>{JSON.stringify(model.rawNodes, null, 2)}</pre>
            <h3>Edges ({model.rawEdges.length})</h3>
            <pre>{JSON.stringify(model.rawEdges, null, 2)}</pre>
          </details>
        </div>
      )}
      <output className="sr-only" data-testid="canvas-edge-count">
        Canvas edge model contains {edges.length} edges
      </output>
      <output className="sr-only" data-testid="layout-invocation-count">
        Layout invocation count {layoutInvocationCount}
      </output>
      <div
        className="topology-canvas"
        data-testid="topology-canvas"
        aria-label="Read-only graph topology canvas"
        onKeyDownCapture={handleTopologyKeyDown}
      >
        <ReactFlow
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          nodesDraggable={false}
          nodesConnectable={false}
          edgesReconnectable={false}
          elementsSelectable={false}
          edgesFocusable
          nodesFocusable
          panOnDrag
          zoomOnScroll
          zoomOnPinch
          zoomOnDoubleClick={false}
          deleteKeyCode={null}
          ariaLabelConfig={{
            "node.a11yDescription.default":
              "Press Enter or Space to select this read-only node.",
            "node.a11yDescription.keyboardDisabled":
              "This node is structurally read-only.",
            "edge.a11yDescription.default":
              "Press Enter or Space to select this read-only edge.",
          }}
          onNodeClick={(_, node) => {
            if (node.data.kind === "group") {
              onPresentationSelect({ kind: "group", id: node.id });
            } else {
              onPresentationSelect(null);
              onAuthoritativeSelect({ kind: "node", id: node.id });
            }
          }}
          onEdgeClick={(_, edge) => {
            if (edge.data?.bundle !== undefined) {
              onPresentationSelect({ kind: "bundle", id: edge.id });
            } else {
              onPresentationSelect(null);
              onAuthoritativeSelect({ kind: "edge", id: edge.id });
            }
          }}
          onPaneClick={onClearSelection}
          proOptions={{ hideAttribution: true }}
          minZoom={0.05}
          maxZoom={4}
          onlyRenderVisibleElements={false}
        >
          <Background gap={24} size={1} />
          <div
            className="minimap-keyboard-control"
            data-testid="minimap-keyboard-control"
            role="group"
            aria-label="Keyboard topology minimap"
            aria-describedby="minimap-keyboard-instructions"
            aria-keyshortcuts="ArrowLeft ArrowRight ArrowUp ArrowDown + -"
            tabIndex={0}
            onKeyDown={handleMinimapKeyDown}
          >
            <span id="minimap-keyboard-instructions" className="sr-only">
              Arrow keys pan the topology viewport. Plus and minus zoom the
              topology viewport.
            </span>
            <MiniMap
              ariaLabel="Topology overview minimap"
              pannable
              zoomable
              nodeColor={(node) =>
                node.type === "groupNode" ? "#7253a6" : "#284f9f"
              }
            />
          </div>
        </ReactFlow>
      </div>
    </section>
  );
}

const TopologyCanvas = memo(function TopologyCanvas(
  props: TopologyCanvasProps,
) {
  return (
    <ReactFlowProvider>
      <TopologyCanvasBody {...props} />
    </ReactFlowProvider>
  );
});

interface NodeTableProps {
  nodes: DisplayNode[];
  selection: Selection;
  search: string;
  typeFilter: string;
  onSelect: (selection: Selection) => void;
  onEdit: (node: DisplayNode) => void;
}

function NodeTable({
  nodes,
  selection,
  search,
  typeFilter,
  onSelect,
  onEdit,
}: NodeTableProps) {
  const visibleNodes = useMemo(() => {
    const idNeedle = search.toLocaleLowerCase();
    const typeNeedle = typeFilter.toLocaleLowerCase();
    return nodes.filter(
      (node) =>
        node.id.toLocaleLowerCase().includes(idNeedle) &&
        node.type.toLocaleLowerCase().includes(typeNeedle),
    );
  }, [nodes, search, typeFilter]);

  return (
    <div className="table-container">
      <table id="nodesTable">
        <caption className="sr-only">Authoritative graph nodes</caption>
        <thead>
          <tr>
            <th scope="col">Node ID</th>
            <th scope="col">Type</th>
            <th scope="col">Configuration</th>
            <th scope="col">Actions</th>
          </tr>
        </thead>
        <tbody>
          {visibleNodes.length === 0 ? (
            <tr>
              <td colSpan={4} className="empty-cell">
                No matching nodes
              </td>
            </tr>
          ) : (
            visibleNodes.map((node) => (
              <tr
                key={node.id}
                className={
                  selection?.kind === "node" && selection.id === node.id
                    ? "selected-row"
                    : ""
                }
              >
                <td>
                  <button
                    type="button"
                    className="table-selection"
                    onClick={() => onSelect({ kind: "node", id: node.id })}
                    aria-pressed={
                      selection?.kind === "node" && selection.id === node.id
                    }
                  >
                    {node.id}
                  </button>
                </td>
                <td>{node.type}</td>
                <td>
                  <code>
                    {JSON.stringify(node.document.node_config ?? {}).slice(0, 90)}
                  </code>
                </td>
                <td>
                  <button
                    type="button"
                    className="edit-button"
                    onClick={() => {
                      onSelect({ kind: "node", id: node.id });
                      onEdit(node);
                    }}
                  >
                    Edit parameters
                  </button>
                </td>
              </tr>
            ))
          )}
        </tbody>
      </table>
    </div>
  );
}

function Inspector({
  model,
  hierarchy,
  projection,
  authoritativeSelection,
  presentationSelection,
  onEdit,
  onSelectMemberEdge,
  onToggleGroup,
  onIsolateGroup,
}: {
  model: DisplayGraph;
  hierarchy: DisplayHierarchy;
  projection: PresentationProjection;
  authoritativeSelection: AuthoritativeSelection;
  presentationSelection: PresentationSelection;
  onEdit: (node: DisplayNode) => void;
  onSelectMemberEdge: (edgeId: string) => void;
  onToggleGroup: (groupId: string) => void;
  onIsolateGroup: (groupId: string) => void;
}) {
  const node =
    authoritativeSelection?.kind === "node"
      ? model.nodes.find(
          (candidate) => candidate.id === authoritativeSelection.id,
        )
      : undefined;
  const edge =
    authoritativeSelection?.kind === "edge"
      ? model.edges.find(
          (candidate) => candidate.id === authoritativeSelection.id,
        )
      : undefined;
  const group =
    presentationSelection?.kind === "group"
      ? hierarchy.groups.find(
          (candidate) => candidate.id === presentationSelection.id,
        )
      : undefined;

  return (
    <aside className="inspector" aria-labelledby="inspector-heading">
      <h2 id="inspector-heading">Inspector</h2>
      {!node && !edge && !group && presentationSelection?.kind !== "bundle" && (
        <p>Select a node, edge, group, or bundle to inspect it.</p>
      )}
      {presentationSelection?.kind === "bundle" && (
        <BundleInspector
          bundle={
            projection.bundles.find(
              (candidate) => candidate.id === presentationSelection.id,
            ) ?? null
          }
          model={model}
          onSelectMemberEdge={onSelectMemberEdge}
        />
      )}
      {group && (
        <GroupInspector
          group={group}
          onToggle={() => onToggleGroup(group.id)}
          onIsolate={() => onIsolateGroup(group.id)}
        />
      )}
      {!presentationSelection && node && (
        <div data-testid="node-inspector">
          <dl>
            <dt>Node ID</dt>
            <dd>{node.id}</dd>
            <dt>Type</dt>
            <dd>{node.type}</dd>
            <dt>Input ports</dt>
            <dd>{node.inputPorts.map((port) => formatPort(port.key)).join(", ") || "None"}</dd>
            <dt>Output ports</dt>
            <dd>{node.outputPorts.map((port) => formatPort(port.key)).join(", ") || "None"}</dd>
          </dl>
          <pre>{JSON.stringify(node.document.node_config ?? {}, null, 2)}</pre>
          <button type="button" onClick={() => onEdit(node)}>
            Edit node parameters
          </button>
        </div>
      )}
      {!presentationSelection && edge && <EdgeInspector edge={edge} />}
    </aside>
  );
}

function GroupInspector({
  group,
  onToggle,
  onIsolate,
}: {
  group: DisplayGroup;
  onToggle: () => void;
  onIsolate: () => void;
}) {
  return (
    <section data-testid="group-inspector">
      <h3>{group.label}</h3>
      <dl>
        <dt>Group identity</dt>
        <dd>{group.id}</dd>
        <dt>Layout</dt>
        <dd>{group.layout}</dd>
        <dt>Parent</dt>
        <dd>{group.parentId ?? "Root"}</dd>
        <dt>Direct members</dt>
        <dd>{group.directMemberIds.length}</dd>
        <dt>Transitive members</dt>
        <dd>{group.memberNodeIds.length}</dd>
        <dt>Descendant groups</dt>
        <dd>{group.descendantGroupIds.length}</dd>
        <dt>Internal authoritative edges</dt>
        <dd>{group.internalEdgeIds.length}</dd>
        <dt>All hidden/crossing authoritative edges</dt>
        <dd>{group.hiddenEdgeIds.length}</dd>
      </dl>
      <details>
        <summary>Authoritative member identities</summary>
        <ul>
          {group.memberNodeIds.map((id) => (
            <li key={id}>{id}</li>
          ))}
        </ul>
      </details>
      <details>
        <summary>Internal authoritative edge identities</summary>
        <ul>
          {group.internalEdgeIds.map((id) => (
            <li className="identity" key={id}>
              {id}
            </li>
          ))}
        </ul>
      </details>
      <div className="inspector-actions">
        <button type="button" onClick={onToggle}>
          Toggle collapse
        </button>
        <button type="button" onClick={onIsolate}>
          Isolate group
        </button>
      </div>
    </section>
  );
}

function BundleInspector({
  bundle,
  model,
  onSelectMemberEdge,
}: {
  bundle: BundleEdge | null;
  model: DisplayGraph;
  onSelectMemberEdge: (edgeId: string) => void;
}) {
  if (!bundle) {
    return <p>Selected bundle is no longer visible.</p>;
  }
  const edgesById = new Map(model.edges.map((edge) => [edge.id, edge]));
  return (
    <section data-testid="bundle-inspector">
      <h3>Presentation bundle</h3>
      <p>
        This boundary aggregate is presentation-only. It has no authoritative
        source or target port and is never serialized.
      </p>
      <dl>
        <dt>Bundle identity</dt>
        <dd className="identity">{bundle.id}</dd>
        <dt>Visible endpoints</dt>
        <dd>
          {bundle.sourceKind} {bundle.sourceId} → {bundle.targetKind}{" "}
          {bundle.targetId}
        </dd>
        <dt>Authoritative member count</dt>
        <dd>{bundle.memberEdgeIds.length}</dd>
      </dl>
      <ol className="bundle-members">
        {bundle.memberEdgeIds.map((edgeId) => {
          const edge = edgesById.get(edgeId);
          return (
            <li key={edgeId}>
              {edge ? (
                <>
                  <span>
                    {edge.sourceNodeId} {formatPort(edge.sourcePort)} →{" "}
                    {edge.targetNodeId} {formatPort(edge.targetPort)}
                  </span>
                  <button
                    type="button"
                    aria-label={`Inspect authoritative edge ${edgeId}`}
                    onClick={() => onSelectMemberEdge(edgeId)}
                  >
                    Inspect exact edge
                  </button>
                </>
              ) : (
                <span>Missing authoritative member {edgeId}</span>
              )}
            </li>
          );
        })}
      </ol>
    </section>
  );
}

function EdgeInspector({ edge }: { edge: DisplayEdge }) {
  return (
    <dl data-testid="edge-inspector">
      <dt>Edge identity</dt>
      <dd className="identity">{edge.id}</dd>
      <dt>Source node</dt>
      <dd>{edge.sourceNodeId}</dd>
      <dt>Source port</dt>
      <dd>{formatPort(edge.sourcePort)}</dd>
      <dt>Target node</dt>
      <dd>{edge.targetNodeId}</dd>
      <dt>Target port</dt>
      <dd>{formatPort(edge.targetPort)}</dd>
    </dl>
  );
}

function Diagnostics({ model }: { model: DisplayGraph }) {
  return (
    <section className="diagnostic-panel" role="alert" aria-labelledby="diagnostic-heading">
      <h2 id="diagnostic-heading">Topology cannot be drawn faithfully</h2>
      <p>
        The canvas is withheld because structural input is malformed. Nothing
        was repaired, synthesized, or reconnected.
      </p>
      <ul>
        {model.diagnostics.map((diagnostic, index) => (
          <li key={`${diagnostic.code}-${diagnostic.entity}-${index}`}>
            <strong>{diagnostic.code}</strong> at {diagnostic.entity}:{" "}
            {diagnostic.detail}
          </li>
        ))}
      </ul>
      <details>
        <summary>Semantic raw topology fallback</summary>
        <h3>Nodes ({model.rawNodes.length})</h3>
        <pre>{JSON.stringify(model.rawNodes, null, 2)}</pre>
        <h3>Edges ({model.rawEdges.length})</h3>
        <pre>{JSON.stringify(model.rawEdges, null, 2)}</pre>
      </details>
    </section>
  );
}

function HierarchyDiagnostics({
  model,
  hierarchy,
  projection,
}: {
  model: DisplayGraph;
  hierarchy: DisplayHierarchy;
  projection: PresentationProjection;
}) {
  const diagnostic = projection.diagnostic ?? hierarchy.diagnostics[0] ?? null;
  if (!diagnostic) {
    return null;
  }
  return (
    <section
      className="diagnostic-panel hierarchy-diagnostic"
      role="alert"
      aria-labelledby="hierarchy-diagnostic-heading"
    >
      <h2 id="hierarchy-diagnostic-heading">
        Presentation grouping rejected; Raw topology preserved
      </h2>
      <p>{hierarchyDiagnosticText(diagnostic)}</p>
      <p>
        No partial groups, bundles, layout state, graph PATCH, or execution
        command were applied.
      </p>
      <details>
        <summary>Exact authoritative raw topology</summary>
        <h3>Nodes ({model.rawNodes.length})</h3>
        <pre>{JSON.stringify(model.rawNodes, null, 2)}</pre>
        <h3>Edges ({model.rawEdges.length})</h3>
        <pre>{JSON.stringify(model.rawEdges, null, 2)}</pre>
      </details>
    </section>
  );
}

function HierarchyNavigation({
  hierarchy,
  state,
  selectedGroupId,
  projection,
  onMode,
  onNavigate,
}: {
  hierarchy: DisplayHierarchy;
  state: PresentationState;
  selectedGroupId: string | null;
  projection: PresentationProjection;
  onMode: (mode: "grouped" | "raw") => void;
  onNavigate: (groupId: string | null) => void;
}) {
  const currentGroupId = state.isolatedGroupId ?? selectedGroupId;
  const breadcrumbs = groupBreadcrumbs(hierarchy, currentGroupId);
  const currentGroup =
    state.isolatedGroupId === null
      ? null
      : hierarchy.groups.find(
          (group) => group.id === state.isolatedGroupId,
        ) ?? null;
  return (
    <section className="hierarchy-navigation" aria-label="Hierarchy navigation">
      <div className="topology-mode" role="group" aria-label="Topology mode">
        <button
          type="button"
          aria-pressed={state.mode === "grouped"}
          disabled={hierarchy.status !== "valid"}
          onClick={() => onMode("grouped")}
        >
          Grouped topology
        </button>
        <button
          type="button"
          aria-pressed={state.mode === "raw"}
          onClick={() => onMode("raw")}
        >
          Raw topology
        </button>
      </div>
      {state.mode === "grouped" && projection.mode === "grouped" && (
        <>
          <nav className="breadcrumbs" aria-label="Group breadcrumbs">
            <button
              type="button"
              aria-current={currentGroupId === null ? "page" : undefined}
              onClick={() => onNavigate(null)}
            >
              All topology
            </button>
            {breadcrumbs.map((group) => (
              <span key={group.id}>
                <span aria-hidden="true"> / </span>
                <button
                  type="button"
                  aria-current={
                    currentGroupId === group.id ? "page" : undefined
                  }
                  onClick={() => onNavigate(group.id)}
                >
                  {group.label}
                </button>
              </span>
            ))}
          </nav>
          {state.isolatedGroupId !== null && (
            <div className="isolation-actions">
              <span>Isolated: {currentGroup?.label ?? state.isolatedGroupId}</span>
              <button
                type="button"
                onClick={() => onNavigate(currentGroup?.parentId ?? null)}
              >
                {currentGroup?.parentId ? "Return to parent" : "Return to all topology"}
              </button>
            </div>
          )}
        </>
      )}
      <output className="grouped-counts" data-testid="grouped-counts">
        Visible: {projection.visibleNodeCount} authoritative nodes,{" "}
        {projection.visibleGroupCount} groups, {projection.edges.length}{" "}
        edges/bundles ({projection.bundles.length} bundles); hidden:{" "}
        {projection.hiddenNodeCount} nodes, {projection.hiddenEdgeCount} edges
      </output>
    </section>
  );
}

function ParameterEditor({
  node,
  onClose,
  onSave,
}: {
  node: DisplayNode;
  onClose: () => void;
  onSave: (config: Record<string, unknown>) => Promise<void>;
}) {
  const [text, setText] = useState(
    JSON.stringify(node.document.node_config ?? {}, null, 2),
  );
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const textarea = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    textarea.current?.focus();
  }, []);

  return (
    <div className="modal-backdrop" onMouseDown={(event) => {
      if (event.currentTarget === event.target) {
        onClose();
      }
    }}>
      <section
        className="modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby="editor-heading"
      >
        <h2 id="editor-heading">Edit node parameters</h2>
        <dl>
          <dt>Node ID</dt>
          <dd>{node.id}</dd>
          <dt>Node type</dt>
          <dd>{node.type}</dd>
        </dl>
        <label htmlFor="node-config">Node configuration (JSON object)</label>
        <textarea
          ref={textarea}
          id="node-config"
          value={text}
          onChange={(event) => setText(event.target.value)}
          rows={12}
        />
        {error && <div role="alert">{error}</div>}
        <div className="modal-actions">
          <button
            type="button"
            disabled={saving}
            onClick={() => {
              try {
                const parsed = JSON.parse(text) as unknown;
                if (
                  parsed === null ||
                  typeof parsed !== "object" ||
                  Array.isArray(parsed)
                ) {
                  throw new Error("node configuration must be a JSON object");
                }
                setSaving(true);
                setError(null);
                void onSave(parsed as Record<string, unknown>)
                  .catch((saveError: unknown) => {
                    setError(
                      saveError instanceof Error
                        ? saveError.message
                        : String(saveError),
                    );
                  })
                  .finally(() => setSaving(false));
              } catch (parseError) {
                setError(
                  parseError instanceof Error
                    ? parseError.message
                    : String(parseError),
                );
              }
            }}
          >
            Save parameters
          </button>
          <button type="button" onClick={onClose}>
            Cancel
          </button>
        </div>
      </section>
    </div>
  );
}

export default function App() {
  const [model, setModel] = useState<DisplayGraph | null>(null);
  const [hierarchy, setHierarchy] = useState<DisplayHierarchy>({
    status: "absent",
    groups: [],
    roots: [],
    nodeDirectGroupIds: {},
    diagnostics: [],
  });
  const [loadError, setLoadError] = useState<string | null>(null);
  const [authoritativeSelection, setAuthoritativeSelection] =
    useState<AuthoritativeSelection>(null);
  const [presentationSelection, setPresentationSelection] =
    useState<PresentationSelection>(null);
  const [presentationMode, setPresentationMode] = useState<"grouped" | "raw">(
    "raw",
  );
  const [collapsedGroupIds, setCollapsedGroupIds] = useState<Set<string>>(
    new Set(),
  );
  const [isolatedGroupId, setIsolatedGroupId] = useState<string | null>(null);
  const [layoutFallbackDiagnostic, setLayoutFallbackDiagnostic] =
    useState<HierarchyDiagnostic | null>(null);
  const hierarchyInitialized = useRef(false);
  const [view, setView] = useState<"topology" | "nodes">("topology");
  const [search, setSearch] = useState("");
  const [typeFilter, setTypeFilter] = useState("");
  const [editing, setEditing] = useState<DisplayNode | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [execution, setExecution] = useState<ExecutionState | null>(null);

  const loadGraph = useCallback(async () => {
    const response = await fetch(`${apiBase}/graph`);
    const envelope = await responseEnvelope(response);
    const next = adaptGraphDocument(envelope.data);
    const nextHierarchy = adaptPresentationGroups(next);
    const firstHierarchyLoad = !hierarchyInitialized.current;
    hierarchyInitialized.current = true;
    setModel(next);
    setHierarchy(nextHierarchy);
    setLoadError(null);
    setCollapsedGroupIds((current) =>
      reconcileCollapsedGroups(
        nextHierarchy,
        firstHierarchyLoad ? null : current,
      ),
    );
    setPresentationMode((current) =>
      nextHierarchy.status === "valid"
        ? firstHierarchyLoad
          ? "grouped"
          : current
        : "raw",
    );
    const validGroupIds = new Set(
      nextHierarchy.groups.map((group) => group.id),
    );
    setIsolatedGroupId((current) =>
      current !== null && validGroupIds.has(current) ? current : null,
    );
    setPresentationSelection((current) =>
      current?.kind === "group" && validGroupIds.has(current.id)
        ? current
        : current?.kind === "bundle"
          ? current
          : null,
    );
    setAuthoritativeSelection((current) => {
      if (current?.kind === "node") {
        return next.nodes.some((node) => node.id === current.id) ? current : null;
      }
      if (current?.kind === "edge") {
        return next.edges.some((edge) => edge.id === current.id) ? current : null;
      }
      return null;
    });
  }, []);

  const presentationState = useMemo<PresentationState>(
    () => ({
      mode: presentationMode,
      collapsedGroupIds,
      isolatedGroupId,
    }),
    [collapsedGroupIds, isolatedGroupId, presentationMode],
  );
  const projection = useMemo(
    () =>
      model
        ? projectPresentation(model, hierarchy, presentationState)
        : null,
    [hierarchy, model, presentationState],
  );
  const effectiveProjection = useMemo<PresentationProjection | null>(() => {
    if (
      !model ||
      !projection ||
      projection.mode === "raw" ||
      layoutFallbackDiagnostic === null
    ) {
      return projection;
    }
    return {
      ...projectPresentation(model, hierarchy, {
        mode: "raw",
        collapsedGroupIds: new Set(),
        isolatedGroupId: null,
      }),
      diagnostic: layoutFallbackDiagnostic,
    };
  }, [hierarchy, layoutFallbackDiagnostic, model, projection]);
  const effectivePresentationState = useMemo<PresentationState>(
    () =>
      effectiveProjection?.mode === "raw"
        ? {
            mode: "raw",
            collapsedGroupIds: new Set<string>(),
            isolatedGroupId: null,
        }
        : presentationState,
    [effectiveProjection?.mode, presentationState],
  );
  const handleLayoutFallback = useCallback(
    (diagnostic: HierarchyDiagnostic | null) => {
      setLayoutFallbackDiagnostic((current) =>
        current?.code === diagnostic?.code &&
        current?.entity === diagnostic?.entity &&
        current?.detail === diagnostic?.detail
          ? current
          : diagnostic,
      );
    },
    [],
  );

  useEffect(() => {
    setLayoutFallbackDiagnostic(null);
  }, [projection]);

  useEffect(() => {
    if (
      (presentationSelection?.kind === "bundle" &&
        !effectiveProjection?.bundles.some(
          (bundle) => bundle.id === presentationSelection.id,
        )) ||
      (presentationSelection?.kind === "group" &&
        effectiveProjection?.mode === "raw")
    ) {
      setPresentationSelection(null);
    }
  }, [effectiveProjection, presentationSelection]);

  const toggleGroup = useCallback((groupId: string) => {
    setCollapsedGroupIds((current) => {
      const next = new Set(current);
      if (next.has(groupId)) {
        next.delete(groupId);
      } else {
        next.add(groupId);
      }
      return next;
    });
  }, []);

  const isolateGroup = useCallback((groupId: string) => {
    setPresentationMode("grouped");
    setIsolatedGroupId(groupId);
    setPresentationSelection({ kind: "group", id: groupId });
  }, []);

  const clearSelection = useCallback(() => {
    setAuthoritativeSelection(null);
    setPresentationSelection(null);
  }, []);

  const updateExecution = useCallback(async () => {
    try {
      const response = await fetch(`${apiBase}/execution/state`);
      const envelope = await responseEnvelope(response);
      setExecution(envelope.data as ExecutionState);
    } catch {
      setExecution(null);
    }
  }, []);

  useEffect(() => {
    void loadGraph().catch((error: unknown) => {
      setLoadError(error instanceof Error ? error.message : String(error));
    });
    void updateExecution();
    const timer = window.setInterval(() => void updateExecution(), 1000);
    return () => window.clearInterval(timer);
  }, [loadGraph, updateExecution]);

  const execute = async (command: string) => {
    try {
      const response = await fetch(`${apiBase}/execution/${command}`, {
        method: "POST",
      });
      const envelope = await responseEnvelope(response);
      setNotice(envelope.message ?? `Command ${command} accepted`);
      await updateExecution();
    } catch (error) {
      setNotice(
        `Command ${command} failed: ${
          error instanceof Error ? error.message : String(error)
        }`,
      );
    }
  };

  const saveNodeConfig = async (
    node: DisplayNode,
    nodeConfig: Record<string, unknown>,
  ) => {
    const response = await fetch(
      `${apiBase}/nodes/${encodeURIComponent(node.id)}`,
      {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ node_config: nodeConfig }),
      },
    );
    await responseEnvelope(response);
    await Promise.all([loadGraph(), updateExecution()]);
    setEditing(null);
    setNotice(`Parameters for ${node.id} updated in memory`);
  };

  return (
    <div className="dashboard">
      <header className="management-header">
        <h1>GraphX Management Dashboard</h1>
        <p>Authoritative topology inspection, parameter editing, and execution control</p>
      </header>
      <main>
        <section className="execution-panel" aria-labelledby="execution-heading">
          <div>
            <h2 id="execution-heading">Execution control</h2>
            <div
              className={`state-indicator state-${(execution?.state ?? "unavailable").toLocaleLowerCase()}`}
              aria-live="polite"
            >
              State: {execution?.state ?? "UNAVAILABLE"}
            </div>
            {execution && (
              <p className="revision-summary" data-testid="revision-summary">
                Coordinator revision {execution.coordinator_revision}; configured{" "}
                {execution.configured_revision ?? "none"}; active{" "}
                {execution.active_revision ?? "none"}; generation{" "}
                {execution.graph_generation}; configuration{" "}
                {execution.configuration_dirty ? "dirty" : "clean"}
              </p>
            )}
          </div>
          <div className="execution-actions" role="toolbar" aria-label="Execution commands">
            {["configure", "init", "start", "run", "stop", "join"].map(
              (command) => (
                <button
                  type="button"
                  key={command}
                  onClick={() => void execute(command)}
                >
                  {command[0].toLocaleUpperCase() + command.slice(1)}
                </button>
              ),
            )}
          </div>
        </section>

        {notice && <div className="notice" role="status">{notice}</div>}

        <nav className="view-tabs" aria-label="Dashboard views">
          <button
            type="button"
            aria-pressed={view === "topology"}
            onClick={() => setView("topology")}
          >
            Topology
          </button>
          <button
            type="button"
            aria-pressed={view === "nodes"}
            onClick={() => setView("nodes")}
          >
            Nodes &amp; parameters
          </button>
        </nav>

        {loadError && (
          <section className="diagnostic-panel" role="alert">
            <h2>Topology fetch failed</h2>
            <p>{loadError}</p>
            <button
              type="button"
              onClick={() =>
                void loadGraph().catch((error: unknown) =>
                  setLoadError(error instanceof Error ? error.message : String(error)),
                )
              }
            >
              Retry graph fetch
            </button>
          </section>
        )}
        {!loadError && !model && (
          <section className="loading-state" aria-live="polite">
            <h2>Loading authoritative topology…</h2>
          </section>
        )}
        {model && (
          <>
            <div className="topology-counts" data-testid="topology-counts">
              Authoritative inventory: {model.rawNodes.length} nodes,{" "}
              {model.rawEdges.length} edges
            </div>
            {projection && model.diagnostics.length === 0 && (
              <HierarchyDiagnostics
                model={model}
                hierarchy={hierarchy}
                projection={projection}
              />
            )}
            {model.diagnostics.length > 0 ? (
              <Diagnostics model={model} />
            ) : model.nodes.length === 0 ? (
              <section className="empty-state">
                <h2>Empty graph</h2>
                <p>The authoritative graph contains no nodes or edges.</p>
              </section>
            ) : (
              <div className="workspace">
                <section className="primary-view">
                  {view === "topology" ? (
                    <>
                      <h2 id="topology-heading">Read-only topology</h2>
                      <p className="structural-note">
                        Layout and navigation are presentation-only. Graph structure
                        cannot be edited here.
                      </p>
                      <HierarchyNavigation
                        hierarchy={hierarchy}
                        state={effectivePresentationState}
                        selectedGroupId={
                          presentationSelection?.kind === "group"
                            ? presentationSelection.id
                            : null
                        }
                        projection={effectiveProjection!}
                        onMode={(mode) => {
                          setPresentationMode(mode);
                          setPresentationSelection(null);
                        }}
                        onNavigate={(groupId) => {
                          setPresentationMode("grouped");
                          setIsolatedGroupId(groupId);
                          setPresentationSelection(
                            groupId === null
                              ? null
                              : { kind: "group", id: groupId },
                          );
                        }}
                      />
                      <TopologyCanvas
                        model={model}
                        hierarchy={hierarchy}
                        projection={effectiveProjection!}
                        authoritativeSelection={authoritativeSelection}
                        presentationSelection={presentationSelection}
                        onAuthoritativeSelect={setAuthoritativeSelection}
                        onPresentationSelect={setPresentationSelection}
                        onToggleGroup={toggleGroup}
                        onIsolateGroup={isolateGroup}
                        onClearSelection={clearSelection}
                        onLayoutFallback={handleLayoutFallback}
                      />
                    </>
                  ) : (
                    <>
                      <h2>Nodes &amp; parameters</h2>
                      <div className="search-filter">
                        <label>
                          Search node ID
                          <input
                            type="search"
                            value={search}
                            onChange={(event) => setSearch(event.target.value)}
                          />
                        </label>
                        <label>
                          Filter node type
                          <input
                            type="search"
                            value={typeFilter}
                            onChange={(event) => setTypeFilter(event.target.value)}
                          />
                        </label>
                      </div>
                      <NodeTable
                        nodes={model.nodes}
                        selection={authoritativeSelection}
                        search={search}
                        typeFilter={typeFilter}
                        onSelect={(selection) => {
                          setPresentationSelection(null);
                          setAuthoritativeSelection(selection);
                        }}
                        onEdit={setEditing}
                      />
                    </>
                  )}
                </section>
                <Inspector
                  model={model}
                  hierarchy={hierarchy}
                  projection={effectiveProjection!}
                  authoritativeSelection={authoritativeSelection}
                  presentationSelection={presentationSelection}
                  onEdit={setEditing}
                  onSelectMemberEdge={(edgeId) => {
                    setPresentationSelection(null);
                    setAuthoritativeSelection({ kind: "edge", id: edgeId });
                  }}
                  onToggleGroup={toggleGroup}
                  onIsolateGroup={isolateGroup}
                />
              </div>
            )}
          </>
        )}
      </main>
      <footer>
        GraphX generic management dashboard · HTTP edits are in-memory only
      </footer>
      {editing && (
        <ParameterEditor
          node={editing}
          onClose={() => setEditing(null)}
          onSave={(config) => saveNodeConfig(editing, config)}
        />
      )}
    </div>
  );
}

export {
  BundleInspector,
  Diagnostics,
  EdgeInspector,
  GroupInspector,
  HierarchyDiagnostics,
  HierarchyNavigation,
  Inspector,
  NodeCard,
  NodeTable,
  ParameterEditor,
  TopologyCanvas,
};
