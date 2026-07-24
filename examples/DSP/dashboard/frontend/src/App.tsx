import { useCallback, useEffect, useMemo, useState } from 'react';
import { getGraph } from './api';
import type { ObservationResource, SnapshotResource } from './api';
import {
  DetectorBankView, DetectorInspector, selectPhysicalChannel, selectedPhysicalChannel,
} from './DetectorBankView';
import { DETECTOR_BANK_ID, toFHSSPresentation } from './fhssPresentation';
import { GraphView, type Selection } from './GraphView';
import { Inspector } from './Inspector';
import { Operations } from './Operations';
import { TopologyTable } from './TopologyTable';
import type { GraphContract, GraphResource, LoadState } from './domain';
import { isPresentationBundleEdge, toTopology, type TopologyModel } from './topology';
import type { FHSSPresentation } from './fhssPresentation';
import { useEventTransport } from './useEventTransport';

export const SELECTION_CLEARED_MESSAGE =
  'Selection cleared because the selected topology identity is not present in the refreshed graph.';

export function App() {
  const [load, setLoad] = useState<LoadState>({ kind: 'loading' });
  const [selection, setSelection] = useState<Selection | null>(null);
  const [refreshToken, setRefreshToken] = useState(0);
  const [detectorBankExpanded, setDetectorBankExpanded] = useState(false);
  const [observation, setObservation] = useState<ObservationResource>();
  const [snapshot, setSnapshot] = useState<SnapshotResource>();
  const [selectionNotice, setSelectionNotice] = useState('');
  const refresh = useCallback(async () => {
    try {
      const resource = await getGraph();
      setLoad(resource.graph.nodes.length
        ? { kind: 'ready', graph: resource.graph, revision: resource.config_revision, resource }
        : { kind: 'empty', message: 'The graph resource contains no nodes.' });
    } catch (error) {
      setLoad((current) => current.kind === 'ready' || current.kind === 'stale'
        ? {
            kind: 'stale', graph: current.graph, revision: current.revision,
            resource: current.resource, message: `Showing stale topology: ${String(error)}`,
          }
        : navigator.onLine ? { kind: 'error', message: String(error) } : { kind: 'disconnected', message: 'Browser is offline.' });
    }
  }, []);
  useEffect(() => { void refresh(); const timer = window.setInterval(() => void refresh(), 10000); return () => window.clearInterval(timer); }, [refresh]);
  const transportStatus = useEventTransport(() => { void refresh(); setRefreshToken((value) => value + 1); });
  const graph = load.kind === 'ready' || load.kind === 'stale' ? load.graph : undefined;
  const prepared = useMemo(() => {
    if (!graph) return {};
    try {
      const model = toTopology(graph);
      return { model, presentation: toFHSSPresentation(model) };
    } catch (error) {
      return { error: `Topology presentation unavailable: ${String(error)}` };
    }
  }, [graph]);
  const { model, presentation } = prepared;
  const chooseSelection = useCallback((next: Selection) => {
    setSelectionNotice('');
    setSelection(next);
  }, []);
  useEffect(() => {
    if (!selection || !model || !presentation) return;
    const present = selectionIsPresent(selection, model, presentation);
    if (!present) {
      setSelection(null);
      setSelectionNotice(SELECTION_CLEARED_MESSAGE);
    }
  }, [model, presentation, selection]);
  useEffect(() => {
    if (!detectorBankExpanded || selection?.kind !== 'edge' || !presentation) return;
    const selectedBundle = presentation.collapsed.edges.find((edge) =>
      edge.id === selection.id && isPresentationBundleEdge(edge));
    if (selectedBundle) setSelection({ kind: 'node', id: DETECTOR_BANK_ID });
  }, [detectorBankExpanded, presentation, selection]);
  const canvasSelection = selection?.kind === 'node'
    && !detectorBankExpanded
    && presentation?.detectorBank?.authoritativeNodeIds.includes(selection.id)
    ? { kind: 'node' as const, id: DETECTOR_BANK_ID }
    : selection;
  const displayModel = presentation
    ? detectorBankExpanded ? presentation.expanded : presentation.collapsed
    : undefined;
  return <>
    <header><div><p className="eyebrow">Engineering and research platform</p><h1>GraphX FHSS Dashboard</h1><p>Synthetic IQ evaluation only — no HWIL, OTA, live-RF, or production-RF qualification.</p></div><p className="transport" role="status" aria-live="polite">{transportStatus}</p></header>
    <main id="main" tabIndex={-1}>
      <LoadStatePanel load={load} retry={() => void refresh()} />
      {'error' in prepared && prepared.error
        && <section className="state" role="alert"><h2>Topology presentation unavailable</h2><p>{prepared.error}</p><p>The operator workbench remains available.</p></section>}
      {selectionNotice && <p className="stale" role="status">{selectionNotice}</p>}
      {model && presentation && displayModel && <>
        <div className="topology-layout">
          <GraphView model={displayModel} selection={canvasSelection} onSelection={chooseSelection}
            authoritativeCounts={{ nodes: model.nodes.length, edges: model.edges.length }} />
          <div className="inspector-column">
            <Inspector model={model} selection={selection} snapshot={snapshot}
              graphResource={(load.kind === 'ready' || load.kind === 'stale') ? load.resource : undefined} />
            <DetectorInspector presentation={presentation} selection={selection} observation={observation} onSelection={chooseSelection} />
          </div>
          <TopologyTable model={displayModel} selection={selection} onSelection={chooseSelection}
            authoritativeCounts={{ nodes: model.nodes.length, edges: model.edges.length }} />
        </div>
        <DetectorBankView presentation={presentation} expanded={detectorBankExpanded}
          onExpanded={setDetectorBankExpanded} selection={selection} onSelection={chooseSelection}
          observation={observation} />
        <details className="raw-diagnostic">
          <summary>Advanced diagnostic: full authoritative GraphX JSON</summary>
          <p>This escaped text is the complete unmodified graph resource. It is diagnostic data, not the primary topology presentation.</p>
          <pre>{JSON.stringify(authoritativeGraphDocument(load, graph!), null, 2)}</pre>
        </details>
      </>}
      <Operations refreshToken={refreshToken}
        selectedPhysicalChannel={presentation ? selectedPhysicalChannel(presentation, selection) : undefined}
        onPhysicalChannel={(channel) => { if (presentation && channel !== undefined) selectPhysicalChannel(presentation, channel, chooseSelection); }}
        onObservation={setObservation} onSnapshot={setSnapshot} />
    </main>
    <footer>One dashboard at <code>/</code> · authoritative application namespace <code>/api/v1/fhss</code> · topology is read-only</footer>
  </>;
}

