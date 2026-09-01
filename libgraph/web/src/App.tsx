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
import {
  graphSignature,
  PRESENTATION_PREFERENCE_KEY,
  readPresentationPreferences,
  removePresentationPreferences,
  writePresentationPreferences,
  type PresentationPreferences,
  type PresentationViewport,
} from "./preferences";
import { SemanticTopology } from "./SemanticTopology";
import {
  COMMAND_HISTORY_LIMIT,
  METRIC_POLL_MS,
  METRIC_STALE_MS,
  aggregateMetricText,
  discoverCommands,
  fetchMetricsSnapshot,
  IgnoredMetricSnapshotError,
  isTerminalOperation,
  isExactAvailableActivity,
  metricText,
  mayAnimateEdge,
  pollOperation,
  extractTopLevelJsonMember,
  prepareGraphExportFromRaw,
  retainOperationHistory,
  submitCommand,
  validateCommandArguments,
  type CommandDescriptor,
  type MetricsSnapshot,
  type OperationRecord,
} from "./runtime";
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
  snapshot?: {
    coordinator_revision: number;
    content_identity: string;
  };
}

interface ExecutionState {
  state: string;
  coordinator_revision: number;
  configured_revision: number | null;
  active_revision: number | null;
  graph_generation: number;
  configuration_dirty: boolean;
}

const emptyRuntimeText = new Map<string, string[]>();
const emptyActiveEdgeIds = new Set<string>();

