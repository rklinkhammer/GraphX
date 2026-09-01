import { useMemo } from "react";

import { formatPort } from "./adapter";
import { hierarchyDiagnosticText } from "./hierarchy";
import { groupContainsSelection } from "./presentation";
import {
  buildSemanticTopology,
  semanticEdgeSearchText,
  semanticNodeSearchText,
  type SemanticNodeRecord,
} from "./semantic";
import type {
  AuthoritativeSelection,
  DisplayGraph,
  DisplayGroup,
  DisplayHierarchy,
  PresentationProjection,
  PresentationSelection,
} from "./types";

interface SemanticTopologyProps {
  model: DisplayGraph;
  hierarchy: DisplayHierarchy;
  projection: PresentationProjection;
  authoritativeSelection: AuthoritativeSelection;
  presentationSelection: PresentationSelection;
  search: string;
  typeFilter: string;
  expandedGroupIds: ReadonlySet<string>;
  collapsedGroupIds: ReadonlySet<string>;
  canvasFallbackActive: boolean;
  runtimeTextByNode?: ReadonlyMap<string, string[]>;
  runtimeTextByEdge?: ReadonlyMap<string, string[]>;
  runtimeTextByGroup?: ReadonlyMap<string, string[]>;
  runtimeTextByBundle?: ReadonlyMap<string, string[]>;
  onSearch: (value: string) => void;
  onTypeFilter: (value: string) => void;
  onExpandedGroup: (groupId: string, expanded: boolean) => void;
  onAuthoritativeSelect: (selection: AuthoritativeSelection) => void;
  onPresentationSelect: (selection: PresentationSelection) => void;
  onToggleCanvasGroup: (groupId: string) => void;
  onIsolateGroup: (groupId: string) => void;
  onEdit: (nodeId: string, invoker: HTMLElement) => void;
}

function WarningList({ warnings }: { warnings: Array<{ code: string; entity: string; detail: string }> }) {
  if (warnings.length === 0) {
    return null;
  }
  return (
    <ul className="semantic-warnings" aria-label="Structural warnings">
      {warnings.map((warning, index) => (
        <li key={`${warning.code}-${warning.entity}-${index}`}>
          <strong>{warning.code}</strong> at {warning.entity}: {warning.detail}
        </li>
      ))}
    </ul>
  );
}

function NodeRecord({
  record,
  selected,
  onSelect,
  onEdit,
  runtimeText,
}: {
  record: SemanticNodeRecord;
  selected: boolean;
  onSelect: () => void;
  onEdit: (invoker: HTMLElement) => void;
  runtimeText: string[];
}) {
  const node = record.node;
  const groupPath =
    record.groupPath.length === 0
      ? "Ungrouped"
      : record.groupPath.map((group) => `${group.label} (${group.id})`).join(" / ");
  return (
    <article
      className={`semantic-node-record${selected ? " selected-record" : ""}`}
      data-semantic-record={`node:${node.id}`}
    >
      <div className="semantic-record-heading">
        <button
          type="button"
          className="identity semantic-selection"
          data-semantic-key={`node:${node.id}`}
          aria-label={`Select authoritative node ${node.id}, type ${node.type}`}
          aria-pressed={selected}
          onClick={onSelect}
        >
          {node.id}
        </button>
        <span className="semantic-state-text">
          {selected ? "Selected authoritative node" : "Authoritative node"}
        </span>
      </div>
      <dl className="semantic-record-details">
        <div><dt>Type</dt><dd>{node.type}</dd></div>
        <div><dt>Group path</dt><dd>{groupPath}</dd></div>
        <div>
          <dt>Input ports</dt>
          <dd>{node.inputPorts.map((port) => formatPort(port.key)).join(", ") || "None"}</dd>
        </div>
        <div>
          <dt>Output ports</dt>
          <dd>{node.outputPorts.map((port) => formatPort(port.key)).join(", ") || "None"}</dd>
        </div>
      </dl>
      <WarningList warnings={record.warnings} />
      <div className="semantic-runtime" aria-label={`Runtime metrics for node ${node.id}`}>
        {runtimeText.length === 0 ? "Runtime metrics unavailable" : runtimeText.slice(0, 8).join("; ")}
      </div>
      <button
        type="button"
        className="semantic-edit"
        data-semantic-key={`edit:${node.id}`}
        aria-label={`Edit parameters for node ${node.id}`}
        onClick={(event) => onEdit(event.currentTarget)}
      >
        Edit parameters
      </button>
    </article>
  );
}