export function authoritativeGraphDocument(
  load: LoadState,
  graph: GraphContract,
): GraphResource {
  if (load.kind === 'ready' || load.kind === 'stale') {
    return load.resource ?? {
      schema: 'graphx.dashboard.graph.v1',
      owner: 'receiver',
      config_revision: load.revision ?? 0,
      etag: '',
      graph,
    };
  }
  return { schema: 'graphx.dashboard.graph.v1', owner: 'receiver', config_revision: 0, etag: '', graph };
}

export function selectionIsPresent(
  selection: Selection,
  model: TopologyModel,
  presentation: FHSSPresentation,
): boolean {
  return selection.kind === 'node'
    ? (selection.id === DETECTOR_BANK_ID && presentation.detectorBank !== undefined)
      || model.nodes.some((node) => node.id === selection.id)
    : model.edges.some((edge) => edge.id === selection.id)
      || presentation.collapsed.edges.some((edge) => edge.id === selection.id)
      || presentation.expanded.edges.some((edge) => edge.id === selection.id);
}

export function LoadStatePanel({ load, retry }: { load: LoadState; retry: () => void }) {
  if (load.kind === 'loading') return <section className="state" aria-live="polite"><h2>Loading topology…</h2></section>;
  if (load.kind === 'error' || load.kind === 'disconnected' || load.kind === 'empty') return <section className="state" role="alert"><h2>{load.kind === 'empty' ? 'Empty topology' : 'Dashboard unavailable'}</h2><p>{load.message}</p><button type="button" onClick={retry}>Retry</button></section>;
  if (load.kind === 'stale') return <p className="stale" role="status">{load.message}</p>;
  return null;
}