function deterministicPresentationDefaults(hierarchy: DisplayHierarchy): {
  mode: "grouped" | "raw";
  collapsedGroupIds: Set<string>;
  semanticExpandedGroupIds: Set<string>;
} {
  return {
    mode: hierarchy.status === "valid" ? "grouped" : "raw",
    collapsedGroupIds: new Set(
      hierarchy.groups
        .filter((group) => group.collapsedByDefault)
        .map((group) => group.id),
    ),
    semanticExpandedGroupIds: new Set(
      hierarchy.groups.map((group) => group.id),
    ),
  };
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
    >
      <header>
        <strong>{node.id}</strong>
        <span>{node.type}</span>
        <button
          type="button"
          className="node-keyboard-select nodrag nopan"
          aria-label={`Select node ${node.id}`}
          aria-pressed={selected}
          onClick={(event) => {
            event.stopPropagation();
            data.onSelect?.({ kind: "node", id: node.id });
          }}
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
      {data.runtimeText && data.runtimeText.length > 0 && (
        <ul className="runtime-values" aria-label={`Runtime metrics for node ${node.id}`}>
          {data.runtimeText.slice(0, 4).map((text) => <li key={text}>{text}</li>)}
        </ul>
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
            aria-pressed={selected}
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
      {data.runtimeText && data.runtimeText.length > 0 && (
        <ul className="runtime-values" aria-label={`Runtime metrics for group ${group.id}`}>
          {data.runtimeText.slice(0, 4).map((text) => <li key={text}>{text}</li>)}
        </ul>
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

function synchronizeCanvasEdgeSelection(
  edge: Edge,
  authoritativeSelection: AuthoritativeSelection,
  presentationSelection: PresentationSelection,
): Edge {
  const selected =
    (edge.data?.edge !== undefined &&
      authoritativeSelection?.kind === "edge" &&
      authoritativeSelection.id === edge.id) ||
    (edge.data?.bundle !== undefined &&
      presentationSelection?.kind === "bundle" &&
      presentationSelection.id === edge.id);
  return {
    ...edge,
    selected,
    ariaRole: "button",
    domAttributes: {
      ...edge.domAttributes,
      "aria-pressed": selected,
    },
  };
}

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
  preferredViewport: PresentationViewport | null;
  viewportResetRevision: number;
  reducedMotion: boolean;
  runtimeTextByNode?: ReadonlyMap<string, string[]>;
  runtimeTextByGroup?: ReadonlyMap<string, string[]>;
  runtimeTextByEdge?: ReadonlyMap<string, string[]>;
  runtimeTextByBundle?: ReadonlyMap<string, string[]>;
  activeEdgeIds?: ReadonlySet<string>;
  onViewportChange: (viewport: PresentationViewport) => void;
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
  preferredViewport,
  viewportResetRevision,
  reducedMotion,
  runtimeTextByNode = emptyRuntimeText,
  runtimeTextByGroup = emptyRuntimeText,
  runtimeTextByEdge = emptyRuntimeText,
  runtimeTextByBundle = emptyRuntimeText,
  activeEdgeIds = emptyActiveEdgeIds,
  onViewportChange,
}: TopologyCanvasProps) {
  const operatorMotionDurationMs = reducedMotion ? 0 : 200;
  const [nodes, setNodes] = useState<Node<CanvasNodeData>[]>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [layoutDiagnostic, setLayoutDiagnostic] = useState<string | null>(null);
  const [layoutRevision, setLayoutRevision] = useState(0);
  const [layoutInvocationCount, setLayoutInvocationCount] = useState(0);
  const authoritativeSelectionRef = useRef(authoritativeSelection);
  const presentationSelectionRef = useRef(presentationSelection);
  const preferredViewportRef = useRef(preferredViewport);
  const runtimeTextByNodeRef = useRef(runtimeTextByNode);
  const runtimeTextByGroupRef = useRef(runtimeTextByGroup);
  const runtimeTextByEdgeRef = useRef(runtimeTextByEdge);
  const runtimeTextByBundleRef = useRef(runtimeTextByBundle);
  const activeEdgeIdsRef = useRef(activeEdgeIds);
  const edgeFocusEpochRef = useRef(0);
  const pendingEdgeFocusRef = useRef<{
    edgeId: string;
    epoch: number;
  } | null>(null);
  authoritativeSelectionRef.current = authoritativeSelection;
  presentationSelectionRef.current = presentationSelection;
  preferredViewportRef.current = preferredViewport;
  runtimeTextByNodeRef.current = runtimeTextByNode;
  runtimeTextByGroupRef.current = runtimeTextByGroup;
  runtimeTextByEdgeRef.current = runtimeTextByEdge;
  runtimeTextByBundleRef.current = runtimeTextByBundle;
  activeEdgeIdsRef.current = activeEdgeIds;
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
  const selectAuthoritative = useCallback(
    (selection: AuthoritativeSelection) => {
      onPresentationSelect(null);
      onAuthoritativeSelect(selection);
    },
    [onAuthoritativeSelect, onPresentationSelect],
  );
  const cancelPendingEdgeFocus = useCallback(() => {
    edgeFocusEpochRef.current += 1;
    pendingEdgeFocusRef.current = null;
  }, []);

  useEffect(() => {
    const handleFocusIn = (event: FocusEvent) => {
      const pending = pendingEdgeFocusRef.current;
      const target = event.target;
      if (pending === null || !(target instanceof Element)) return;
      const focusedEdgeId = target
        .closest(".topology-canvas .react-flow__edge")
        ?.getAttribute("data-id");
      if (focusedEdgeId !== pending.edgeId) {
        cancelPendingEdgeFocus();
      }
    };
    document.addEventListener("focusin", handleFocusIn);
    return () => document.removeEventListener("focusin", handleFocusIn);
  }, [cancelPendingEdgeFocus]);

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
      if (projection.mode === "grouped") {
        onLayoutFallback(layout.fellBackToRaw ? layout.diagnostic : null);
      }
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
                  onSelect: selectAuthoritative,
                  runtimeText: runtimeTextByNodeRef.current.get(node.id) ?? [],
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
                  runtimeText: runtimeTextByGroupRef.current.get(node.id) ?? [],
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
      let animatedEdgeCount = 0;
      setEdges(
        layout.edges.map((edge) => {
          const synchronized = synchronizeCanvasEdgeSelection(
            edge,
            currentAuthoritativeSelection,
            currentPresentationSelection,
          );
          const text = edge.data?.bundle !== undefined
            ? runtimeTextByBundleRef.current.get(edge.id) ?? []
            : runtimeTextByEdgeRef.current.get(edge.id) ?? [];
          const mayAnimate = edge.data?.bundle === undefined && mayAnimateEdge(
            activeEdgeIdsRef.current.has(edge.id), reducedMotion, animatedEdgeCount,
          );
          if (mayAnimate) animatedEdgeCount += 1;
          return {
            ...synchronized,
            animated: mayAnimate,
            label: text[0],
          };
        }),
      );
      setLayoutDiagnostic(
        layout.diagnostic ? hierarchyDiagnosticText(layout.diagnostic) : null,
      );
      requestAnimationFrame(() => {
        if (preferredViewportRef.current) {
          void setViewport(preferredViewportRef.current, { duration: 0 });
        } else {
          void fitView({ padding: 0.16, duration: 0 });
        }
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
    onIsolateGroup,
    onLayoutFallback,
    onPresentationSelect,
    selectAuthoritative,
    onToggleGroup,
    projection,
    setViewport,
    viewportResetRevision,
  ]);

  useEffect(() => {
    setNodes((current) => current.map((node) => ({
      ...node,
      data: {
        ...node.data,
        runtimeText: node.data.kind === "node"
          ? runtimeTextByNode.get(node.id) ?? []
          : runtimeTextByGroup.get(node.id) ?? [],
      },
    })));
    let animated = 0;
    setEdges((current) => current.map((edge) => {
      const text = edge.data?.bundle !== undefined
        ? runtimeTextByBundle.get(edge.id) ?? []
        : runtimeTextByEdge.get(edge.id) ?? [];
      const hasRuntime = text.length > 0;
      const mayAnimate = edge.data?.bundle === undefined && mayAnimateEdge(
        activeEdgeIds.has(edge.id), reducedMotion, animated,
      );
      if (mayAnimate) animated += 1;
      return {
        ...edge,
        animated: mayAnimate,
        label: hasRuntime ? text[0] : undefined,
        style: hasRuntime ? { ...edge.style, strokeWidth: 3 } : edge.style,
      };
    }));
  }, [
    reducedMotion,
    activeEdgeIds,
    runtimeTextByBundle,
    runtimeTextByEdge,
    runtimeTextByGroup,
    runtimeTextByNode,
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
    setEdges((current) =>
      current.map((edge) =>
        synchronizeCanvasEdgeSelection(
          edge,
          authoritativeSelection,
          presentationSelection,
        ),
      ),
    );
  }, [
    authoritativeSelection,
    containedSelection,
    presentationSelection,
    selectedGroupId,
    selectedNodeId,
  ]);

  useEffect(() => {
    const pending = pendingEdgeFocusRef.current;
    if (pending === null) return;
    requestAnimationFrame(() => {
      if (pending.epoch !== edgeFocusEpochRef.current) return;
      requestAnimationFrame(() => {
        if (pending.epoch !== edgeFocusEpochRef.current) return;
        const currentEdge = [...document.querySelectorAll<SVGElement>(
          ".topology-canvas .react-flow__edge",
        )].find((candidate) => candidate.getAttribute("data-id") === pending.edgeId);
        currentEdge?.focus();
        if (pending.epoch === edgeFocusEpochRef.current) {
          pendingEdgeFocusRef.current = null;
        }
      });
    });
  }, [authoritativeSelection, presentationSelection]);

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
        onViewportChange({ ...viewport, x: viewport.x + panStep });
      } else if (event.key === "ArrowRight") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, x: viewport.x - panStep },
          { duration: 0 },
        );
        onViewportChange({ ...viewport, x: viewport.x - panStep });
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, y: viewport.y + panStep },
          { duration: 0 },
        );
        onViewportChange({ ...viewport, y: viewport.y + panStep });
      } else if (event.key === "ArrowDown") {
        event.preventDefault();
        event.stopPropagation();
        void setViewport(
          { ...viewport, y: viewport.y - panStep },
          { duration: 0 },
        );
        onViewportChange({ ...viewport, y: viewport.y - panStep });
      } else if (event.key === "+" || event.key === "=") {
        event.preventDefault();
        event.stopPropagation();
        void zoomIn({ duration: 0 }).then(() => onViewportChange(getViewport()));
      } else if (event.key === "-" || event.key === "_") {
        event.preventDefault();
        event.stopPropagation();
        void zoomOut({ duration: 0 }).then(() => onViewportChange(getViewport()));
      }
    },
    [getViewport, onViewportChange, setViewport, zoomIn, zoomOut],
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
      pendingEdgeFocusRef.current = {
        edgeId,
        epoch: ++edgeFocusEpochRef.current,
      };
      const edge = edges.find((candidate) => candidate.id === edgeId);
      if (edge?.data?.bundle !== undefined) {
        onPresentationSelect({ kind: "bundle", id: edgeId });
      } else {
        selectAuthoritative({ kind: "edge", id: edgeId });
      }
    },
    [edges, onPresentationSelect, selectAuthoritative],
  );

  return (
    <section
      className="topology-shell"
      aria-labelledby="topology-heading"
      data-motion-duration-ms={operatorMotionDurationMs}
      data-reduced-motion={reducedMotion ? "reduce" : "no-preference"}
    >
      <div className="topology-toolbar" role="toolbar" aria-label="Topology view controls">
        <button type="button" onClick={() => void zoomIn({ duration: operatorMotionDurationMs }).then(() => onViewportChange(getViewport()))} aria-label="Zoom in topology">
          Zoom in
        </button>
        <button type="button" onClick={() => void zoomOut({ duration: operatorMotionDurationMs }).then(() => onViewportChange(getViewport()))} aria-label="Zoom out topology">
          Zoom out
        </button>
        <button
          type="button"
          onClick={() => void fitView({ padding: 0.16, duration: operatorMotionDurationMs }).then(() => onViewportChange(getViewport()))}
          aria-label="Fit topology to view"
        >
          Fit to view
        </button>
        <button
          type="button"
          onClick={() => {
            setLayoutRevision((revision) => revision + 1);
            void setViewport({ x: 0, y: 0, zoom: 1 }, { duration: 0 });
            onViewportChange({ x: 0, y: 0, zoom: 1 });
          }}
          aria-label="Reset deterministic topology layout"
        >
          Reset layout
        </button>
        <button
          type="button"
          onClick={() => {
            cancelPendingEdgeFocus();
            onClearSelection();
          }}
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
      <span className="sr-only" aria-hidden="true" data-testid="canvas-edge-count">
        Canvas edge model contains {edges.length} edges
      </span>
      <span className="sr-only" aria-hidden="true" data-testid="layout-invocation-count">
        Layout invocation count {layoutInvocationCount}
      </span>
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
          nodesFocusable={false}
          panOnDrag
          zoomOnScroll
          zoomOnPinch
          zoomOnDoubleClick={false}
          deleteKeyCode={null}
          ariaLabelConfig={{
            "node.a11yDescription.default":
              "Read-only topology node. Use its named controls to select or inspect it.",
            "node.a11yDescription.keyboardDisabled":
              "This node is structurally read-only.",
            "edge.a11yDescription.default":
              "Press Enter or Space to select this read-only edge.",
          }}
          onNodeClick={(_, node) => {
            cancelPendingEdgeFocus();
            if (node.data.kind === "group") {
              onPresentationSelect({ kind: "group", id: node.id });
            } else {
              selectAuthoritative({ kind: "node", id: node.id });
            }
          }}
          onEdgeClick={(_, edge) => {
            cancelPendingEdgeFocus();
            if (edge.data?.bundle !== undefined) {
              onPresentationSelect({ kind: "bundle", id: edge.id });
            } else {
              selectAuthoritative({ kind: "edge", id: edge.id });
            }
          }}
          onPaneClick={() => {
            cancelPendingEdgeFocus();
            onClearSelection();
          }}
          onMoveEnd={(event, viewport) => {
            if (event !== null) {
              onViewportChange(viewport);
            }
          }}
          proOptions={{ hideAttribution: true }}
          minZoom={0.1}
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
  runtimeTextByNode = emptyRuntimeText,
  runtimeTextByEdge = emptyRuntimeText,
  runtimeTextByGroup = emptyRuntimeText,
  runtimeTextByBundle = emptyRuntimeText,
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
  runtimeTextByNode?: ReadonlyMap<string, string[]>;
  runtimeTextByEdge?: ReadonlyMap<string, string[]>;
  runtimeTextByGroup?: ReadonlyMap<string, string[]>;
  runtimeTextByBundle?: ReadonlyMap<string, string[]>;
  onEdit: (node: DisplayNode, invoker: HTMLElement) => void;
  onSelectMemberEdge: (edgeId: string, invoker: HTMLElement) => void;
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
      <h2 id="inspector-heading" tabIndex={-1}>Inspector</h2>
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
          runtimeText={runtimeTextByBundle.get(presentationSelection.id) ?? []}
        />
      )}
      {group && (
        <GroupInspector
          group={group}
          onToggle={() => onToggleGroup(group.id)}
          onIsolate={() => onIsolateGroup(group.id)}
          runtimeText={runtimeTextByGroup.get(group.id) ?? []}
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
          <RuntimeMetricList text={runtimeTextByNode.get(node.id) ?? []} />
          <button type="button" onClick={(event) => onEdit(node, event.currentTarget)}>
            Edit node parameters
          </button>
        </div>
      )}
      {!presentationSelection && edge && (
        <EdgeInspector edge={edge} runtimeText={runtimeTextByEdge.get(edge.id) ?? []} />
      )}
    </aside>
  );
}

