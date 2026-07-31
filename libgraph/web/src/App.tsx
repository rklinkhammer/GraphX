import {
  Background,
  Handle,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useReactFlow,
  type Edge,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type KeyboardEvent as ReactKeyboardEvent,
} from "react";

import { adaptGraphDocument, formatPort } from "./adapter";
import { layoutDisplayGraph } from "./layout";
import type {
  DisplayEdge,
  DisplayGraph,
  DisplayNode,
  NodeCardData,
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
    </article>
  );
}

const nodeTypes = { graphNode: NodeCard };

interface TopologyCanvasProps {
  model: DisplayGraph;
  selection: Selection;
  onSelect: (selection: Selection) => void;
}

function TopologyCanvasBody({
  model,
  selection,
  onSelect,
}: TopologyCanvasProps) {
  const [nodes, setNodes] = useState<Node<NodeCardData>[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [layoutDiagnostic, setLayoutDiagnostic] = useState<string | null>(null);
  const [layoutRevision, setLayoutRevision] = useState(0);
  const { fitView, setViewport, zoomIn, zoomOut } = useReactFlow();

  useEffect(() => {
    let current = true;
    void layoutDisplayGraph(model).then((layout) => {
      if (!current) {
        return;
      }
      setNodes(
        layout.nodes.map((node) => ({
          ...node,
          data: { ...node.data, onSelect },
          selected: selection?.kind === "node" && selection.id === node.id,
        })),
      );
      setEdges(
        layout.edges.map((edge) => ({
          ...edge,
          selected: selection?.kind === "edge" && selection.id === edge.id,
        })),
      );
      setLayoutDiagnostic(layout.diagnostic);
      requestAnimationFrame(() => {
        void fitView({ padding: 0.16, duration: 0 });
      });
    });
    return () => {
      current = false;
    };
  }, [fitView, layoutRevision, model]);

  useEffect(() => {
    setNodes((current) =>
      current.map((node) => ({
        ...node,
        selected: selection?.kind === "node" && selection.id === node.id,
      })),
    );
    setEdges((current) =>
      current.map((edge) => ({
        ...edge,
        selected: selection?.kind === "edge" && selection.id === edge.id,
      })),
    );
  }, [selection]);

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
      onSelect({ kind: "edge", id: edgeId });
    },
    [edges, onSelect],
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
          onClick={() => onSelect(null)}
          disabled={selection === null}
          aria-label="Clear topology selection"
        >
          Clear selection
        </button>
      </div>
      {layoutDiagnostic && (
        <div className="diagnostic-panel" role="alert">
          {layoutDiagnostic}
        </div>
      )}
      <output className="sr-only" data-testid="canvas-edge-count">
        Canvas edge model contains {edges.length} edges
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
          elementsSelectable
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
          onNodeClick={(_, node) => onSelect({ kind: "node", id: node.id })}
          onEdgeClick={(_, edge) => onSelect({ kind: "edge", id: edge.id })}
          onPaneClick={() => onSelect(null)}
          proOptions={{ hideAttribution: true }}
          minZoom={0.05}
          maxZoom={4}
          onlyRenderVisibleElements={false}
        >
          <Background gap={24} size={1} />
        </ReactFlow>
      </div>
    </section>
  );
}

function TopologyCanvas(props: TopologyCanvasProps) {
  return (
    <ReactFlowProvider>
      <TopologyCanvasBody {...props} />
    </ReactFlowProvider>
  );
}

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
  selection,
  onEdit,
}: {
  model: DisplayGraph;
  selection: Selection;
  onEdit: (node: DisplayNode) => void;
}) {
  const node =
    selection?.kind === "node"
      ? model.nodes.find((candidate) => candidate.id === selection.id)
      : undefined;
  const edge =
    selection?.kind === "edge"
      ? model.edges.find((candidate) => candidate.id === selection.id)
      : undefined;

  return (
    <aside className="inspector" aria-labelledby="inspector-heading">
      <h2 id="inspector-heading">Inspector</h2>
      {!node && !edge && <p>Select a node or edge to inspect it.</p>}
      {node && (
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
      {edge && <EdgeInspector edge={edge} />}
    </aside>
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
  const [loadError, setLoadError] = useState<string | null>(null);
  const [selection, setSelection] = useState<Selection>(null);
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
    setModel(next);
    setLoadError(null);
    setSelection((current) => {
      if (current?.kind === "node") {
        return next.nodes.some((node) => node.id === current.id) ? current : null;
      }
      if (current?.kind === "edge") {
        return next.edges.some((edge) => edge.id === current.id) ? current : null;
      }
      return null;
    });
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
                      <TopologyCanvas
                        model={model}
                        selection={selection}
                        onSelect={setSelection}
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
                        selection={selection}
                        search={search}
                        typeFilter={typeFilter}
                        onSelect={setSelection}
                        onEdit={setEditing}
                      />
                    </>
                  )}
                </section>
                <Inspector
                  model={model}
                  selection={selection}
                  onEdit={setEditing}
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
  Diagnostics,
  EdgeInspector,
  Inspector,
  NodeCard,
  NodeTable,
  ParameterEditor,
  TopologyCanvas,
};
