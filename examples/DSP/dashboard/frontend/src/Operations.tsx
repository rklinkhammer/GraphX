import { useCallback, useEffect, useState } from 'react';
import type { CSSProperties, ReactNode } from 'react';
import {
  getSpectrum, postCommand, requestJson, resourceLoaders,
  type ComparisonResource, type ConfigurationResource, type ExpectedTruthResource,
  type InvestigationHistoryResource, type JobHistoryResource, type MetricsResource,
  type ObservationResource, type SpectrumResource,
} from './api';

type DataMap = Record<string, unknown>;
const readableName = (name: string) => name.replaceAll('_', ' ');

function resourceWithSchema<T extends DataMap>(value: unknown, expected: string): T | undefined {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return undefined;
  const document = value as DataMap;
  return document.schema === expected ? document as T : undefined;
}

export function StructuredValue({ value, depth = 0 }: { value: unknown; depth?: number }): ReactNode {
  if (value === null || value === undefined) return <span>Unavailable</span>;
  if (typeof value !== 'object') return <span>{String(value)}</span>;
  if (depth >= 4) return <span>Nested detail available ({Array.isArray(value) ? value.length : Object.keys(value).length} items)</span>;
  if (Array.isArray(value)) return value.length === 0 ? <span>None</span> : <ol className="structured-list">{value.slice(0, 64).map((entry, index) => <li key={index}><StructuredValue value={entry} depth={depth + 1} /></li>)}{value.length > 64 && <li>{value.length - 64} additional items omitted</li>}</ol>;
  return <dl className="structured-fields">{Object.entries(value).slice(0, 128).map(([field, entry]) => <div key={field}><dt>{readableName(field)}</dt><dd><StructuredValue value={entry} depth={depth + 1} /></dd></div>)}</dl>;
}

export function ResourcePanel({ name, value }: { name: string; value: unknown }) {
  return <details><summary>{readableName(name)}</summary><StructuredValue value={value} /></details>;
}

export function Visualization({ value }: { value: unknown }) {
  const document = value && typeof value === 'object' ? value as DataMap : {};
  const schedule = document.schedule && typeof document.schedule === 'object' ? document.schedule as DataMap : {};
  const heatmap = document.heatmap && typeof document.heatmap === 'object' ? document.heatmap as DataMap : {};
  const timeline = document.timeline && typeof document.timeline === 'object' ? document.timeline as DataMap : {};
  const channels = Array.isArray(heatmap.channels) ? heatmap.channels as DataMap[] : [];
  const maximum = Math.max(1, ...channels.map((channel) => Number(channel.expected_pulse_count ?? 0)));
  return <article className="visualization"><h3>FHSS Schedule</h3><p>{String(document.fixture_label ?? 'Synthetic schedule unavailable.')}</p>
    <ResourcePanel name="scheduled_messages" value={schedule} />
    <h3>64-Channel Heatmap</h3><div className="heatmap" aria-label="Expected pulse counts by physical channel">
      {channels.map((channel) => { const count = Number(channel.expected_pulse_count ?? 0); return <div key={String(channel.channel_index)} style={{ '--load': count / maximum } as CSSProperties}><strong>Ch {String(channel.channel_index)}</strong><span>{count} expected pulses</span></div>; })}
    </div><h3>Synthetic Schedule Expectations</h3><ResourcePanel name="expected_pulse_timeline" value={timeline} /></article>;
}

export function MetricsActivity({ value }: { value: MetricsResource | undefined }) {
  if (!value) return <article><h3>Runtime metrics and topology activity</h3><p>Metrics unavailable.</p></article>;
  return <article><h3>Runtime metrics and topology activity</h3><dl className="metric-summary">
    {Object.entries(value.graph).map(([name, metric]) => <div key={name}><dt>{readableName(name)}</dt><dd><StructuredValue value={metric} /></dd></div>)}
  </dl><ResourcePanel name={`node_activity (${value.nodes.length})`} value={value.nodes} /><ResourcePanel name={`edge_activity (${value.edges.length})`} value={value.edges} /></article>;
}