function GroupInspector({
  group,
  onToggle,
  onIsolate,
  runtimeText = [],
}: {
  group: DisplayGroup;
  onToggle: () => void;
  onIsolate: () => void;
  runtimeText?: string[];
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
      <RuntimeMetricList text={runtimeText} />
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
  runtimeText = [],
}: {
  bundle: BundleEdge | null;
  model: DisplayGraph;
  onSelectMemberEdge: (edgeId: string, invoker: HTMLElement) => void;
  runtimeText?: string[];
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
      <RuntimeMetricList text={runtimeText} />
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
                    aria-label={`Inspect exact edge: ${edgeId}`}
                    onClick={(event) =>
                      onSelectMemberEdge(edgeId, event.currentTarget)
                    }
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

function focusPersistentInspectorHeading(): void {
  requestAnimationFrame(() => {
    document.getElementById("inspector-heading")?.focus();
  });
}

function presentationSelectionSurvivesRefresh(
  selection: PresentationSelection,
  graph: DisplayGraph,
  hierarchy: DisplayHierarchy,
  state: PresentationState,
): boolean {
  if (selection === null) {
    return true;
  }
  if (selection.kind === "group") {
    return hierarchy.status === "valid" &&
      hierarchy.groups.some((group) => group.id === selection.id);
  }
  if (hierarchy.status !== "valid") {
    return false;
  }
  const groupedState: PresentationState =
    state.mode === "grouped" ? state : { ...state, mode: "grouped" };
  return projectPresentation(graph, hierarchy, groupedState).bundles.some(
    (bundle) => bundle.id === selection.id,
  );
}

function removedSelectionNotice(
  selection: Exclude<AuthoritativeSelection | PresentationSelection, null>,
): string {
  return `Selected ${selection.kind} ${selection.id} was removed by the graph refresh; selection cleared.`;
}

function RuntimeMetricList({ text }: { text: string[] }) {
  return (
    <section className="runtime-inspector" aria-label="Runtime metrics">
      <h4>Runtime metrics</h4>
      {text.length === 0 ? <p>Unavailable: no correlated metric value.</p> : (
        <ul>{text.slice(0, 64).map((value) => <li key={value}>{value}</li>)}</ul>
      )}
    </section>
  );
}

function EdgeInspector({ edge, runtimeText = [] }: { edge: DisplayEdge; runtimeText?: string[] }) {
  return (
    <div data-testid="edge-inspector">
      <dl>
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
      <RuntimeMetricList text={runtimeText} />
    </div>
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
  onNavigate: (groupId: string | null, invoker: HTMLElement) => void;
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
              data-focus-key="all-topology"
              aria-current={currentGroupId === null ? "page" : undefined}
              onClick={(event) => onNavigate(null, event.currentTarget)}
            >
              All topology
            </button>
            {breadcrumbs.map((group) => (
              <span key={group.id}>
                <span aria-hidden="true"> / </span>
                <button
                  type="button"
                  data-focus-group={group.id}
                  aria-current={
                    currentGroupId === group.id ? "page" : undefined
                  }
                  onClick={(event) => onNavigate(group.id, event.currentTarget)}
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
                onClick={(event) =>
                  onNavigate(currentGroup?.parentId ?? null, event.currentTarget)
                }
              >
                {currentGroup?.parentId ? "Return to parent" : "Return to all topology"}
              </button>
            </div>
          )}
        </>
      )}
      <p className="grouped-counts" data-testid="grouped-counts">
        Visible: {projection.visibleNodeCount} authoritative nodes,{" "}
        {projection.visibleGroupCount} groups, {projection.edges.length}{" "}
        edges/bundles ({projection.bundles.length} bundles); hidden:{" "}
        {projection.hiddenNodeCount} nodes, {projection.hiddenEdgeCount} edges
      </p>
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
    const frame = requestAnimationFrame(() => textarea.current?.focus());
    return () => cancelAnimationFrame(frame);
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
        onKeyDown={(event) => {
          if (event.key === "Escape") {
            event.preventDefault();
            onClose();
            return;
          }
          if (event.key !== "Tab") {
            return;
          }
          const controls = [...event.currentTarget.querySelectorAll<HTMLElement>(
            'button:not(:disabled), textarea, input:not(:disabled), [tabindex]:not([tabindex="-1"])',
          )];
          if (controls.length === 0) {
            event.preventDefault();
            return;
          }
          const first = controls[0];
          const last = controls[controls.length - 1];
          if (event.shiftKey && document.activeElement === first) {
            event.preventDefault();
            last.focus();
          } else if (!event.shiftKey && document.activeElement === last) {
            event.preventDefault();
            first.focus();
          }
        }}
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

function browserStorage(): Storage | null {
  try {
    return window.localStorage;
  } catch {
    return null;
  }
}

function useReducedMotion(): boolean {
  const [reduced, setReduced] = useState(false);
  useEffect(() => {
    if (typeof window.matchMedia !== "function") {
      return;
    }
    const query = window.matchMedia("(prefers-reduced-motion: reduce)");
    const update = () => setReduced(query.matches);
    update();
    query.addEventListener?.("change", update);
    return () => query.removeEventListener?.("change", update);
  }, []);
  return reduced;
}

function CommandArgumentControls({
  descriptor,
  values,
  onChange,
}: {
  descriptor: CommandDescriptor | undefined;
  values: Record<string, unknown>;
  onChange: (values: Record<string, unknown>) => void;
}) {
  const properties = descriptor?.arguments.properties;
  if (!descriptor || !descriptor.supported || properties === null ||
      typeof properties !== "object" || Array.isArray(properties)) return null;
  const requiredFields = new Set(
    Array.isArray(descriptor.arguments.required)
      ? descriptor.arguments.required.filter((name): name is string => typeof name === "string")
      : [],
  );
  return (
    <fieldset className="command-arguments">
      <legend>Structured command arguments</legend>
      {Object.entries(properties as Record<string, Record<string, unknown>>).slice(0, 32)
        .map(([name, schema]) => {
          const id = `command-argument-${name}`;
          const required = requiredFields.has(name);
          const label = `${name}${required ? " (required)" : ""}`;
          const update = (value: unknown) => onChange({ ...values, [name]: value });
          if (schema.type === "boolean") return (
            <label key={name} htmlFor={id}>{label}
              <select id={id} required={required} aria-required={required}
                value={typeof values[name] === "boolean" ? String(values[name]) : ""}
                onChange={(event) => {
                  if (event.target.value === "") {
                    const next = { ...values };
                    delete next[name];
                    onChange(next);
                  } else {
                    update(event.target.value === "true");
                  }
                }}>
                <option value="">Select true or false</option>
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
          );
          if (schema.type === "string" && Array.isArray(schema.enum)) {
            const enumValues = schema.enum as string[];
            return (
            <label key={name} htmlFor={id}>{label}
              <select id={id} required={required} aria-required={required}
                value={typeof values[name] === "string"
                  ? String(enumValues.findIndex((value) => value === values[name])) : "unset"}
                onChange={(event) => {
                  if (event.target.value === "unset") {
                    const next = { ...values };
                    delete next[name];
                    onChange(next);
                  } else {
                    update(enumValues[Number(event.target.value)]);
                  }
                }}>
                <option value="unset">Select a value</option>
                {enumValues.slice(0, 64).map((value, index) =>
                  <option key={index} value={String(index)}>
                    {String(value) === "" ? "(empty string)" : String(value)}
                  </option>)}
              </select>
            </label>
            );
          }
          if (schema.type === "string") return (
            <label key={name} htmlFor={id}>{label}
              <input id={id} type="text" maxLength={Number(schema.maxLength)}
                required={required} aria-required={required}
                value={String(values[name] ?? "")}
                onChange={(event) => update(event.target.value)} />
            </label>
          );
          return (
            <label key={name} htmlFor={id}>{label}
              <input id={id} type="number"
                required={required} aria-required={required}
                min={Number(schema.minimum)} max={Number(schema.maximum)}
                step={schema.type === "integer" || schema.type === "unsigned" ? 1 : "any"}
                value={typeof values[name] === "number" ? String(values[name]) : ""}
                onChange={(event) => {
                  if (event.target.value === "") {
                    const next = { ...values };
                    delete next[name];
                    onChange(next);
                  } else {
                    update(Number(event.target.value));
                  }
                }} />
            </label>
          );
        })}
    </fieldset>
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
  const [semanticExpandedGroupIds, setSemanticExpandedGroupIds] = useState<Set<string>>(
    new Set(),
  );
  const [preferredViewport, setPreferredViewport] =
    useState<PresentationViewport | null>(null);
  const [viewportResetRevision, setViewportResetRevision] = useState(0);
  const [preferenceSignature, setPreferenceSignature] = useState<string | null>(null);
  const [preferencesDirty, setPreferencesDirty] = useState(false);
  const [preferencePersistenceDisabled, setPreferencePersistenceDisabled] =
    useState(false);
  const [isolatedGroupId, setIsolatedGroupId] = useState<string | null>(null);
  const [layoutFallbackDiagnostic, setLayoutFallbackDiagnostic] =
    useState<HierarchyDiagnostic | null>(null);
  const [view, setView] = useState<"topology" | "semantic">("topology");
  const [search, setSearch] = useState("");
  const [typeFilter, setTypeFilter] = useState("");
  const [editing, setEditing] = useState<DisplayNode | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [execution, setExecution] = useState<ExecutionState | null>(null);
  const [commands, setCommands] = useState<CommandDescriptor[]>([]);
  const [selectedCommand, setSelectedCommand] = useState("configure");
  const [commandArguments, setCommandArguments] = useState<Record<string, unknown>>({});
  const [commandHistory, setCommandHistory] = useState<OperationRecord[]>([]);
  const [commandSubmissionBusy, setCommandSubmissionBusy] = useState(false);
  const [followedCommands, setFollowedCommands] = useState<Set<string>>(new Set());
  const [graphSnapshot, setGraphSnapshot] = useState<{
    coordinator_revision: number;
    content_identity: string;
  } | null>(null);
  const [metrics, setMetrics] = useState<MetricsSnapshot | null>(null);
  const [metricsPaused, setMetricsPaused] = useState(false);
  const [metricsReceivedAt, setMetricsReceivedAt] = useState<number | null>(null);
  const [metricsBrowserStale, setMetricsBrowserStale] = useState(false);
  const reducedMotion = useReducedMotion();
  const modelRef = useRef<DisplayGraph | null>(null);
  const exportSnapshotRef = useRef<{
    raw_graph: string;
    coordinator_revision: number;
    content_identity: string;
  } | null>(null);
  const hierarchyRef = useRef(hierarchy);
  const signatureRef = useRef<string | null>(null);
  const selectionRef = useRef(authoritativeSelection);
  const presentationSelectionRef = useRef(presentationSelection);
  const presentationStateRef = useRef<PresentationState>({
    mode: presentationMode,
    collapsedGroupIds,
    isolatedGroupId,
  });
  const viewRef = useRef(view);
  const pendingFocusRef = useRef<string | null>(null);
  const presentationFocusEpochRef = useRef(0);
  const pendingPresentationFocusRef = useRef<{
    invoker: HTMLElement | null;
    groupId: string | null;
    retainInvoker: boolean;
    originView: "topology" | "semantic";
    epoch: number;
  } | null>(null);
  const lastRefreshRemovedSelectionRef = useRef(false);
  const editorInvokerRef = useRef<HTMLElement | null>(null);
  const commandSubmissionBusyRef = useRef(false);
  const followedCommandsRef = useRef<Set<string>>(new Set());
  const commandAbortControllersRef = useRef<Set<AbortController>>(new Set());
  const metricConnectionRef = useRef<"unknown" | "available" | "lost">("unknown");
  const metricSequenceRef = useRef<{ generation: number; sequence: number } | null>(null);
  const metricGenerationRef = useRef<number | null>(null);
  modelRef.current = model;
  hierarchyRef.current = hierarchy;
  selectionRef.current = authoritativeSelection;
  presentationSelectionRef.current = presentationSelection;
  presentationStateRef.current = {
    mode: presentationMode,
    collapsedGroupIds,
    isolatedGroupId,
  };
  viewRef.current = view;

  const loadGraph = useCallback(async (
    options: { announceSuccess?: boolean } = {},
  ) => {
    lastRefreshRemovedSelectionRef.current = false;
    let completionNoticeSuppressed = false;
    const setLoadNotice = (message: string) => {
      completionNoticeSuppressed = true;
      setNotice(message);
    };
    const focusedSemanticKey =
      document.activeElement instanceof Element
        ? document.activeElement
            .closest<HTMLElement>("[data-semantic-key]")
            ?.dataset.semanticKey ?? null
        : null;
    const response = await fetch(`${apiBase}/graph`);
    const responseText = await response.text();
    let envelope: ApiEnvelope;
    try {
      envelope = JSON.parse(responseText) as ApiEnvelope;
    } catch {
      throw new Error("graph response is not valid JSON");
    }
    if (!response.ok || !envelope.success || envelope.data === undefined) {
      throw new Error(envelope.message ?? `HTTP ${response.status}`);
    }
    const rawGraph = extractTopLevelJsonMember(responseText, "data");
    if (!envelope.snapshot ||
        !Number.isSafeInteger(envelope.snapshot.coordinator_revision) ||
        envelope.snapshot.coordinator_revision < 0 ||
        typeof envelope.snapshot.content_identity !== "string" ||
        envelope.snapshot.content_identity.length === 0 ||
        envelope.snapshot.content_identity.length > 256) {
      throw new Error("graph response is missing its atomic snapshot identity");
    }
    const next = adaptGraphDocument(envelope.data);
    const nextHierarchy = adaptPresentationGroups(next);
    const wasLoaded = modelRef.current !== null;
    let nextSignature: string | null = null;
    try {
      nextSignature = await graphSignature(next, nextHierarchy);
    } catch {
      setLoadNotice(
        "View preferences are unavailable because Web Crypto SHA-256 is unavailable; deterministic defaults are active.",
      );
    }
    const graphIdentityChanged =
      nextSignature === null || signatureRef.current !== nextSignature;
    signatureRef.current = nextSignature;
    setPreferenceSignature(nextSignature);

    const validGroupIds = new Set(
      nextHierarchy.groups.map((group) => group.id),
    );
    const defaults = deterministicPresentationDefaults(nextHierarchy);
    if (graphIdentityChanged) {
      let restored = false;
      const storage = browserStorage();
      if (nextSignature !== null && storage !== null) {
        const saved = readPresentationPreferences(
          storage,
          nextSignature,
          validGroupIds,
        );
        if (saved.status === "valid") {
          restored = true;
          setPresentationMode(
            nextHierarchy.status === "valid" ? saved.value.mode : "raw",
          );
          setCollapsedGroupIds(new Set(saved.value.collapsed_group_ids));
          setSemanticExpandedGroupIds(
            new Set(saved.value.semantic_expanded_group_ids),
          );
          setPreferredViewport(saved.value.viewport);
        } else if (saved.status === "fallback") {
          setLoadNotice(saved.message);
          if (saved.disablePersistence) {
            setPreferencePersistenceDisabled(true);
          }
        }
      } else if (nextSignature !== null) {
        setLoadNotice(
          "View preferences are unavailable; deterministic defaults are active.",
        );
        setPreferencePersistenceDisabled(true);
      }
      if (!restored) {
        setPresentationMode(defaults.mode);
        setCollapsedGroupIds(defaults.collapsedGroupIds);
        setSemanticExpandedGroupIds(defaults.semanticExpandedGroupIds);
        setPreferredViewport(null);
      }
      setPreferencesDirty(false);
    } else {
      setCollapsedGroupIds((current) =>
        reconcileCollapsedGroups(nextHierarchy, current),
      );
      setSemanticExpandedGroupIds((current) =>
        new Set([...current].filter((id) => validGroupIds.has(id))),
      );
      setPresentationMode((current) =>
        nextHierarchy.status === "valid" ? current : "raw",
      );
    }

    const previousSelection = selectionRef.current;
    const selectionSurvives =
      previousSelection === null ||
      (previousSelection.kind === "node"
        ? next.nodes.some((node) => node.id === previousSelection.id)
        : next.edges.some((edge) => edge.id === previousSelection.id));
    const previousPresentationSelection = presentationSelectionRef.current;
    const nextPresentationState: PresentationState = {
      mode:
        nextHierarchy.status === "valid"
          ? presentationStateRef.current.mode
          : "raw",
      collapsedGroupIds: new Set(
        [...presentationStateRef.current.collapsedGroupIds].filter((id) =>
          validGroupIds.has(id),
        ),
      ),
      isolatedGroupId:
        presentationStateRef.current.isolatedGroupId !== null &&
        validGroupIds.has(presentationStateRef.current.isolatedGroupId)
          ? presentationStateRef.current.isolatedGroupId
          : null,
    };
    const presentationSelectionSurvives = presentationSelectionSurvivesRefresh(
      previousPresentationSelection,
      next,
      nextHierarchy,
      nextPresentationState,
    );
    if (wasLoaded && previousSelection !== null && !selectionSurvives) {
      lastRefreshRemovedSelectionRef.current = true;
      setLoadNotice(
        removedSelectionNotice(previousSelection),
      );
      pendingFocusRef.current = "__heading__";
    } else if (
      wasLoaded &&
      previousPresentationSelection !== null &&
      !presentationSelectionSurvives
    ) {
      lastRefreshRemovedSelectionRef.current = true;
      setLoadNotice(
        removedSelectionNotice(previousPresentationSelection),
      );
      pendingFocusRef.current = "__heading__";
    } else if (wasLoaded && focusedSemanticKey) {
      const [kind, ...identityParts] = focusedSemanticKey.split(":");
      const identity = identityParts.join(":");
      const focusSurvives =
        (kind === "node" || kind === "edit")
          ? next.nodes.some((node) => node.id === identity)
          : kind === "edge"
            ? next.edges.some((edge) => edge.id === identity)
            : (kind === "group" || kind === "group-disclosure") &&
              validGroupIds.has(identity);
      pendingFocusRef.current = focusSurvives
        ? focusedSemanticKey
        : "__heading__";
    }
    exportSnapshotRef.current = {
      raw_graph: rawGraph,
      coordinator_revision: envelope.snapshot.coordinator_revision,
      content_identity: envelope.snapshot.content_identity,
    };
    setModel(next);
    setGraphSnapshot(envelope.snapshot ?? null);
    setHierarchy(nextHierarchy);
    setLoadError(null);
    setIsolatedGroupId((current) =>
      current !== null && validGroupIds.has(current) ? current : null,
    );
    setPresentationSelection((current) =>
      current !== null && presentationSelectionSurvives ? current : null,
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
    if (!completionNoticeSuppressed && options.announceSuccess !== false) {
      setNotice(
        `${wasLoaded ? "Authoritative topology refreshed" : "Authoritative topology loaded"}: ${next.nodes.length} nodes and ${next.edges.length} edges.`,
      );
    }
  }, []);

  useEffect(() => {
    const pending = pendingFocusRef.current;
    if (!pending || !model) {
      return;
    }
    pendingFocusRef.current = null;
    requestAnimationFrame(() => {
      if (pending === "__heading__") {
        const target =
          document.getElementById(
            viewRef.current === "semantic" ? "semantic-heading" : "topology-heading",
          ) ??
          document.getElementById("empty-graph-heading") ??
          document.getElementById("dashboard-view-controls");
        target?.focus();
        return;
      }
      const target = [...document.querySelectorAll<HTMLElement>("[data-semantic-key]")]
        .find((element) => element.dataset.semanticKey === pending);
      target?.focus();
    });
  }, [model]);

  useEffect(() => {
    const pending = pendingPresentationFocusRef.current;
    if (!pending) {
      return;
    }
    pendingPresentationFocusRef.current = null;
    if (pending.originView !== view) {
      return;
    }
    const scheduledView = view;
    requestAnimationFrame(() => {
      // A presentation action can enqueue focus restoration just before the
      // operator changes views. Never let that stale callback steal focus
      // from the newly activated view control.
      if (
        pending.epoch !== presentationFocusEpochRef.current ||
        viewRef.current !== scheduledView
      ) {
        return;
      }
      const activeElement = document.activeElement;
      const invokerDetached = pending.invoker !== null && !pending.invoker.isConnected;
      if (
        activeElement !== pending.invoker &&
        !(activeElement === document.body && invokerDetached)
      ) {
        return;
      }
      if (pending.retainInvoker && pending.invoker?.isConnected) {
        pending.invoker.focus();
        return;
      }
      const fallback = pending.groupId === null
        ? document.querySelector<HTMLElement>('[data-focus-key="all-topology"]')
        : [...document.querySelectorAll<HTMLElement>("[data-focus-group]")]
            .find((element) => element.dataset.focusGroup === pending.groupId);
      if (fallback) {
        fallback.focus();
        return;
      }
      document
        .getElementById(
          viewRef.current === "semantic" ? "semantic-heading" : "topology-heading",
        )
        ?.focus();
    });
  }, [isolatedGroupId, presentationMode, view]);

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

  useEffect(() => {
    if (
      !preferencesDirty ||
      preferenceSignature === null ||
      preferencePersistenceDisabled
    ) {
      return;
    }
    const timer = window.setTimeout(() => {
      const storage = browserStorage();
      if (storage === null) {
        setNotice(
          "View preferences are unavailable; deterministic defaults were restored and browser persistence is disabled for this page.",
        );
        const defaults = deterministicPresentationDefaults(hierarchy);
        setPresentationMode(defaults.mode);
        setCollapsedGroupIds(defaults.collapsedGroupIds);
        setSemanticExpandedGroupIds(defaults.semanticExpandedGroupIds);
        setPreferredViewport(null);
        setIsolatedGroupId(null);
        setPresentationSelection(null);
        setViewportResetRevision((revision) => revision + 1);
        setPreferencePersistenceDisabled(true);
        setPreferencesDirty(false);
        return;
      }
      const knownGroups = new Set(hierarchy.groups.map((group) => group.id));
      const record: PresentationPreferences = {
        schema: 1,
        graph_signature: preferenceSignature,
        mode:
          hierarchy.status === "valid" ? presentationMode : "raw",
        collapsed_group_ids: [...collapsedGroupIds]
          .filter((id) => knownGroups.has(id))
          .sort(),
        semantic_expanded_group_ids: [...semanticExpandedGroupIds]
          .filter((id) => knownGroups.has(id))
          .sort(),
        viewport: preferredViewport ?? { x: 0, y: 0, zoom: 1 },
      };
      const failure = writePresentationPreferences(storage, record);
      if (failure) {
        setNotice(
          `${failure} Deterministic defaults were restored and browser persistence is disabled for this page.`,
        );
        void removePresentationPreferences(storage);
        const defaults = deterministicPresentationDefaults(hierarchy);
        setPresentationMode(defaults.mode);
        setCollapsedGroupIds(defaults.collapsedGroupIds);
        setSemanticExpandedGroupIds(defaults.semanticExpandedGroupIds);
        setPreferredViewport(null);
        setIsolatedGroupId(null);
        setPresentationSelection(null);
        setViewportResetRevision((revision) => revision + 1);
        setPreferencePersistenceDisabled(true);
      }
      setPreferencesDirty(false);
    }, 250);
    return () => window.clearTimeout(timer);
  }, [
    collapsedGroupIds,
    hierarchy.groups,
    hierarchy.status,
    preferenceSignature,
    preferencePersistenceDisabled,
    preferencesDirty,
    preferredViewport,
    presentationMode,
    semanticExpandedGroupIds,
  ]);

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
    setPreferencesDirty(true);
  }, []);

  const isolateGroup = useCallback((groupId: string) => {
    const invoker =
      document.activeElement instanceof HTMLElement
        ? document.activeElement
        : null;
    pendingPresentationFocusRef.current = {
      invoker,
      groupId,
      retainInvoker: invoker?.closest("#semantic-topology-region") !== null,
      originView: viewRef.current,
      epoch: ++presentationFocusEpochRef.current,
    };
    setPresentationMode("grouped");
    setIsolatedGroupId(groupId);
    setPresentationSelection({ kind: "group", id: groupId });
    setPreferencesDirty(true);
  }, []);

  const clearSelection = useCallback(() => {
    setAuthoritativeSelection(null);
    setPresentationSelection(null);
  }, []);

  const cancelPendingPresentationFocus = useCallback(() => {
    presentationFocusEpochRef.current += 1;
    pendingPresentationFocusRef.current = null;
  }, []);
  const activateDashboardView = useCallback(
    (nextView: "topology" | "semantic") => {
      cancelPendingPresentationFocus();
      setView(nextView);
    },
    [cancelPendingPresentationFocus],
  );

  const openEditor = useCallback((node: DisplayNode, invoker?: HTMLElement) => {
    editorInvokerRef.current =
      invoker ??
      (document.activeElement instanceof HTMLElement
        ? document.activeElement
        : null);
    setEditing(node);
  }, []);

  const restoreEditorFocus = useCallback(() => {
    const invoker = editorInvokerRef.current;
    editorInvokerRef.current = null;
    requestAnimationFrame(() => {
      if (invoker?.isConnected) {
        invoker.focus();
      } else {
        document.getElementById("semantic-heading")?.focus();
      }
    });
  }, []);

  const closeEditor = useCallback(() => {
    setEditing(null);
    restoreEditorFocus();
  }, [restoreEditorFocus]);

  const resetViewPreferences = useCallback(() => {
    const storage = browserStorage();
    const failure =
      storage === null
        ? "Saved view preferences are unavailable; deterministic defaults are active for this page."
        : removePresentationPreferences(storage);
    const defaults = deterministicPresentationDefaults(hierarchyRef.current);
    setPresentationMode(defaults.mode);
    setCollapsedGroupIds(defaults.collapsedGroupIds);
    setSemanticExpandedGroupIds(defaults.semanticExpandedGroupIds);
    setPreferredViewport(null);
    setViewportResetRevision((revision) => revision + 1);
    setIsolatedGroupId(null);
    setPresentationSelection(null);
    setPreferencesDirty(false);
    setNotice(
      failure ??
        `View preferences reset to deterministic defaults; ${PRESENTATION_PREFERENCE_KEY} was removed from local storage.`,
    );
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
    const discoveryAbort = new AbortController();
    void discoverCommands(discoveryAbort.signal)
      .then((discovered) => {
        setCommands(discovered);
        if (discovered.length > 0) setSelectedCommand(discovered[0].name);
      })
      .catch((error: unknown) => {
        if (!(error instanceof DOMException && error.name === "AbortError")) {
          setNotice(`Command discovery failed: ${error instanceof Error ? error.message : String(error)}`);
        }
      });
    void updateExecution();
    const timer = window.setInterval(() => void updateExecution(), 1000);
    return () => {
      discoveryAbort.abort();
      for (const controller of commandAbortControllersRef.current) controller.abort();
      commandAbortControllersRef.current.clear();
      window.clearInterval(timer);
    };
  }, [loadGraph, updateExecution]);

  useEffect(() => {
    if (!model || !execution || metricsPaused) return;
    let active = true;
    let timer = 0;
    let inFlight: AbortController | null = null;
    const poll = async () => {
      if (!active || inFlight !== null) return;
      inFlight = new AbortController();
      try {
        const snapshot = await fetchMetricsSnapshot(
          model,
          execution.graph_generation,
          inFlight.signal,
          metricSequenceRef.current,
        );
        if (!active) return;
        const prior = metricSequenceRef.current;
        if (prior && prior.generation === snapshot.graph_generation &&
            snapshot.snapshot_sequence < prior.sequence) return;
        metricSequenceRef.current = {
          generation: snapshot.graph_generation,
          sequence: snapshot.snapshot_sequence,
        };
        setMetrics(snapshot);
        setMetricsReceivedAt(Date.now());
        setMetricsBrowserStale(false);
        if (metricConnectionRef.current === "lost") {
          setNotice("Runtime metrics connection recovered.");
        }
        metricConnectionRef.current = "available";
      } catch (error) {
        if (!active || (error instanceof DOMException && error.name === "AbortError")) return;
        if (error instanceof IgnoredMetricSnapshotError) return;
        if (metricConnectionRef.current !== "lost") {
          setNotice(`Runtime metrics unavailable: ${error instanceof Error ? error.message : String(error)}`);
        }
        metricConnectionRef.current = "lost";
      } finally {
        inFlight = null;
        if (active) timer = window.setTimeout(() => void poll(), METRIC_POLL_MS);
      }
    };
    void poll();
    return () => {
      active = false;
      if (timer) window.clearTimeout(timer);
      inFlight?.abort();
    };
  }, [execution?.graph_generation, metricsPaused, model]);

  useEffect(() => {
    const generation = execution?.graph_generation ?? null;
    const retainedGenerationMismatch = metrics !== null &&
      metrics.graph_generation !== generation;
    if (retainedGenerationMismatch ||
        (metricGenerationRef.current !== null &&
         metricGenerationRef.current !== generation)) {
      setMetrics(null);
      setMetricsReceivedAt(null);
      setMetricsBrowserStale(false);
      metricSequenceRef.current = null;
      setNotice(`Runtime metrics invalidated for graph generation ${generation ?? "unavailable"}.`);
    }
    metricGenerationRef.current = generation;
  }, [execution?.graph_generation, metrics]);

  useEffect(() => {
    if (metricsPaused || metricsReceivedAt === null) {
      setMetricsBrowserStale(false);
      return;
    }
    const update = () => setMetricsBrowserStale(
      Date.now() - metricsReceivedAt > METRIC_STALE_MS,
    );
    update();
    const timer = window.setInterval(update, 250);
    return () => window.clearInterval(timer);
  }, [metricsPaused, metricsReceivedAt]);

  const retainOperation = useCallback((operation: OperationRecord) => {
    setCommandHistory((current) => retainOperationHistory(current, operation));
  }, []);

  const execute = async (command: string, argumentsValue: Record<string, unknown> = {}) => {
    if (commandSubmissionBusyRef.current) {
      setNotice("A command submission is already in progress.");
      return;
    }
    if (followedCommandsRef.current.has(command)) {
      setNotice(`Command ${command} already has an active operation.`);
      return;
    }
    const descriptor = commands.find((candidate) => candidate.name === command);
    if (!descriptor?.supported) {
      setNotice(`Command ${command} is unsupported: ${descriptor?.unsupportedReason ?? "not discovered"}`);
      return;
    }
    const argumentError = validateCommandArguments(descriptor, argumentsValue);
    if (argumentError) {
      setNotice(`Command ${command} is invalid: ${argumentError}`);
      return;
    }
    const abort = new AbortController();
    commandAbortControllersRef.current.add(abort);
    commandSubmissionBusyRef.current = true;
    setCommandSubmissionBusy(true);
    let ownsSubmission = true;
    let following = false;
    const releaseSubmission = () => {
      if (!ownsSubmission) return;
      ownsSubmission = false;
      commandSubmissionBusyRef.current = false;
      setCommandSubmissionBusy(false);
    };
    let acceptedOperation: OperationRecord | null = null;
    try {
      const submitted = await submitCommand(command, argumentsValue, abort.signal);
      acceptedOperation = submitted.operation;
      retainOperation({ ...submitted.operation, diagnostic: submitted.message });
      setNotice(submitted.message);
      if (submitted.location && !isTerminalOperation(submitted.operation.status)) {
        following = true;
        followedCommandsRef.current.add(command);
        setFollowedCommands(new Set(followedCommandsRef.current));
        releaseSubmission();
        const completed = await pollOperation(submitted.location, abort.signal, command);
        retainOperation({ ...completed, diagnostic: `Command ${command} ${completed.status}.` });
        setNotice(`Command ${command} ${completed.status}.`);
      }
      await updateExecution();
    } catch (error) {
      if (!(error instanceof DOMException && error.name === "AbortError")) {
        const diagnostic = error instanceof Error ? error.message : String(error);
        if (acceptedOperation && diagnostic === "operation expired or is unknown") {
          retainOperation({
            ...acceptedOperation,
            status: "expired",
            diagnostic: "Operation expired or is unknown.",
          });
          setNotice(`Command ${command} operation expired or is unknown.`);
        } else if (acceptedOperation) {
          retainOperation({
            ...acceptedOperation,
            status: "failed",
            diagnostic: `Operation follow-up failed: ${diagnostic}`,
          });
          setNotice(`Command ${command} failed: ${diagnostic}`);
        } else {
          setNotice(`Command ${command} failed: ${diagnostic}`);
        }
      }
    } finally {
      if (following) {
        followedCommandsRef.current.delete(command);
        setFollowedCommands(new Set(followedCommandsRef.current));
      }
      commandAbortControllersRef.current.delete(abort);
      releaseSubmission();
    }
  };

  const exportGraph = useCallback(() => {
    const snapshot = exportSnapshotRef.current;
    if (!snapshot) {
      setNotice("Graph export is unavailable until an authoritative snapshot is loaded.");
      return;
    }
    try {
      const { encoded, filename } = prepareGraphExportFromRaw(
        snapshot.raw_graph,
        snapshot.coordinator_revision,
        snapshot.content_identity,
      );
      const url = URL.createObjectURL(new Blob([encoded], { type: "application/json" }));
      const anchor = document.createElement("a");
      anchor.href = url;
      anchor.download = filename;
      try {
        anchor.click();
      } finally {
        URL.revokeObjectURL(url);
      }
      setNotice(`Graph export prepared: ${filename}`);
    } catch (error) {
      setNotice(`Graph export failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }, []);

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
    await Promise.all([loadGraph({ announceSuccess: false }), updateExecution()]);
    setEditing(null);
    restoreEditorFocus();
    if (!lastRefreshRemovedSelectionRef.current) {
      setNotice(`Parameters for ${node.id} updated in memory`);
    }
  };

  const runtimeValuesByIdentity = useMemo(() => {
    const result = new Map<string, NonNullable<typeof metrics>["values"]>();
    for (const value of metrics?.values ?? []) {
      const displayValue = metricsBrowserStale
        ? {
            ...value,
            availability: "unavailable" as const,
            reason: "browser_snapshot_stale",
            value: null,
            sample_time: null,
            rate: null,
          }
        : value;
      const current = result.get(value.identity) ?? [];
      current.push(displayValue);
      result.set(value.identity, current);
    }
    return result;
  }, [metrics, metricsBrowserStale]);
  const runtimeTextByNode = useMemo(() => new Map(
    (model?.nodes ?? []).map((node) => [
      node.id,
      (runtimeValuesByIdentity.get(`node:${node.id}`) ?? []).map(metricText),
    ]),
  ), [model?.nodes, runtimeValuesByIdentity]);
  const runtimeTextByEdge = useMemo(() => new Map(
    (model?.edges ?? []).map((edge) => [
      edge.id,
      (runtimeValuesByIdentity.get(`edge:${edge.id}`) ?? []).map(metricText),
    ]),
  ), [model?.edges, runtimeValuesByIdentity]);
  const runtimeTextByGroup = useMemo(() => new Map(
    hierarchy.groups.map((group) => {
      const values = [
        ...group.memberNodeIds.flatMap((id) => runtimeValuesByIdentity.get(`node:${id}`) ?? []),
        ...group.internalEdgeIds.flatMap((id) => runtimeValuesByIdentity.get(`edge:${id}`) ?? []),
      ];
      return [group.id, aggregateMetricText(
        values, group.memberNodeIds.length + group.internalEdgeIds.length,
      )];
    }),
  ), [hierarchy.groups, runtimeValuesByIdentity]);
  const runtimeTextByBundle = useMemo(() => new Map(
    (effectiveProjection?.bundles ?? []).map((bundle) => [
      bundle.id,
      aggregateMetricText(bundle.memberEdgeIds.flatMap((id) =>
        runtimeValuesByIdentity.get(`edge:${id}`) ?? []), bundle.memberEdgeIds.length),
    ]),
  ), [effectiveProjection?.bundles, runtimeValuesByIdentity]);
  const activeEdgeIds = useMemo(() => new Set(
    (!metricsPaused && !metricsBrowserStale && metrics?.availability.state === "available"
      ? metrics.values : [])
      .filter(isExactAvailableActivity)
      .map((value) => value.identity.slice("edge:".length)),
  ), [metrics, metricsPaused, metricsBrowserStale]);

  return (
    <div className="dashboard">
      <a className="skip-link" href="#dashboard-view-controls">
        Skip to dashboard view controls
      </a>
      <header className="management-header">
        <h1>GraphX Management Dashboard</h1>
        <p>Authoritative topology inspection, parameter editing, and execution control</p>
      </header>
      <main id="dashboard-main">
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
            {commands.map((command) => (
                <button
                  type="button"
                  key={command.name}
                  disabled={commandSubmissionBusy || followedCommands.has(command.name) ||
                    !command.supported}
                  title={command.supported ? command.description : command.unsupportedReason}
                  onClick={() => void execute(command.name)}
                >
                  {command.name[0]?.toLocaleUpperCase() + command.name.slice(1)}
                </button>
              ))}
          </div>
        </section>

        <section className="runtime-controls" aria-labelledby="command-palette-heading">
          <div>
            <h2 id="command-palette-heading">Typed command palette</h2>
            <label htmlFor="command-palette">Discovered command</label>
            <select id="command-palette" value={selectedCommand}
              onChange={(event) => {
                setSelectedCommand(event.target.value);
                setCommandArguments({});
              }}>
              {commands.map((command) => <option key={command.name} value={command.name}>
                {command.name}{command.supported ? "" : " (unsupported)"}
              </option>)}
            </select>
            <CommandArgumentControls
              descriptor={commands.find((command) => command.name === selectedCommand)}
              values={commandArguments}
              onChange={setCommandArguments}
            />
            <button type="button" disabled={commandSubmissionBusy ||
              followedCommands.has(selectedCommand) ||
              !commands.find((command) => command.name === selectedCommand)?.supported}
              onClick={() => void execute(selectedCommand, commandArguments)}>
              Submit typed command
            </button>
          </div>
          <div>
            <h2>Runtime observation</h2>
            <p data-testid="metrics-status">
              Metrics: {metricsPaused ? "paused" : metricsBrowserStale
                ? "stale" : metrics?.availability.state ?? "unavailable"}
              {metrics?.snapshot_time ? `; captured ${metrics.snapshot_time}` : ""}
              {metrics?.availability.reason ? `; ${metrics.availability.reason}` : ""}
            </p>
            <p className="runtime-legend">
              Runtime legend: exact available values include units and sample time;
              unavailable values include a reason; an animated edge means its exact
              generic <code>activity</code> gauge is currently positive.
            </p>
            <button type="button" aria-pressed={metricsPaused}
              onClick={() => {
                setMetricsPaused((current) => {
                  setNotice(current ? "Runtime metric updates resumed." : "Runtime metric updates paused; graph execution continues.");
                  return !current;
                });
              }}>
              {metricsPaused ? "Resume runtime updates" : "Pause runtime updates"}
            </button>
            <button type="button" onClick={exportGraph}>Export graph snapshot</button>
          </div>
          <details className="command-history">
            <summary>Command history ({commandHistory.length}/{COMMAND_HISTORY_LIMIT})</summary>
            <ol>
              {commandHistory.map((entry) => <li key={entry.operation_id}>
                <strong>{entry.command}</strong> {entry.status}; operation {entry.operation_id};
                state {entry.state}; revision {entry.coordinator_revision}; generation {entry.graph_generation}
                {entry.diagnostic ? `; ${entry.diagnostic}` : ""}
              </li>)}
            </ol>
          </details>
        </section>

        <div className="notice-region" role="status" aria-live="polite" aria-atomic="true">
          {notice && <div className="notice">{notice}</div>}
        </div>

        <nav
          id="dashboard-view-controls"
          className="view-tabs"
          aria-label="Dashboard views and local preferences"
          tabIndex={-1}
        >
          <button
            type="button"
            aria-pressed={view === "topology"}
            onFocus={cancelPendingPresentationFocus}
            onClick={() => activateDashboardView("topology")}
          >
            Topology
          </button>
          <button
            type="button"
            aria-pressed={view === "semantic"}
            onFocus={cancelPendingPresentationFocus}
            onClick={() => activateDashboardView("semantic")}
          >
            Semantic topology
          </button>
          <button
            type="button"
            data-storage-key={PRESENTATION_PREFERENCE_KEY}
            onClick={resetViewPreferences}
            aria-label="Reset view preferences stored only in this browser"
          >
            Reset view preferences
          </button>
        </nav>

        {loadError && (
          <section className="diagnostic-panel" role="alert">
            <h2>Topology fetch failed</h2>
            <p>{loadError}</p>
            <button
              type="button"
              onClick={(event) => {
                const invoker = event.currentTarget;
                pendingFocusRef.current = "__heading__";
                void loadGraph().catch((error: unknown) => {
                  pendingFocusRef.current = null;
                  setLoadError(error instanceof Error ? error.message : String(error));
                  requestAnimationFrame(() => {
                    if (invoker.isConnected) {
                      invoker.focus();
                    }
                  });
                });
              }}
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
            {effectiveProjection && model.diagnostics.length === 0 && (
              <HierarchyDiagnostics
                model={model}
                hierarchy={hierarchy}
                projection={effectiveProjection}
              />
            )}
            {model.diagnostics.length > 0 && <Diagnostics model={model} />}
            {model.nodes.length === 0 ? (
              <section className="empty-state">
                <h2 id="empty-graph-heading" tabIndex={-1}>Empty graph</h2>
                <p>The authoritative graph contains no nodes or edges.</p>
              </section>
            ) : (
              <div className="workspace">
                <section className="primary-view">
                  {view === "topology" ? (
                    <>
                      <h2 id="topology-heading" tabIndex={-1}>Read-only topology</h2>
                      <p className="structural-note">
                        Layout and navigation are presentation-only. Graph structure
                        cannot be edited here.
                      </p>
                      {model.diagnostics.length > 0 ? (
                        <p>
                          The canvas is withheld for malformed structure. Use Semantic topology
                          and the exact raw diagnostic above; no object was repaired or dropped silently.
                        </p>
                      ) : (
                        <>
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
                              setPreferencesDirty(true);
                            }}
                            onNavigate={(groupId, invoker) => {
                              pendingPresentationFocusRef.current = {
                                invoker,
                                groupId,
                                retainInvoker: true,
                                originView: viewRef.current,
                                epoch: ++presentationFocusEpochRef.current,
                              };
                              setPresentationMode("grouped");
                              setIsolatedGroupId(groupId);
                              setPresentationSelection(
                                groupId === null
                                  ? null
                                  : { kind: "group", id: groupId },
                              );
                              setPreferencesDirty(true);
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
                            preferredViewport={preferredViewport}
                            viewportResetRevision={viewportResetRevision}
                            reducedMotion={reducedMotion}
                            runtimeTextByNode={runtimeTextByNode}
                            runtimeTextByGroup={runtimeTextByGroup}
                            runtimeTextByEdge={runtimeTextByEdge}
                            runtimeTextByBundle={runtimeTextByBundle}
                            activeEdgeIds={activeEdgeIds}
                            onViewportChange={(viewport) => {
                              setPreferredViewport(viewport);
                              setPreferencesDirty(true);
                            }}
                          />
                        </>
                      )}
                    </>
                  ) : (
                    <SemanticTopology
                      model={model}
                      hierarchy={hierarchy}
                      projection={effectiveProjection!}
                      authoritativeSelection={authoritativeSelection}
                      presentationSelection={presentationSelection}
                      search={search}
                      typeFilter={typeFilter}
                      expandedGroupIds={semanticExpandedGroupIds}
                      collapsedGroupIds={collapsedGroupIds}
                      canvasFallbackActive={
                        layoutFallbackDiagnostic !== null &&
                        presentationMode === "grouped"
                      }
                      runtimeTextByNode={runtimeTextByNode}
                      runtimeTextByEdge={runtimeTextByEdge}
                      runtimeTextByGroup={runtimeTextByGroup}
                      runtimeTextByBundle={runtimeTextByBundle}
                      onSearch={setSearch}
                      onTypeFilter={setTypeFilter}
                      onExpandedGroup={(groupId, expanded) => {
                        setSemanticExpandedGroupIds((current) => {
                          if (current.has(groupId) === expanded) {
                            return current;
                          }
                          const next = new Set(current);
                          if (expanded) {
                            next.add(groupId);
                          } else {
                            next.delete(groupId);
                          }
                          return next;
                        });
                        setPreferencesDirty(true);
                      }}
                      onAuthoritativeSelect={setAuthoritativeSelection}
                      onPresentationSelect={setPresentationSelection}
                      onToggleCanvasGroup={toggleGroup}
                      onIsolateGroup={isolateGroup}
                      onEdit={(nodeId, invoker) => {
                        const node = model.nodes.find((candidate) => candidate.id === nodeId);
                        if (node) {
                          openEditor(node, invoker);
                        }
                      }}
                    />
                  )}
                </section>
                <Inspector
                  model={model}
                  hierarchy={hierarchy}
                  projection={effectiveProjection!}
                  authoritativeSelection={authoritativeSelection}
                  presentationSelection={presentationSelection}
                  runtimeTextByNode={runtimeTextByNode}
                  runtimeTextByEdge={runtimeTextByEdge}
                  runtimeTextByGroup={runtimeTextByGroup}
                  runtimeTextByBundle={runtimeTextByBundle}
                  onEdit={openEditor}
                  onSelectMemberEdge={(edgeId) => {
                    setPresentationSelection(null);
                    setAuthoritativeSelection({ kind: "edge", id: edgeId });
                    focusPersistentInspectorHeading();
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
          onClose={closeEditor}
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
  focusPersistentInspectorHeading,
  presentationSelectionSurvivesRefresh,
  removedSelectionNotice,
  synchronizeCanvasEdgeSelection,
};