export function SemanticTopology({
  model,
  hierarchy,
  projection,
  authoritativeSelection,
  presentationSelection,
  search,
  typeFilter,
  expandedGroupIds,
  collapsedGroupIds,
  canvasFallbackActive,
  runtimeTextByNode = new Map(),
  runtimeTextByEdge = new Map(),
  runtimeTextByGroup = new Map(),
  runtimeTextByBundle = new Map(),
  onSearch,
  onTypeFilter,
  onExpandedGroup,
  onAuthoritativeSelect,
  onPresentationSelect,
  onToggleCanvasGroup,
  onIsolateGroup,
  onEdit,
}: SemanticTopologyProps) {
  const semantic = useMemo(
    () => buildSemanticTopology(model, hierarchy),
    [hierarchy, model],
  );
  const groupsById = useMemo(
    () => new Map(hierarchy.groups.map((group) => [group.id, group])),
    [hierarchy.groups],
  );
  const nodesById = useMemo(
    () => new Map(semantic.nodes.map((record) => [record.node.id, record])),
    [semantic.nodes],
  );
  const primaryNodeOrder = useMemo(
    () => new Map(semantic.primaryNodeIds.map((id, index) => [id, index])),
    [semantic.primaryNodeIds],
  );
  const orderNodeIds = (ids: string[]) =>
    [...ids].sort(
      (left, right) =>
        (primaryNodeOrder.get(left) ?? Number.MAX_SAFE_INTEGER) -
        (primaryNodeOrder.get(right) ?? Number.MAX_SAFE_INTEGER),
    );
  const searchNeedle = search.trim().toLocaleLowerCase();
  const typeNeedle = typeFilter.trim().toLocaleLowerCase();
  const filtering = searchNeedle.length > 0 || typeNeedle.length > 0;
  const visibleNodeIds = useMemo(
    () =>
      new Set(
        semantic.nodes
          .filter(
            (record) =>
              semanticNodeSearchText(record).includes(searchNeedle) &&
              record.node.type.toLocaleLowerCase().includes(typeNeedle),
          )
          .map((record) => record.node.id),
      ),
    [searchNeedle, semantic.nodes, typeNeedle],
  );
  const visibleEdges = useMemo(
    () =>
      semantic.edges.filter((record) =>
        semanticEdgeSearchText(record).includes(searchNeedle),
      ),
    [searchNeedle, semantic.edges],
  );

  const renderNode = (nodeId: string) => {
    const record = nodesById.get(nodeId);
    if (!record || !visibleNodeIds.has(nodeId)) {
      return null;
    }
    return (
      <li key={nodeId}>
        <NodeRecord
          record={record}
          selected={
            authoritativeSelection?.kind === "node" &&
            authoritativeSelection.id === nodeId
          }
          onSelect={() => {
            onPresentationSelect(null);
            onAuthoritativeSelect({ kind: "node", id: nodeId });
          }}
          onEdit={(invoker) => onEdit(nodeId, invoker)}
          runtimeText={runtimeTextByNode.get(nodeId) ?? []}
        />
      </li>
    );
  };

  const renderGroup = (group: DisplayGroup) => {
    const childGroups = group.childIds
      .map((id) => groupsById.get(id))
      .filter((candidate): candidate is DisplayGroup => candidate !== undefined);
    const containsSelection = groupContainsSelection(group, authoritativeSelection);
    const selected =
      presentationSelection?.kind === "group" &&
      presentationSelection.id === group.id;
    const bundleCount = projection.bundles.filter(
      (bundle) => bundle.sourceId === group.id || bundle.targetId === group.id,
    ).length;
    const canvasCollapsed = collapsedGroupIds.has(group.id);
    const canvasState = canvasFallbackActive
      ? `raw layout fallback active; saved grouped preference ${canvasCollapsed ? "collapsed" : "expanded"}`
      : canvasCollapsed
        ? "collapsed"
        : "expanded";
    const forcedOpenForMatches =
      filtering && group.memberNodeIds.some((id) => visibleNodeIds.has(id));
    return (
      <li key={group.id} className="semantic-group-item">
        <details
          open={expandedGroupIds.has(group.id) || forcedOpenForMatches}
          onToggle={(event) => {
            if (forcedOpenForMatches) {
              if (!event.currentTarget.open) {
                event.currentTarget.open = true;
              }
              return;
            }
            onExpandedGroup(group.id, event.currentTarget.open);
          }}
        >
          <summary
            className="semantic-group-summary"
            data-semantic-key={`group-disclosure:${group.id}`}
          >
            <span>{group.label}</span>{" "}
            <span className="identity">({group.id})</span>{" "}
            <span>
              — {group.directMemberIds.length} direct members; {group.memberNodeIds.length} transitive/hidden-node members;{" "}
              {group.descendantGroupIds.length} descendant groups; {group.internalEdgeIds.length} internal edges;{" "}
              {group.hiddenEdgeIds.length} hidden-edge/crossing members; {bundleCount} visible bundles; canvas {canvasState}
              {containsSelection ? "; contains authoritative selection" : ""}
            </span>
          </summary>
          <p className="semantic-state-text">
            Canvas state: {canvasState}.
            {containsSelection ? " Contains the authoritative selection." : ""}
          </p>
          <div className="semantic-runtime" aria-label={`Runtime metrics for group ${group.id}`}>
            {(runtimeTextByGroup.get(group.id) ?? []).slice(0, 8).join("; ") ||
              "Runtime metrics unavailable"}
          </div>
          <div className="semantic-group-actions">
            <button
              type="button"
              data-semantic-key={`group:${group.id}`}
              aria-label={`Inspect group ${group.id}`}
              aria-pressed={selected}
              onClick={() => onPresentationSelect({ kind: "group", id: group.id })}
            >
              Inspect group
            </button>
            <button
              type="button"
              aria-label={
                canvasFallbackActive
                  ? `Set saved grouped preference ${canvasCollapsed ? "expanded" : "collapsed"}: group ${group.id}`
                  : `${canvasCollapsed ? "Expand" : "Collapse"} on canvas: group ${group.id}`
              }
              onClick={() => onToggleCanvasGroup(group.id)}
            >
              {canvasFallbackActive
                ? `Set saved grouped preference ${canvasCollapsed ? "expanded" : "collapsed"}`
                : canvasCollapsed
                  ? "Expand on canvas"
                  : "Collapse on canvas"}
            </button>
            <button
              type="button"
              aria-label={
                canvasFallbackActive
                  ? `Set grouped-canvas isolation: group ${group.id}`
                  : `Isolate on canvas: group ${group.id}`
              }
              onClick={() => onIsolateGroup(group.id)}
            >
              {canvasFallbackActive ? "Set grouped-canvas isolation" : "Isolate on canvas"}
            </button>
          </div>
          {(childGroups.length > 0 || group.directMemberIds.some((id) => visibleNodeIds.has(id))) && (
            <ul className="semantic-hierarchy-list">
              {childGroups.map(renderGroup)}
              {orderNodeIds(group.directMemberIds).map(renderNode)}
            </ul>
          )}
        </details>
      </li>
    );
  };

  return (
    <section
      id="semantic-topology-region"
      className="semantic-topology"
      aria-labelledby="semantic-heading"
    >
      <h2 id="semantic-heading" tabIndex={-1}>Semantic topology</h2>
      <p>
        This native hierarchy and edge table are the complete non-canvas topology alternative.
        Semantic disclosure is independent of canvas collapse and isolation.
      </p>
      {filtering && (
        <p className="semantic-filter-note">
          Matching nodes temporarily reveal their ancestor groups. Clearing both filters restores your semantic disclosures.
        </p>
      )}
      <div className="search-filter" role="search" aria-label="Semantic topology search">
        <label htmlFor="semantic-search">
          Search identities, types, groups, endpoints, or ports
          <input
            id="semantic-search"
            type="search"
            value={search}
            onChange={(event) => onSearch(event.target.value)}
          />
        </label>
        <label htmlFor="semantic-type-filter">
          Filter node type
          <input
            id="semantic-type-filter"
            type="search"
            value={typeFilter}
            onChange={(event) => onTypeFilter(event.target.value)}
          />
        </label>
      </div>
      <p className="semantic-counts" data-testid="semantic-counts">
        Showing {visibleNodeIds.size} of {semantic.nodes.length} authoritative nodes and{" "}
        {visibleEdges.length} of {semantic.edges.length} authoritative edges.
      </p>
      <WarningList warnings={semantic.globalWarnings} />

      <section aria-labelledby="semantic-hierarchy-heading">
        <h3 id="semantic-hierarchy-heading">Authored groups and authoritative nodes</h3>
        {hierarchy.status === "invalid" && (
          <p className="semantic-warning-text">
            Authored grouping is invalid. A flat, stable-identity node inventory is shown.
            {hierarchy.diagnostics[0]
              ? ` ${hierarchyDiagnosticText(hierarchy.diagnostics[0])}`
              : ""}
          </p>
        )}
        {semantic.rootGroupIds.length > 0 && (
          <ul className="semantic-hierarchy-list semantic-root-list">
            {semantic.rootGroupIds
              .map((id) => groupsById.get(id))
              .filter((group): group is DisplayGroup => group !== undefined)
              .map(renderGroup)}
          </ul>
        )}
        {semantic.ungroupedNodeIds.length > 0 && (
          <section className="semantic-ungrouped" aria-labelledby="semantic-ungrouped-heading">
            <h4 id="semantic-ungrouped-heading">
              Ungrouped nodes — {semantic.ungroupedNodeIds.length}
            </h4>
            <ul className="semantic-hierarchy-list">
              {orderNodeIds(semantic.ungroupedNodeIds).map(renderNode)}
            </ul>
          </section>
        )}
        {visibleNodeIds.size === 0 && <p>No authoritative nodes match the current filters.</p>}
      </section>

      <section aria-labelledby="semantic-edges-heading">
        <h3 id="semantic-edges-heading">Authoritative edges</h3>
        <div
          className="semantic-edge-table-region"
          role="region"
          aria-label="Complete authoritative edge table; use arrow keys to scroll if needed"
          tabIndex={0}
        >
          <table className="semantic-edge-table">
            <caption className="sr-only">
              Every authoritative graph edge exactly once in stable full-tuple identity order
            </caption>
            <thead>
              <tr>
                <th scope="col">Edge identity</th>
                <th scope="col">Source node</th>
                <th scope="col">Source port</th>
                <th scope="col">Target node</th>
                <th scope="col">Target port</th>
                <th scope="col">State</th>
              </tr>
            </thead>
            <tbody>
              {visibleEdges.map(({ edge, warnings }) => {
                const selected =
                  authoritativeSelection?.kind === "edge" &&
                  authoritativeSelection.id === edge.id;
                return (
                  <tr
                    key={edge.id}
                    className={selected ? "selected-row" : ""}
                    data-semantic-record={`edge:${edge.id}`}
                  >
                    <th scope="row">
                      <button
                        type="button"
                        className="identity semantic-selection"
                        data-semantic-key={`edge:${edge.id}`}
                        aria-label={`Select authoritative edge ${edge.id}; ${edge.sourceNodeId} ${formatPort(edge.sourcePort)} to ${edge.targetNodeId} ${formatPort(edge.targetPort)}`}
                        aria-pressed={selected}
                        onClick={() => {
                          onPresentationSelect(null);
                          onAuthoritativeSelect({ kind: "edge", id: edge.id });
                        }}
                      >
                        {edge.id}
                      </button>
                      <WarningList warnings={warnings} />
                    </th>
                    <td className="identity">{edge.sourceNodeId}</td>
                    <td>{formatPort(edge.sourcePort)}</td>
                    <td className="identity">{edge.targetNodeId}</td>
                    <td>{formatPort(edge.targetPort)}</td>
                    <td>
                      {selected ? "Selected authoritative edge" : "Not selected"}
                      <div className="semantic-runtime">
                        {(runtimeTextByEdge.get(edge.id) ?? []).slice(0, 8).join("; ") ||
                          "Runtime metrics unavailable"}
                      </div>
                    </td>
                  </tr>
                );
              })}
              {visibleEdges.length === 0 && (
                <tr><td colSpan={6}>No authoritative edges match the current search.</td></tr>
              )}
            </tbody>
          </table>
        </div>
      </section>

      {projection.bundles.length > 0 && (
        <section aria-labelledby="semantic-bundles-heading">
          <h3 id="semantic-bundles-heading">Visible presentation bundles</h3>
          <ul className="semantic-warnings">
            {projection.bundles.map((bundle) => (
              <li key={bundle.id}>
                <span className="identity">{bundle.id}</span>: {bundle.memberEdgeIds.length} exact member edges.
                {" "}{(runtimeTextByBundle.get(bundle.id) ?? []).slice(0, 8).join("; ") ||
                  "Runtime metrics unavailable"}
              </li>
            ))}
          </ul>
        </section>
      )}
    </section>
  );
}
