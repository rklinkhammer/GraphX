import { parseGraphResource, type GraphResource, type JsonObject } from './domain';

const API = '/api/v1/fhss';

export interface ConfigurationResource extends JsonObject {
  schema: string; config_revision: number; etag: string; effective: JsonObject;
  derived_paths: string[]; httpEtag: string;
}
export interface RuntimeResource extends JsonObject { schema: string; lifecycle_state: string; active_config_revision: number; active_config_etag: string }
export interface MetricsResource extends JsonObject { schema: string; graph: JsonObject; nodes: JsonObject[]; edges: JsonObject[] }
export interface DiagnosticsResource extends JsonObject { schema: string; nodes: JsonObject[] }
export interface ExpectedTruthResource extends JsonObject { schema: string; messages: unknown[]; pulses: unknown[]; expected_receiver_message: unknown }
export interface ObservationResource extends JsonObject { schema: string; availability: JsonObject; observed_pulses: unknown[]; receiver_message_result: unknown }
export interface ComparisonResource extends JsonObject { schema: string; evaluation_state: string; availability: JsonObject; matches: unknown[] }
export interface SpectrumResource extends JsonObject { schema: string; channel_index: number | null; availability: JsonObject; bins: unknown[] }
export interface JobHistoryResource extends JsonObject { schema: string; controller_epoch: number; entries: JsonObject[]; bounds: JsonObject }
export interface InvestigationHistoryResource extends JsonObject { schema: string; entries: JsonObject[]; bounds: JsonObject }
export interface VisualizationResource extends JsonObject { schema: string; fixture_label: string; schedule: JsonObject; heatmap: JsonObject; timeline: JsonObject }
export interface SnapshotResource extends JsonObject { schema: string; publisher_epoch: string; latest_sequence: number; graph: JsonObject; configuration: JsonObject; runtime: JsonObject; metrics: JsonObject; diagnostics: JsonObject }
export interface ProvenanceResource extends JsonObject { schema: string; config_revision: number; etag: string; provenance: JsonObject[] }
export interface HealthResource extends JsonObject { status: string }
export interface ReadinessResource extends JsonObject { ready: boolean; state: string }

function object(value: unknown, label: string): JsonObject {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error(`${label} must be an object`);
  return value as JsonObject;
}
function stringField(value: JsonObject, field: string, label: string): string {
  if (typeof value[field] !== 'string') throw new Error(`${label}.${field} must be a string`);
  return value[field] as string;
}
function numberField(value: JsonObject, field: string, label: string): number {
  if (!Number.isSafeInteger(value[field]) || Number(value[field]) < 0) throw new Error(`${label}.${field} must be a non-negative safe integer`);
  return Number(value[field]);
}
function positiveIntegerField(value: JsonObject, field: string, label: string): number {
  const result = numberField(value, field, label);
  if (result < 1) throw new Error(`${label}.${field} must be a positive safe integer`);
  return result;
}
function booleanField(value: JsonObject, field: string, label: string): boolean {
  if (typeof value[field] !== 'boolean') throw new Error(`${label}.${field} must be a boolean`);
  return value[field] as boolean;
}
function arrayField(value: JsonObject, field: string, label: string): unknown[] {
  if (!Array.isArray(value[field])) throw new Error(`${label}.${field} must be an array`);
  return value[field] as unknown[];
}
function objectArrayField(value: JsonObject, field: string, label: string): JsonObject[] {
  return arrayField(value, field, label).map((entry, index) => object(entry, `${label}.${field}[${index}]`));
}
function stringArrayField(value: JsonObject, field: string, label: string): string[] {
  return arrayField(value, field, label).map((entry, index) => {
    if (typeof entry !== 'string') throw new Error(`${label}.${field}[${index}] must be a string`);
    return entry;
  });
}
function enumField(value: JsonObject, field: string, allowed: readonly string[], label: string): string {
  const result = stringField(value, field, label);
  if (!allowed.includes(result)) throw new Error(`${label}.${field} is not an allowed value`);
  return result;
}
function nullableIntegerField(value: JsonObject, field: string, label: string): number | null {
  if (value[field] === null) return null;
  return numberField(value, field, label);
}
function nullableStringField(value: JsonObject, field: string, label: string): string | null {
  if (value[field] === null) return null;
  return stringField(value, field, label);
}
function hashField(value: JsonObject, field: string, label: string): string {
  const result = stringField(value, field, label);
  if (!/^[0-9a-f]{64}$/.test(result)) throw new Error(`${label}.${field} must be a lowercase SHA-256 digest`);
  return result;
}
function schema(value: unknown, expected: string, required: string[], label = expected): JsonObject {
  const result = object(value, label);
  if (result.schema !== expected) throw new Error(`${label}.schema must be ${expected}`);
  for (const field of required) if (!(field in result)) throw new Error(`${label} is missing ${field}`);
  return result;
}