export function ReceiverEvidence({ expected, observation, comparison, spectrum, channel, onChannel }: {
  expected?: ExpectedTruthResource; observation?: ObservationResource; comparison?: ComparisonResource;
  spectrum?: SpectrumResource; channel: number | undefined; onChannel: (channel: number | undefined) => void;
}) {
  return <article><h3>Expected versus receiver-observed evidence</h3><p>Expected truth is evaluator-side synthetic evidence and never receiver input.</p>
    <div className="evidence-grid"><section><h4>Synthetic expected truth</h4><p>{expected ? `${expected.messages.length} messages; ${expected.pulses.length} pulses` : 'Unavailable'}</p><StructuredValue value={expected?.expected_receiver_message} /></section>
    <section><h4>Receiver observation</h4><p>{observation ? `${observation.observed_pulses.length} observed pulses` : 'Unavailable'}</p><StructuredValue value={observation?.receiver_message_result ?? observation?.availability} /></section>
    <section><h4>Evaluator comparison</h4><p>{comparison?.evaluation_state ?? 'Unavailable'}; {comparison?.matches.length ?? 0} matches</p><StructuredValue value={comparison} /></section></div>
    <label htmlFor="spectrum-channel">Receiver spectrum physical channel</label><select id="spectrum-channel" value={channel ?? ''} onChange={(event) => onChannel(event.target.value === '' ? undefined : Number(event.target.value))}><option value="">Observed/default channel</option>{Array.from({ length: 64 }, (_, value) => <option key={value} value={value}>Channel {value}</option>)}</select>
    <p>Selected channel: {spectrum?.channel_index ?? 'unavailable'}; bins: {spectrum?.bins.length ?? 0}</p><StructuredValue value={spectrum} /></article>;
}

