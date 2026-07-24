import type { Selection } from './GraphView';
import { StructuredValue } from './Operations';
import type { SnapshotResource } from './api';
import type { GraphResource } from './domain';
import type { TopologyModel } from './topology';

export function Inspector({ model, selection, snapshot, graphResource }: {
  model: TopologyModel; selection: Selection | null; snapshot?: SnapshotResource;
  graphResource?: GraphResource;
}) {
  const node = selection?.kind === 'node' ? model.nodes.find((candidate) => candidate.id === selection.id) : undefined;
  const edge = selection?.kind === 'edge' ? model.edges.find((candidate) => candidate.id === selection.id) : undefined;
  const metrics = snapshot?.metrics;
  const diagnostics = snapshot?.diagnostics;
  const metricsCoherent = inspectorEvidenceCoherent(graphResource, snapshot);
  const diagnosticsCoherent = metricsCoherent;
  const nodeMetric = node && metrics && metricsCoherent
    ? metrics.nodes.find((record) => record.node_id === node.id) : undefined;
  const nodeDiagnostic = node && diagnostics && diagnosticsCoherent
    ? diagnostics.nodes.find((record) => record.node_id === node.id) : undefined;
  const edgeMetric = edge && metrics && metricsCoherent
    ? metrics.edges.find((record) => record.edge_id === edge.id) : undefined;
  return <aside className="inspector" aria-labelledby="inspector-heading">
    <h2 id="inspector-heading">Selection inspector</h2>
    {!selection && <p>Select a node or exact-port edge from either representation.</p>}
    {node && <><dl><dt>Stable configuration identity</dt><dd>{node.id}</dd><dt>Node type</dt><dd>{node.type}</dd><dt>Input ports</dt><dd>{node.inputPorts.join(', ') || 'none'}</dd><dt>Output ports</dt><dd>{node.outputPorts.join(', ') || 'none'}</dd></dl><details><summary>Configuration metadata</summary><StructuredValue value={node.configuration} /></details>
      <QualifiedIdentity metrics={metrics} coherent={metricsCoherent} />
      <details><summary>Canonically correlated node metrics</summary><StructuredValue value={nodeMetric ?? { availability: 'unavailable', reason: metricsCoherent ? 'No record for canonical node ID' : 'Metric identity tuple is stale or unavailable' }} /></details>
      <details><summary>Canonically correlated diagnostics</summary><StructuredValue value={nodeDiagnostic ?? { availability: 'unavailable', reason: diagnosticsCoherent ? 'No record for canonical node ID' : 'Diagnostic identity tuple is stale or unavailable' }} /></details></>}
    {edge && <><dl><dt>Stable port-aware identity</dt><dd>{edge.id}</dd><dt>Source</dt><dd>{edge.source_node_id}, port {edge.source_port}</dd><dt>Target</dt><dd>{edge.target_node_id}, port {edge.target_port}</dd></dl>
      <QualifiedIdentity metrics={metrics} coherent={metricsCoherent} />
      <details><summary>Canonically correlated edge metrics</summary><StructuredValue value={edgeMetric ?? { availability: 'unavailable', reason: metricsCoherent ? 'No record for canonical edge ID' : 'Metric identity tuple is stale or unavailable' }} /></details>
      {edgeMetric && <QueueDepthSummary edgeMetric={edgeMetric} />}</>}
    <p className="deferred">Runtime indices and names are noncanonical diagnostic fields. Correlation uses only stable configuration and exact-port edge identities. No generic latency or animated activity is inferred.</p>
  </aside>;
}

export function QueueDepthSummary({ edgeMetric }: { edgeMetric: Record<string, unknown> }) {
  const depthAvailability = edgeMetric.current_queue_depth_availability as
    | { state?: unknown; reason?: unknown }
    | undefined;
  const current = depthAvailability?.state === 'available'
    ? `${String(edgeMetric.current_queue_depth)} messages`
    : `unavailable (${typeof depthAvailability?.reason === 'string' ? depthAvailability.reason : 'no authoritative queue sample'})`;
  const peak = edgeMetric.availability === 'available'
    ? `${String(edgeMetric.peak_queue_depth)} messages`
    : 'unavailable';
  return <p>Current queue depth: {current}. Peak queue depth: {peak}. These are distinct gauges.</p>;
}

export function inspectorEvidenceCoherent(graph: GraphResource | undefined, snapshot: SnapshotResource | undefined): boolean {
  if (!graph || !snapshot) return false;
  return graph.config_revision === snapshot.config_revision
    && graph.etag === snapshot.config_etag
    && snapshot.graph.config_revision === graph.config_revision
    && snapshot.graph.etag === graph.etag
    && snapshot.runtime.active_generation === snapshot.generation
    && snapshot.runtime.active_run_epoch === snapshot.run_epoch
    && snapshot.runtime.active_config_revision === snapshot.config_revision
    && snapshot.runtime.active_config_etag === snapshot.config_etag
    && snapshot.metrics.active_generation === snapshot.generation
    && snapshot.metrics.active_run_epoch === snapshot.run_epoch
    && snapshot.metrics.active_config_revision === snapshot.config_revision
    && snapshot.metrics.active_config_etag === snapshot.config_etag
    && snapshot.diagnostics.active_generation === snapshot.generation
    && snapshot.diagnostics.active_run_epoch === snapshot.run_epoch
    && snapshot.diagnostics.active_config_revision === snapshot.config_revision
    && snapshot.diagnostics.active_config_etag === snapshot.config_etag
    && snapshot.coherence.metric_capture_id === snapshot.metrics.capture_id
    && snapshot.coherence.diagnostic_capture_id === snapshot.diagnostics.capture_id;
}

function QualifiedIdentity({ metrics, coherent }: { metrics?: SnapshotResource['metrics']; coherent: boolean }) {
  if (!metrics || !coherent) return <p role="status">Runtime evidence unavailable or stale for this configuration.</p>;
  const rates = Array.isArray(metrics.qualified_rates) ? metrics.qualified_rates : [];
  return <><dl><dt>Graph generation</dt><dd>{metrics.active_generation}</dd><dt>Run epoch</dt><dd>{metrics.active_run_epoch}</dd><dt>Configuration revision</dt><dd>{metrics.active_config_revision}</dd><dt>Configuration ETag</dt><dd>{metrics.active_config_etag || 'unavailable'}</dd><dt>Capture identity</dt><dd>{metrics.capture_id}</dd></dl>
    <p>Qualified server-interval rates: {rates.length ? rates.map((rate) => `${String((rate as Record<string, unknown>).name)} ${String((rate as Record<string, unknown>).value)} messages/s over ${String((rate as Record<string, unknown>).interval_ms)} ms`).join('; ') : 'unavailable'}</p></>;
}