export async function requestJson(path: string, init?: RequestInit): Promise<unknown> {
  return (await requestDocument(path, init)).body;
}

export async function requestDocument(path: string, init?: RequestInit): Promise<{ body: unknown; headers: Headers; status: number }> {
  const response = await fetch(path, init);
  const body: unknown = response.status === 204 ? null : await response.json();
  if (!response.ok) {
    const detail = body && typeof body === 'object' && 'detail' in body ? String((body as JsonObject).detail) : response.statusText;
    throw new Error(`${response.status}: ${detail}`);
  }
  return { body, headers: response.headers, status: response.status };
}

export const parsers = {
  graph: parseGraphResource,
  configuration(value: unknown, httpEtag = ''): ConfigurationResource {
    const item = schema(value, 'graphx.dashboard.config.v1', ['owner', 'config_revision', 'etag', 'effective', 'derived_paths'], 'configuration');
    stringField(item, 'owner', 'configuration');
    const bodyEtag = stringField(item, 'etag', 'configuration');
    const captured = httpEtag || bodyEtag;
    if (httpEtag && httpEtag !== bodyEtag) throw new Error('configuration HTTP ETag and body ETag disagree');
    return { ...item, schema: String(item.schema), config_revision: numberField(item, 'config_revision', 'configuration'), etag: bodyEtag,
      effective: object(item.effective, 'configuration.effective'), derived_paths: stringArrayField(item, 'derived_paths', 'configuration'), httpEtag: captured } as ConfigurationResource;
  },
  runtime(value: unknown): RuntimeResource {
    const required = ['lifecycle_state', 'ready', 'rebuild_allowed', 'rebuild_blocked', 'active_generation', 'active_run_epoch', 'rebuild_attempts', 'successful_rebuilds', 'active_config_revision', 'active_config_etag', 'config_revision', 'etag', 'rebuild_required', 'configuration_stale', 'stop_requested', 'started_at', 'terminal_at', 'terminal_result', 'last_error'];
    const item = schema(value, 'graphx.dashboard.runtime_status.v1', required, 'runtime');
    const state = enumField(item, 'lifecycle_state', ['initializing', 'not_built', 'stopped', 'starting', 'running', 'completed', 'failed', 'rebuilding', 'cleanup_failed', 'shutting_down', 'dead'], 'runtime');
    for (const field of ['ready', 'rebuild_allowed', 'rebuild_blocked', 'rebuild_required', 'configuration_stale', 'stop_requested']) booleanField(item, field, 'runtime');
    for (const field of ['active_generation', 'active_run_epoch', 'rebuild_attempts', 'successful_rebuilds', 'active_config_revision', 'config_revision']) numberField(item, field, 'runtime');
    stringField(item, 'active_config_etag', 'runtime'); stringField(item, 'etag', 'runtime');
    for (const field of ['started_at', 'terminal_at']) nullableStringField(item, field, 'runtime');
    for (const field of ['terminal_result', 'last_error']) if (item[field] !== null) object(item[field], `runtime.${field}`);
    return { ...item, schema: String(item.schema), lifecycle_state: state, active_config_revision: Number(item.active_config_revision), active_config_etag: String(item.active_config_etag) } as RuntimeResource;
  },
  metrics(value: unknown): MetricsResource {
    const item = schema(value, 'graphx.dashboard.metrics.v1', ['active_generation', 'active_run_epoch', 'active_config_revision', 'active_config_etag', 'metric_definitions', 'graph', 'nodes', 'edges'], 'metrics');
    for (const field of ['active_generation', 'active_run_epoch', 'active_config_revision']) numberField(item, field, 'metrics');
    stringField(item, 'active_config_etag', 'metrics');
    if (objectArrayField(item, 'metric_definitions', 'metrics').length !== 19) throw new Error('metrics.metric_definitions must contain exactly 19 entries');
    return { ...item, schema: String(item.schema), graph: object(item.graph, 'metrics.graph'), nodes: arrayField(item, 'nodes', 'metrics').map((entry) => object(entry, 'metric node')), edges: arrayField(item, 'edges', 'metrics').map((entry) => object(entry, 'metric edge')) } as MetricsResource;
  },
  diagnostics(value: unknown): DiagnosticsResource {
    const item = schema(value, 'graphx.dashboard.diagnostics.v1', ['active_generation', 'active_run_epoch', 'active_config_revision', 'active_config_etag', 'nodes'], 'diagnostics');
    for (const field of ['active_generation', 'active_run_epoch', 'active_config_revision']) numberField(item, field, 'diagnostics');
    stringField(item, 'active_config_etag', 'diagnostics');
    return { ...item, schema: String(item.schema), nodes: arrayField(item, 'nodes', 'diagnostics').map((entry) => object(entry, 'diagnostic node')) } as DiagnosticsResource;
  },
  expectedTruth(value: unknown): ExpectedTruthResource {
    const item = schema(value, 'graphx.dashboard.fhss_expected_truth.v1', ['semantic_class', 'scenario_id', 'config_revision', 'config_etag', 'timing_basis', 'messages', 'pulses', 'expected_receiver_message', 'synthetic_impairments', 'bounds', 'truth_sha256'], 'expected truth');
    if (stringField(item, 'semantic_class', 'expected truth') !== 'expected') throw new Error('expected truth.semantic_class must be expected');
    stringField(item, 'scenario_id', 'expected truth'); numberField(item, 'config_revision', 'expected truth'); stringField(item, 'config_etag', 'expected truth');
    for (const field of ['timing_basis', 'synthetic_impairments', 'bounds']) object(item[field], `expected truth.${field}`);
    object(item.expected_receiver_message, 'expected truth.expected_receiver_message'); hashField(item, 'truth_sha256', 'expected truth');
    return { ...item, schema: String(item.schema), messages: arrayField(item, 'messages', 'expected truth'), pulses: arrayField(item, 'pulses', 'expected truth'), expected_receiver_message: item.expected_receiver_message } as ExpectedTruthResource;
  },
  observation(value: unknown): ObservationResource {
    const required = ['semantic_class', 'generation', 'run_epoch', 'config_revision', 'config_etag', 'observation_id', 'availability', 'timing_basis', 'sample_rate', 'observed_pulses', 'detected_count', 'rejected_count', 'count_availability', 'count_semantics', 'rejection_reason_codes', 'preamble', 'receiver_derived_active_frequencies', 'assembler', 'receiver_message_result', 'terminal_result', 'sources', 'provenance', 'truncation', 'observation_sha256'];
    const item = schema(value, 'graphx.dashboard.fhss_receiver_observation.v1', required, 'observation');
    if (stringField(item, 'semantic_class', 'observation') !== 'observed') throw new Error('observation.semantic_class must be observed');
    for (const field of ['generation', 'run_epoch', 'config_revision']) numberField(item, field, 'observation');
    stringField(item, 'config_etag', 'observation'); stringField(item, 'observation_id', 'observation');
    for (const field of ['availability', 'timing_basis', 'sample_rate', 'count_availability', 'count_semantics', 'preamble', 'receiver_derived_active_frequencies', 'assembler', 'truncation']) object(item[field], `observation.${field}`);
    nullableIntegerField(item, 'detected_count', 'observation'); nullableIntegerField(item, 'rejected_count', 'observation');
    stringArrayField(item, 'rejection_reason_codes', 'observation'); objectArrayField(item, 'sources', 'observation'); objectArrayField(item, 'provenance', 'observation');
    if (item.receiver_message_result !== null) object(item.receiver_message_result, 'observation.receiver_message_result');
    if (item.terminal_result !== null) object(item.terminal_result, 'observation.terminal_result');
    hashField(item, 'observation_sha256', 'observation');
    return { ...item, schema: String(item.schema), availability: object(item.availability, 'observation.availability'), observed_pulses: arrayField(item, 'observed_pulses', 'observation'), receiver_message_result: item.receiver_message_result } as ObservationResource;
  },
  comparison(value: unknown): ComparisonResource {
    const required = ['semantic_class', 'evaluation_state', 'expected_truth_sha256', 'receiver_observation_sha256', 'generation', 'run_epoch', 'config_identity', 'algorithm', 'availability', 'matches', 'missed_expected_indices', 'unexpected_observed_indices', 'ambiguous', 'terminal_result_agrees', 'execution_lifecycle', 'comparison_sha256'];
    const item = schema(value, 'graphx.dashboard.fhss_comparison_result.v1', required, 'comparison');
    if (stringField(item, 'semantic_class', 'comparison') !== 'comparison') throw new Error('comparison.semantic_class must be comparison');
    const evaluationState = enumField(item, 'evaluation_state', ['evaluated', 'indeterminate'], 'comparison');
    nullableStringField(item, 'expected_truth_sha256', 'comparison'); nullableStringField(item, 'receiver_observation_sha256', 'comparison');
    nullableIntegerField(item, 'generation', 'comparison'); nullableIntegerField(item, 'run_epoch', 'comparison');
    for (const field of ['config_identity', 'algorithm', 'availability', 'execution_lifecycle']) object(item[field], `comparison.${field}`);
    for (const field of ['matches', 'missed_expected_indices', 'unexpected_observed_indices', 'ambiguous']) arrayField(item, field, 'comparison');
    if (item.terminal_result_agrees !== null) booleanField(item, 'terminal_result_agrees', 'comparison');
    hashField(item, 'comparison_sha256', 'comparison');
    return { ...item, schema: String(item.schema), evaluation_state: evaluationState, availability: object(item.availability, 'comparison.availability'), matches: arrayField(item, 'matches', 'comparison') } as ComparisonResource;
  },
  spectrum(value: unknown): SpectrumResource {
    const item = schema(value, 'graphx.dashboard.fhss_receiver_spectrum.v1', ['semantic_class', 'generation', 'run_epoch', 'config_revision', 'config_etag', 'channel_index', 'availability', 'bins'], 'spectrum');
    enumField(item, 'semantic_class', ['observed', 'unavailable'], 'spectrum');
    for (const field of ['generation', 'run_epoch', 'config_revision']) numberField(item, field, 'spectrum');
    stringField(item, 'config_etag', 'spectrum');
    if (item.channel_index !== null && (!Number.isInteger(item.channel_index) || Number(item.channel_index) < 0 || Number(item.channel_index) > 63)) throw new Error('spectrum.channel_index is invalid');
    return { ...item, schema: String(item.schema), channel_index: item.channel_index === null ? null : Number(item.channel_index), availability: object(item.availability, 'spectrum.availability'), bins: arrayField(item, 'bins', 'spectrum') } as SpectrumResource;
  },
  jobs(value: unknown): JobHistoryResource {
    const item = schema(value, 'graphx.dashboard.fhss_job_history.v1', ['controller_epoch', 'entries', 'bounds'], 'jobs');
    return { ...item, schema: String(item.schema), controller_epoch: positiveIntegerField(item, 'controller_epoch', 'jobs'), entries: objectArrayField(item, 'entries', 'jobs'), bounds: object(item.bounds, 'jobs.bounds') } as JobHistoryResource;
  },
  investigations(value: unknown): InvestigationHistoryResource {
    const item = schema(value, 'graphx.dashboard.fhss_investigation_operations.v1', ['entries', 'bounds'], 'investigations');
    return { ...item, schema: String(item.schema), entries: arrayField(item, 'entries', 'investigations').map((entry) => object(entry, 'operation')), bounds: object(item.bounds, 'investigations.bounds') } as InvestigationHistoryResource;
  },
  visualization(value: unknown): VisualizationResource {
    const item = schema(value, 'graphx.dashboard.fhss_visualization.v1', ['fixture_label', 'schedule', 'heatmap', 'timeline', 'bounds', 'config_revision'], 'visualization');
    object(item.bounds, 'visualization.bounds'); numberField(item, 'config_revision', 'visualization');
    return { ...item, schema: String(item.schema), fixture_label: stringField(item, 'fixture_label', 'visualization'), schedule: object(item.schedule, 'visualization.schedule'), heatmap: object(item.heatmap, 'visualization.heatmap'), timeline: object(item.timeline, 'visualization.timeline') } as VisualizationResource;
  },
  snapshot(value: unknown): SnapshotResource {
    const item = schema(value, 'graphx.dashboard.fhss_snapshot.v1', ['publisher_epoch', 'latest_sequence', 'captured_at', 'config_revision', 'config_etag', 'generation', 'run_epoch', 'configuration', 'graph', 'runtime', 'metrics', 'transport', 'diagnostics'], 'snapshot');
    const publisherEpoch = stringField(item, 'publisher_epoch', 'snapshot'); if (!/^[0-9a-f]{32}$/.test(publisherEpoch)) throw new Error('snapshot.publisher_epoch is invalid');
    stringField(item, 'captured_at', 'snapshot'); stringField(item, 'config_etag', 'snapshot');
    for (const field of ['config_revision', 'generation', 'run_epoch']) numberField(item, field, 'snapshot');
    object(item.transport, 'snapshot.transport');
    return { ...item, schema: String(item.schema), publisher_epoch: publisherEpoch, latest_sequence: numberField(item, 'latest_sequence', 'snapshot'), graph: object(item.graph, 'snapshot.graph'), configuration: object(item.configuration, 'snapshot.configuration'), runtime: object(item.runtime, 'snapshot.runtime'), metrics: object(item.metrics, 'snapshot.metrics'), diagnostics: object(item.diagnostics, 'snapshot.diagnostics') } as SnapshotResource;
  },
  provenance(value: unknown): ProvenanceResource {
    const item = schema(value, 'graphx.dashboard.configuration_provenance.v1', ['config_revision', 'etag', 'provenance'], 'provenance');
    return { ...item, schema: String(item.schema), config_revision: numberField(item, 'config_revision', 'provenance'), etag: stringField(item, 'etag', 'provenance'), provenance: objectArrayField(item, 'provenance', 'provenance') } as ProvenanceResource;
  },
  health(value: unknown): HealthResource { const item = object(value, 'health'); if (stringField(item, 'status', 'health') !== 'ok') throw new Error('health.status must be ok'); return { ...item, status: 'ok' } as HealthResource; },
  readiness(value: unknown): ReadinessResource { const item = object(value, 'readiness'); if (typeof item.ready !== 'boolean') throw new Error('readiness.ready must be boolean'); return { ...item, ready: item.ready, state: stringField(item, 'state', 'readiness') } as ReadinessResource; },
};