export function Operations({ refreshToken }: { refreshToken: number }) {
  const [data, setData] = useState<DataMap>({});
  const [status, setStatus] = useState('Loading operator resources…');
  const [patch, setPatch] = useState('[{"op":"replace","path":"/iq_center_frequency_hz","value":1240000000.0}]');
  const [bundleName, setBundleName] = useState('fhss-investigation-1');
  const [jobId, setJobId] = useState('');
  const [operationId, setOperationId] = useState('');
  const [copyIq, setCopyIq] = useState(false);
  const [confirmCopy, setConfirmCopy] = useState(false);
  const [spectrumChannel, setSpectrumChannel] = useState<number>();
  const refresh = useCallback(async () => {
    const pairs = await Promise.all(Object.entries(resourceLoaders).map(async ([name, load]) => {
      try { return [name, await load()] as const; }
      catch (error) { return [name, { availability: 'unavailable', reason: String(error) }] as const; }
    }));
    setData(Object.fromEntries(pairs)); setStatus('Operator resources refreshed');
  }, []);
  useEffect(() => { void refresh(); }, [refresh, refreshToken]);
  useEffect(() => { void getSpectrum(spectrumChannel).then((value) => setData((current) => ({ ...current, spectrum: value }))).catch((error) => setStatus(`Spectrum failed: ${String(error)}`)); }, [spectrumChannel]);
  const command = async (label: string, path: string, body: unknown, headers?: HeadersInit, method = 'POST') => {
    setStatus(`${label} in progress…`);
    try { const result = headers ? await requestJson(path, { method, headers, body: JSON.stringify(body) }) : await postCommand(path, body as Record<string, unknown>); const response = result && typeof result === 'object' ? result as DataMap : {}; setStatus(`${label}: ${String(response.state ?? response.status ?? response.schema ?? 'completed')}`); await refresh(); }
    catch (error) { setStatus(`${label} failed: ${String(error)}`); }
  };
  const configuration = resourceWithSchema<ConfigurationResource>(data.configuration, 'graphx.dashboard.config.v1');
  const revision = configuration?.config_revision ?? 0;
  const configurationEtag = configuration?.httpEtag ?? '';
  const jobs = resourceWithSchema<JobHistoryResource>(data.jobs, 'graphx.dashboard.fhss_job_history.v1');
  const investigations = resourceWithSchema<InvestigationHistoryResource>(data.investigations, 'graphx.dashboard.fhss_investigation_operations.v1');
  const expectedTruth = resourceWithSchema<ExpectedTruthResource>(data.expectedTruth, 'graphx.dashboard.fhss_expected_truth.v1');
  const observations = resourceWithSchema<ObservationResource>(data.observations, 'graphx.dashboard.fhss_receiver_observation.v1');
  const comparison = resourceWithSchema<ComparisonResource>(data.comparison, 'graphx.dashboard.fhss_comparison_result.v1');
  const spectrum = resourceWithSchema<SpectrumResource>(data.spectrum, 'graphx.dashboard.fhss_receiver_spectrum.v1');
  const metrics = resourceWithSchema<MetricsResource>(data.metrics, 'graphx.dashboard.metrics.v1');
  return <section className="operations" aria-labelledby="operations-heading">
    <div className="section-heading"><div><h2 id="operations-heading">FHSS operator workbench</h2><p>Synthetic software evidence only. Expected truth is evaluator-side and never receiver input.</p></div><button type="button" onClick={() => void refresh()}>Refresh all</button></div>
    <p role="status" aria-live="polite">{status}</p><div className="operation-grid">
      <article><h3>Receiver lifecycle</h3><div className="button-row"><button type="button" onClick={() => void command('Rebuild', '/api/v1/fhss/config/rebuild', { expected_revision: revision, command_id: `ui-rebuild-${Date.now()}` })}>Rebuild</button><button type="button" onClick={() => void command('Start', '/api/v1/fhss/commands/start', { command_id: `ui-start-${Date.now()}` })}>Start</button><button type="button" onClick={() => void command('Stop', '/api/v1/fhss/commands/stop', { command_id: `ui-stop-${Date.now()}` })}>Stop</button></div><ResourcePanel name="runtime_status" value={data.status} /></article>
      <article><h3>Configuration</h3><label htmlFor="configuration-patch">RFC 6902 patch</label><textarea id="configuration-patch" rows={5} value={patch} onChange={(event) => setPatch(event.target.value)} /><div className="button-row"><button type="button" disabled={!configurationEtag} onClick={() => { try { void command('Validate configuration', '/api/v1/fhss/config/validate', JSON.parse(patch) as unknown, { 'Content-Type': 'application/json-patch+json', 'If-Match': configurationEtag }); } catch (error) { setStatus(`Malformed patch: ${String(error)}`); } }}>Validate</button><button type="button" disabled={!configurationEtag} onClick={() => { try { void command('Apply configuration', '/api/v1/fhss/config', JSON.parse(patch) as unknown, { 'Content-Type': 'application/json-patch+json', 'If-Match': configurationEtag }, 'PATCH'); } catch (error) { setStatus(`Malformed patch: ${String(error)}`); } }}>Apply with captured If-Match</button></div><p>Captured authoritative ETag: <code>{configurationEtag || 'unavailable'}</code></p><ResourcePanel name="effective_configuration" value={configuration} /><ResourcePanel name="configuration_provenance" value={data.provenance} /></article>
      <article><h3>Complete-message jobs</h3><div className="button-row"><button type="button" onClick={() => void command('Step one complete message', '/api/v1/fhss/commands/step', { request_id: `ui-step-${Date.now()}` }, { 'Content-Type': 'application/json', 'Idempotency-Key': `ui-step-${Date.now()}` })}>Step message</button><button type="button" onClick={() => void command('Continue complete messages', '/api/v1/fhss/commands/continue', { request_id: `ui-continue-${Date.now()}`, message_count: 2 }, { 'Content-Type': 'application/json', 'Idempotency-Key': `ui-continue-${Date.now()}` })}>Continue 2</button><button type="button" onClick={() => void command('Reset jobs', '/api/v1/fhss/commands/reset', {})}>Reset</button><button type="button" disabled={!jobId} onClick={() => void command('Cancel job', `/api/v1/fhss/jobs/${jobId}/cancel`, {})}>Cancel job</button></div><label htmlFor="selected-job-id">Selected/completed job identity</label><input id="selected-job-id" value={jobId} onChange={(event) => setJobId(event.target.value)} placeholder="j-…" /><div className="state-list">{jobs?.entries.map((job) => <button type="button" key={String(job.job_id)} aria-pressed={jobId === job.job_id} onClick={() => setJobId(String(job.job_id))}><strong>{String(job.job_id)}</strong><span>{String(job.state)}</span></button>)}</div><ResourcePanel name="job_history" value={data.jobs} /></article>
      <ReceiverEvidence expected={expectedTruth} observation={observations} comparison={comparison} spectrum={spectrum} channel={spectrumChannel} onChannel={setSpectrumChannel} />
      <MetricsActivity value={metrics} /><Visualization value={data.visualization} />
      <article><h3>Investigation workflow</h3><p>Reference-only export is the default. Replays invoke the truth-free binary-IQ receiver.</p><label htmlFor="bundle-name">Bundle name</label><input id="bundle-name" value={bundleName} onChange={(event) => setBundleName(event.target.value)} /><p>Completed job: <code>{jobId || 'none selected'}</code></p><label><input className="inline-control" type="checkbox" checked={copyIq} onChange={(event) => { setCopyIq(event.target.checked); if (!event.target.checked) setConfirmCopy(false); }} /> Copy IQ into self-contained bundle</label><label><input className="inline-control" type="checkbox" disabled={!copyIq} checked={confirmCopy} onChange={(event) => setConfirmCopy(event.target.checked)} /> Confirm copied-IQ storage</label><label htmlFor="operation-id">Operation identity to cancel</label><input id="operation-id" value={operationId} onChange={(event) => setOperationId(event.target.value)} placeholder="op-…" /><div className="button-row"><button type="button" disabled={!jobId || (copyIq && !confirmCopy)} onClick={() => void command('Export investigation bundle', '/api/v1/fhss/investigations/exports', { request_id: `ui-export-${Date.now()}`, bundle_name: bundleName, job_id: jobId, iq_mode: copyIq ? 'copy' : 'reference', ...(copyIq ? { confirm_copy: confirmCopy } : {}) }, { 'Content-Type': 'application/json', 'Idempotency-Key': `ui-export-${Date.now()}` })}>Export</button><button type="button" onClick={() => void command('Validate bundle', '/api/v1/fhss/investigations/import-validations', { request_id: `ui-validate-${Date.now()}`, bundle_name: bundleName }, { 'Content-Type': 'application/json', 'Idempotency-Key': `ui-validate-${Date.now()}` })}>Validate</button><button type="button" onClick={() => void command('Replay bundle', '/api/v1/fhss/investigations/replays', { request_id: `ui-replay-${Date.now()}`, bundle_name: bundleName }, { 'Content-Type': 'application/json', 'Idempotency-Key': `ui-replay-${Date.now()}` })}>Replay</button><button type="button" disabled={!operationId} onClick={() => void command('Cancel operation', `/api/v1/fhss/investigations/operations/${operationId}/cancel`, {})}>Cancel operation</button></div><div className="state-list">{investigations?.entries.map((operation) => <button type="button" key={String(operation.operation_id)} aria-pressed={operationId === operation.operation_id} onClick={() => setOperationId(String(operation.operation_id))}><strong>{String(operation.operation_id)}</strong><span>{String(operation.state)}</span></button>)}</div><ResourcePanel name="investigation_operations" value={data.investigations} /></article>
      <article><h3>Health and evidence</h3><ResourcePanel name="health" value={data.health} /><ResourcePanel name="readiness" value={data.readiness} /><ResourcePanel name="diagnostics" value={data.diagnostics} /><ResourcePanel name="coherent_snapshot" value={data.snapshot} /></article>
    </div></section>;
}
