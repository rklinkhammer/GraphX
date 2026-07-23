import { useCallback, useEffect, useMemo, useState } from 'react';
import { getGraph } from './api';
import { GraphView, type Selection } from './GraphView';
import { Inspector } from './Inspector';
import { Operations } from './Operations';
import { TopologyTable } from './TopologyTable';
import type { LoadState } from './domain';
import { toTopology } from './topology';
import { useEventTransport } from './useEventTransport';

export function App() {
  const [load, setLoad] = useState<LoadState>({ kind: 'loading' });
  const [selection, setSelection] = useState<Selection | null>(null);
  const [refreshToken, setRefreshToken] = useState(0);
  const refresh = useCallback(async () => {
    try {
      const resource = await getGraph();
      setLoad(resource.graph.nodes.length ? { kind: 'ready', graph: resource.graph, revision: resource.config_revision } : { kind: 'empty', message: 'The graph resource contains no nodes.' });
    } catch (error) {
      setLoad((current) => current.kind === 'ready' || current.kind === 'stale'
        ? { kind: 'stale', graph: current.graph, message: `Showing stale topology: ${String(error)}` }
        : navigator.onLine ? { kind: 'error', message: String(error) } : { kind: 'disconnected', message: 'Browser is offline.' });
    }
  }, []);
  useEffect(() => { void refresh(); const timer = window.setInterval(() => void refresh(), 10000); return () => window.clearInterval(timer); }, [refresh]);
  const transportStatus = useEventTransport(() => { void refresh(); setRefreshToken((value) => value + 1); });
  const graph = load.kind === 'ready' || load.kind === 'stale' ? load.graph : undefined;
  const model = useMemo(() => graph ? toTopology(graph) : undefined, [graph]);
  useEffect(() => {
    if (!selection || !model) return;
    const present = selection.kind === 'node' ? model.nodes.some((node) => node.id === selection.id) : model.edges.some((edge) => edge.id === selection.id);
    if (!present) setSelection(null);
  }, [model, selection]);
  return <>
    <header><div><p className="eyebrow">Engineering and research platform</p><h1>GraphX FHSS Dashboard</h1><p>Synthetic IQ evaluation only — no HWIL, OTA, live-RF, or production-RF qualification.</p></div><p className="transport" role="status" aria-live="polite">{transportStatus}</p></header>
    <main id="main" tabIndex={-1}>
      <LoadStatePanel load={load} retry={() => void refresh()} />
      {model && <div className="topology-layout"><GraphView model={model} selection={selection} onSelection={setSelection} /><Inspector model={model} selection={selection} /><TopologyTable model={model} selection={selection} onSelection={setSelection} /></div>}
      <Operations refreshToken={refreshToken} />
    </main>
    <footer>One dashboard at <code>/</code> · authoritative application namespace <code>/api/v1/fhss</code> · topology is read-only</footer>
  </>;
}

export function LoadStatePanel({ load, retry }: { load: LoadState; retry: () => void }) {
  if (load.kind === 'loading') return <section className="state" aria-live="polite"><h2>Loading topology…</h2></section>;
  if (load.kind === 'error' || load.kind === 'disconnected' || load.kind === 'empty') return <section className="state" role="alert"><h2>{load.kind === 'empty' ? 'Empty topology' : 'Dashboard unavailable'}</h2><p>{load.message}</p><button type="button" onClick={retry}>Retry</button></section>;
  if (load.kind === 'stale') return <p className="stale" role="status">{load.message}</p>;
  return null;
}