export async function getGraph(): Promise<GraphResource> { return parsers.graph(await requestJson(`${API}/graph`)); }
export async function getConfiguration(): Promise<ConfigurationResource> { const response = await requestDocument(`${API}/config/effective`); return parsers.configuration(response.body, response.headers.get('etag') ?? ''); }
export async function getSpectrum(channel?: number): Promise<SpectrumResource> { return parsers.spectrum(await requestJson(`${API}/spectrum${channel === undefined ? '' : `?channel=${channel}`}`)); }

export const resourceLoaders = {
  snapshot: async () => parsers.snapshot(await requestJson(`${API}/snapshot`)),
  status: async () => parsers.runtime(await requestJson(`${API}/status`)),
  configuration: getConfiguration,
  provenance: async () => parsers.provenance(await requestJson(`${API}/config/provenance`)),
  metrics: async () => parsers.metrics(await requestJson(`${API}/metrics`)),
  jobs: async () => parsers.jobs(await requestJson(`${API}/jobs`)),
  expectedTruth: async () => parsers.expectedTruth(await requestJson(`${API}/expected-truth`)),
  observations: async () => parsers.observation(await requestJson(`${API}/observations`)),
  comparison: async () => parsers.comparison(await requestJson(`${API}/comparison`)),
  spectrum: () => getSpectrum(),
  investigations: async () => parsers.investigations(await requestJson(`${API}/investigations/operations`)),
  visualization: async () => parsers.visualization(await requestJson(`${API}/visualization`)),
  health: async () => parsers.health(await requestJson('/healthz')),
  readiness: async () => parsers.readiness(await requestJson('/readyz')),
  diagnostics: async () => parsers.diagnostics(await requestJson(`${API}/diagnostics`)),
} as const;

export async function postCommand(path: string, body: JsonObject): Promise<unknown> {
  return requestJson(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}
