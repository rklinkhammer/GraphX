import { parseGraphResource, type GraphResource, type JsonObject } from './domain';

const API = '/api/v1/fhss';

export interface ConfigurationResource extends JsonObject {
  schema: string; config_revision: number; etag: string; effective: JsonObject;
  derived_paths: string[]; httpEtag: string;
}
export interface RuntimeResource extends JsonObject {
  schema: string; lifecycle_state: string; active_generation: number; active_run_epoch: number;
  active_config_revision: number; active_config_etag: string; config_revision: number; etag: string;
}
export interface MetricsResource extends JsonObject {
  schema: string; active_generation: number; active_run_epoch: number;
  active_config_revision: number; active_config_etag: string; capture_id: string;
  sampled_at_monotonic_ms: number; collection_interval: JsonObject;
  graph: JsonObject; nodes: JsonObject[]; edges: JsonObject[];
}
export interface DiagnosticsResource extends JsonObject {
  schema: string; active_generation: number; active_run_epoch: number;
  active_config_revision: number; active_config_etag: string; capture_id: string;
  nodes: JsonObject[];
}
export interface ExpectedTruthResource extends JsonObject { schema: string; messages: unknown[]; pulses: unknown[]; expected_receiver_message: unknown }
export interface ObservationResource extends JsonObject { schema: string; availability: JsonObject; observed_pulses: unknown[]; receiver_message_result: unknown }
export interface ComparisonResource extends JsonObject { schema: string; evaluation_state: string; availability: JsonObject; matches: unknown[] }
export interface SpectrumResource extends JsonObject { schema: string; channel_index: number | null; availability: JsonObject; bins: unknown[] }
export interface JobHistoryResource extends JsonObject { schema: string; controller_epoch: number; entries: JsonObject[]; bounds: JsonObject }
export interface InvestigationHistoryResource extends JsonObject { schema: string; entries: JsonObject[]; bounds: JsonObject }
export interface VisualizationResource extends JsonObject { schema: string; fixture_label: string; schedule: JsonObject; heatmap: JsonObject; timeline: JsonObject }
export interface SnapshotResource extends JsonObject {
  schema: string; publisher_epoch: string; latest_sequence: number;
  config_revision: number; config_etag: string; generation: number; run_epoch: number;
  graph: GraphResource; configuration: ConfigurationResource; runtime: RuntimeResource;
  metrics: MetricsResource; diagnostics: DiagnosticsResource; coherence: JsonObject; transport: JsonObject;
}
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

function exactFields(value: JsonObject, allowed: readonly string[], label: string): void {
  const permitted = new Set(allowed);
  const unexpected = Object.keys(value).find((field) => !permitted.has(field));
  if (unexpected) throw new Error(`${label} contains unexpected field ${unexpected}`);
}

function availability(value: unknown, label: string, extras: readonly string[] = []): JsonObject {
  const item = object(value, label);
  exactFields(item, ['state', 'reason', ...extras], label);
  const state = enumField(item, 'state', ['available', 'unavailable'], label);
  if (state === 'available' && item.reason !== null) throw new Error(`${label}.reason must be null when available`);
  if (state === 'unavailable' && (typeof item.reason !== 'string' || item.reason.length === 0)) throw new Error(`${label}.reason is required when unavailable`);
  return item;
}

const EXPECTED_METRICS = [
  'graph:total_items_processed', 'graph:total_items_rejected', 'graph:total_messages_processed',
  'graph:graph_total_enqueued', 'graph:graph_total_dequeued', 'graph:backpressure_events',
  'graph:peak_queue_depth', 'graph:peak_active_threads',
  'node:inbound_messages', 'node:outbound_messages', 'node:rejected_messages',
  'node:backpressure_events', 'node:peak_queue_depth', 'node:connected_edges',
  'node:diagnostics_available', 'node:activity_state',
  'edge:messages_enqueued', 'edge:messages_dequeued', 'edge:messages_rejected',
  'edge:backpressure_events', 'edge:current_queue_depth', 'edge:peak_queue_depth',
  'edge:transfer_service_duration', 'edge:initialized', 'edge:started', 'edge:thread_active',
  'edge:activity_state', 'edge:queue_residence_duration', 'edge:node_processing_duration',
  'edge:end_to_end_duration', 'edge:dashboard_delivery_duration',
] as const;

function parseMetricDefinition(value: unknown, index: number): JsonObject {
  const label = `metrics.metric_definitions[${index}]`;
  const item = object(value, label);
  exactFields(item, ['name', 'field', 'scope', 'kind', 'unit', 'monotonic', 'availability', 'capture', 'reset', 'aggregation', 'overflow', 'numeric_representation'], label);
  for (const field of ['name', 'field', 'capture', 'aggregation']) {
    if (stringField(item, field, label).length === 0) throw new Error(`${label}.${field} must not be empty`);
  }
  enumField(item, 'scope', ['graph', 'node', 'edge'], label);
  enumField(item, 'kind', ['counter', 'gauge', 'distribution', 'state'], label);
  enumField(item, 'unit', ['item', 'message', 'event', 'thread', 'edge', 'boolean', 'state', 'nanosecond'], label);
  enumField(item, 'availability', ['explicit'], label);
  enumField(item, 'reset', ['new_runtime_manager', 'sample_replaced', 'not_collected'], label);
  enumField(item, 'overflow', ['unavailable_above_javascript_safe_integer', 'not_applicable'], label);
  enumField(item, 'numeric_representation', ['non_negative_javascript_safe_integer', 'boolean', 'enumerated_string', 'structured_duration'], label);
  booleanField(item, 'monotonic', label);
  const expectedField = `/${item.scope}${item.scope === 'graph' ? '' : 's/*'}/${item.name}`;
  if (item.field !== expectedField) throw new Error(`${label}.field does not identify its metric`);
  return item;
}

function parseMetricNode(value: unknown, index: number): JsonObject {
  const label = `metrics.nodes[${index}]`;
  const item = object(value, label);
  exactFields(item, ['node_id', 'availability', 'unavailable_reason', 'node_index', 'name', 'type', 'inbound_messages', 'outbound_messages', 'rejected_messages', 'backpressure_events', 'peak_queue_depth', 'connected_edges', 'diagnostics_available', 'activity_state'], label);
  if (stringField(item, 'node_id', label).length === 0) throw new Error(`${label}.node_id must not be empty`);
  const state = enumField(item, 'availability', ['available', 'unavailable'], label);
  if (state === 'available' ? item.unavailable_reason !== null : typeof item.unavailable_reason !== 'string') throw new Error(`${label}.unavailable_reason disagrees with availability`);
  numberField(item, 'node_index', label);
  stringField(item, 'name', label); stringField(item, 'type', label); booleanField(item, 'diagnostics_available', label);
  const activity = enumField(item, 'activity_state', ['idle', 'initialized', 'started', 'active', 'backpressured', 'running', 'unavailable'], label);
  if (state === 'unavailable' && activity !== 'unavailable') throw new Error(`${label}.activity_state must be unavailable`);
  for (const field of ['inbound_messages', 'outbound_messages', 'rejected_messages', 'backpressure_events', 'peak_queue_depth', 'connected_edges']) {
    if (state === 'available') numberField(item, field, label);
    else if (item[field] !== null) throw new Error(`${label}.${field} must be null when unavailable`);
  }
  return item;
}

function parseDuration(value: unknown, label: string, transferService: boolean): JsonObject {
  const item = object(value, label);
  exactFields(item, ['availability', 'reason', 'clock', 'start_event', 'end_event', 'unit', 'count', 'cumulative_total'], label);
  const state = enumField(item, 'availability', ['available', 'unavailable'], label);
  if (stringField(item, 'unit', label) !== 'nanosecond') throw new Error(`${label}.unit is invalid`);
  if (state === 'available') {
    if (item.reason !== null || item.clock !== 'steady_clock') throw new Error(`${label} available timing metadata is invalid`);
    if (!transferService) throw new Error(`${label} is not instrumented and cannot be available`);
    if (stringField(item, 'start_event', label) !== 'DynamicEdge TransferTo call entry'
        || stringField(item, 'end_event', label) !== 'DynamicEdge TransferTo call return') {
      throw new Error(`${label} transfer/service event boundaries are invalid`);
    }
    numberField(item, 'count', label); numberField(item, 'cumulative_total', label);
  } else {
    if (typeof item.reason !== 'string' || item.reason.length === 0) throw new Error(`${label}.reason is required`);
    for (const field of ['clock', 'start_event', 'end_event', 'count', 'cumulative_total']) if (item[field] !== null) throw new Error(`${label}.${field} must be null`);
  }
  return item;
}

function parseMetricEdge(value: unknown, index: number): JsonObject {
  const label = `metrics.edges[${index}]`;
  const item = object(value, label);
  exactFields(item, ['edge_id', 'source_node_id', 'source_port', 'destination_node_id', 'destination_port', 'availability', 'unavailable_reason', 'edge_index', 'source_node_index', 'source_node_name', 'source_port_index', 'destination_node_index', 'destination_node_name', 'destination_port_index', 'message_type', 'messages_enqueued', 'messages_dequeued', 'messages_rejected', 'backpressure_events', 'current_queue_depth', 'current_queue_depth_availability', 'peak_queue_depth', 'transfer_service_duration', 'queue_residence_duration', 'node_processing_duration', 'end_to_end_duration', 'dashboard_delivery_duration', 'initialized', 'started', 'thread_active', 'activity_state'], label);
  for (const field of ['edge_id', 'source_node_id', 'destination_node_id']) {
    if (stringField(item, field, label).length === 0) throw new Error(`${label}.${field} must not be empty`);
  }
  for (const field of ['source_node_name', 'destination_node_name', 'message_type']) stringField(item, field, label);
  const state = enumField(item, 'availability', ['available', 'unavailable'], label);
  if (state === 'available' ? item.unavailable_reason !== null : typeof item.unavailable_reason !== 'string') throw new Error(`${label}.unavailable_reason disagrees with availability`);
  for (const field of ['source_port', 'destination_port', 'edge_index', 'source_node_index', 'source_port_index', 'destination_node_index', 'destination_port_index']) numberField(item, field, label);
  for (const field of ['messages_enqueued', 'messages_dequeued', 'messages_rejected', 'backpressure_events', 'peak_queue_depth']) {
    if (state === 'available') numberField(item, field, label);
    else if (item[field] !== null) throw new Error(`${label}.${field} must be null when unavailable`);
  }
  const depthAvailability = availability(item.current_queue_depth_availability, `${label}.current_queue_depth_availability`);
  if (depthAvailability.state === 'available') numberField(item, 'current_queue_depth', label);
  else if (item.current_queue_depth !== null) throw new Error(`${label}.current_queue_depth must be null when unavailable`);
  for (const field of ['initialized', 'started', 'thread_active']) booleanField(item, field, label);
  const activity = enumField(item, 'activity_state', ['idle', 'initialized', 'started', 'active', 'backpressured', 'running', 'unavailable'], label);
  if (state === 'unavailable' && (activity !== 'unavailable' || depthAvailability.state !== 'unavailable')) throw new Error(`${label} unavailable edge state is contradictory`);
  if (state === 'available' && depthAvailability.state === 'available'
      && Number(item.current_queue_depth) > Number(item.peak_queue_depth)) {
    throw new Error(`${label}.current_queue_depth exceeds peak_queue_depth`);
  }
  const expected = `${item.source_node_id}:${item.source_port}->${item.destination_node_id}:${item.destination_port}`;
  if (item.edge_id !== expected) throw new Error(`${label}.edge_id disagrees with its canonical endpoints`);
  for (const field of ['transfer_service_duration', 'queue_residence_duration', 'node_processing_duration', 'end_to_end_duration', 'dashboard_delivery_duration']) {
    parseDuration(item[field], `${label}.${field}`, field === 'transfer_service_duration');
  }
  return item;
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
    const fields = ['owner', 'config_revision', 'etag', 'effective', 'derived_paths'];
    const item = schema(value, 'graphx.dashboard.config.v1', fields, 'configuration');
    exactFields(item, ['schema', ...fields], 'configuration');
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
    exactFields(item, ['schema', ...required], 'runtime');
    const state = enumField(item, 'lifecycle_state', ['initializing', 'not_built', 'stopped', 'starting', 'running', 'completed', 'failed', 'rebuilding', 'cleanup_failed', 'shutting_down', 'dead'], 'runtime');
    for (const field of ['ready', 'rebuild_allowed', 'rebuild_blocked', 'rebuild_required', 'configuration_stale', 'stop_requested']) booleanField(item, field, 'runtime');
    for (const field of ['active_generation', 'active_run_epoch', 'rebuild_attempts', 'successful_rebuilds', 'active_config_revision', 'config_revision']) numberField(item, field, 'runtime');
    stringField(item, 'active_config_etag', 'runtime'); stringField(item, 'etag', 'runtime');
    for (const field of ['started_at', 'terminal_at']) nullableStringField(item, field, 'runtime');
    for (const field of ['terminal_result', 'last_error']) if (item[field] !== null) object(item[field], `runtime.${field}`);
    return { ...item, schema: String(item.schema), lifecycle_state: state,
      active_generation: Number(item.active_generation), active_run_epoch: Number(item.active_run_epoch),
      active_config_revision: Number(item.active_config_revision), active_config_etag: String(item.active_config_etag),
      config_revision: Number(item.config_revision), etag: String(item.etag) } as RuntimeResource;
  },
  metrics(value: unknown): MetricsResource {
    const fields = ['active_generation', 'active_run_epoch', 'active_config_revision', 'active_config_etag', 'capture_id', 'sampled_at_monotonic_ms', 'collection_interval', 'rate_availability', 'qualified_rates', 'identity_availability', 'metric_definitions', 'graph', 'nodes', 'edges'];
    const item = schema(value, 'graphx.dashboard.metrics.v1', fields, 'metrics');
    exactFields(item, ['schema', ...fields], 'metrics');
    for (const field of ['active_generation', 'active_run_epoch', 'active_config_revision', 'sampled_at_monotonic_ms']) numberField(item, field, 'metrics');
    stringField(item, 'active_config_etag', 'metrics');
    if (stringField(item, 'capture_id', 'metrics').length === 0) throw new Error('metrics.capture_id must not be empty');
    const identity = availability(item.identity_availability, 'metrics.identity_availability', ['canonical_node_count', 'canonical_edge_count']);
    for (const field of ['canonical_node_count', 'canonical_edge_count']) {
      if (identity[field] !== undefined) numberField(identity, field, 'metrics.identity_availability');
    }
    availability(item.rate_availability, 'metrics.rate_availability');
    const interval = availability(item.collection_interval, 'metrics.collection_interval', ['clock', 'duration_ms']);
    if (interval.clock !== 'steady_clock') throw new Error('metrics.collection_interval.clock must be steady_clock');
    if (interval.state === 'available') positiveIntegerField(interval, 'duration_ms', 'metrics.collection_interval');
    else if (interval.duration_ms !== null) throw new Error('metrics.collection_interval.duration_ms must be null when unavailable');
    const rates = objectArrayField(item, 'qualified_rates', 'metrics');
    if (item.rate_availability && (item.rate_availability as JsonObject).state === 'available') {
      if (rates.length !== 2) throw new Error('metrics.qualified_rates must contain both graph rates when available');
      for (const [index, rate] of rates.entries()) {
        const label = `metrics.qualified_rates[${index}]`;
        exactFields(rate, ['name', 'scope', 'unit', 'interval_ms', 'value'], label);
        enumField(rate, 'name', ['graph_total_enqueued', 'graph_total_dequeued'], label);
        if (stringField(rate, 'scope', label) !== 'graph' || stringField(rate, 'unit', label) !== 'messages_per_second') throw new Error(`${label} qualification is invalid`);
        if (positiveIntegerField(rate, 'interval_ms', label) !== interval.duration_ms) throw new Error(`${label}.interval_ms disagrees with collection interval`);
        if (typeof rate.value !== 'number' || !Number.isFinite(rate.value) || rate.value < 0) throw new Error(`${label}.value must be finite and non-negative`);
      }
      if (new Set(rates.map((rate) => rate.name)).size !== 2) throw new Error('metrics.qualified_rates contains duplicate rate names');
    } else if (rates.length !== 0) throw new Error('metrics.qualified_rates must be empty when rates are unavailable');
    const definitions = arrayField(item, 'metric_definitions', 'metrics').map(parseMetricDefinition);
    if (definitions.length !== EXPECTED_METRICS.length) throw new Error(`metrics.metric_definitions must contain exactly ${EXPECTED_METRICS.length} entries`);
    const definitionKeys = new Set(definitions.map((definition) => `${definition.scope}:${definition.name}`));
    if (definitionKeys.size !== definitions.length) throw new Error('metrics.metric_definitions contains a duplicate scope/name');
    for (const key of EXPECTED_METRICS) if (!definitionKeys.has(key)) throw new Error(`metrics.metric_definitions is missing ${key}`);
    const graph = object(item.graph, 'metrics.graph');
    exactFields(graph, ['availability', 'unavailable_reason', 'total_items_processed', 'total_items_rejected', 'total_messages_processed', 'graph_total_enqueued', 'graph_total_dequeued', 'backpressure_events', 'peak_queue_depth', 'peak_active_threads'], 'metrics.graph');
    const graphState = enumField(graph, 'availability', ['available', 'unavailable'], 'metrics.graph');
    if (graphState === 'available' ? graph.unavailable_reason !== null : typeof graph.unavailable_reason !== 'string') throw new Error('metrics.graph.unavailable_reason disagrees with availability');
    for (const field of ['total_items_processed', 'total_items_rejected', 'total_messages_processed', 'graph_total_enqueued', 'graph_total_dequeued', 'backpressure_events', 'peak_queue_depth', 'peak_active_threads']) {
      if (graphState === 'available') numberField(graph, field, 'metrics.graph');
      else if (graph[field] !== null) throw new Error(`metrics.graph.${field} must be null when unavailable`);
    }
    const nodeRecords = arrayField(item, 'nodes', 'metrics');
    const edgeRecords = arrayField(item, 'edges', 'metrics');
    if (nodeRecords.length > 256 || edgeRecords.length > 512) throw new Error('metrics record arrays exceed bounded graph limits');
    const nodes = nodeRecords.map(parseMetricNode);
    const edges = edgeRecords.map(parseMetricEdge);
    if (new Set(nodes.map((node) => node.node_id)).size !== nodes.length) throw new Error('metrics.nodes contains duplicate canonical node_id');
    if (new Set(edges.map((edge) => edge.edge_id)).size !== edges.length) throw new Error('metrics.edges contains duplicate canonical edge_id');
    return { ...item, schema: String(item.schema), active_generation: Number(item.active_generation), active_run_epoch: Number(item.active_run_epoch), active_config_revision: Number(item.active_config_revision), active_config_etag: String(item.active_config_etag), capture_id: String(item.capture_id), graph, nodes, edges } as MetricsResource;
  },
  diagnostics(value: unknown): DiagnosticsResource {
    const fields = ['active_generation', 'active_run_epoch', 'active_config_revision', 'active_config_etag', 'capture_id', 'sampled_at_monotonic_ms', 'identity_availability', 'nodes'];
    const item = schema(value, 'graphx.dashboard.diagnostics.v1', fields, 'diagnostics');
    exactFields(item, ['schema', ...fields], 'diagnostics');
    for (const field of ['active_generation', 'active_run_epoch', 'active_config_revision', 'sampled_at_monotonic_ms']) numberField(item, field, 'diagnostics');
    stringField(item, 'active_config_etag', 'diagnostics');
    if (stringField(item, 'capture_id', 'diagnostics').length === 0) throw new Error('diagnostics.capture_id must not be empty');
    availability(item.identity_availability, 'diagnostics.identity_availability');
    const diagnosticRecords = arrayField(item, 'nodes', 'diagnostics');
    if (diagnosticRecords.length > 256) throw new Error('diagnostics.nodes exceeds bounded graph limits');
    const nodes = diagnosticRecords.map((entry, index) => {
      const label = `diagnostics.nodes[${index}]`; const node = object(entry, label);
      exactFields(node, ['node_id', 'availability', 'unavailable_reason', 'node_index', 'name', 'type', 'diagnostics'], label);
      if (stringField(node, 'node_id', label).length === 0) throw new Error(`${label}.node_id must not be empty`);
      const state = enumField(node, 'availability', ['available', 'unavailable'], label); numberField(node, 'node_index', label);
      stringField(node, 'name', label); stringField(node, 'type', label);
      if (state === 'available') {
        if (node.unavailable_reason !== null) throw new Error(`${label}.unavailable_reason must be null when available`);
        object(node.diagnostics, `${label}.diagnostics`);
      } else if (typeof node.unavailable_reason !== 'string' || node.unavailable_reason.length === 0 || node.diagnostics !== null) {
        throw new Error(`${label} unavailable diagnostics contract is invalid`);
      }
      return node;
    });
    if (new Set(nodes.map((node) => node.node_id)).size !== nodes.length) throw new Error('diagnostics.nodes contains duplicate canonical node_id');
    return { ...item, schema: String(item.schema), active_generation: Number(item.active_generation), active_run_epoch: Number(item.active_run_epoch), active_config_revision: Number(item.active_config_revision), active_config_etag: String(item.active_config_etag), capture_id: String(item.capture_id), nodes } as DiagnosticsResource;
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
    const fields = ['publisher_epoch', 'latest_sequence', 'captured_at', 'config_revision', 'config_etag', 'generation', 'run_epoch', 'coherence', 'configuration', 'graph', 'runtime', 'metrics', 'transport', 'diagnostics'];
    const item = schema(value, 'graphx.dashboard.fhss_snapshot.v1', fields, 'snapshot');
    exactFields(item, ['schema', ...fields], 'snapshot');
    const publisherEpoch = stringField(item, 'publisher_epoch', 'snapshot'); if (!/^[0-9a-f]{32}$/.test(publisherEpoch)) throw new Error('snapshot.publisher_epoch is invalid');
    stringField(item, 'captured_at', 'snapshot'); const configEtag = stringField(item, 'config_etag', 'snapshot');
    for (const field of ['config_revision', 'generation', 'run_epoch']) numberField(item, field, 'snapshot');
    const configuration = parsers.configuration(item.configuration);
    const graph = parsers.graph(item.graph);
    const runtime = parsers.runtime(item.runtime);
    const metrics = parsers.metrics(item.metrics);
    const diagnostics = parsers.diagnostics(item.diagnostics);
    const coherence = object(item.coherence, 'snapshot.coherence');
    exactFields(coherence, ['state', 'metric_capture_id', 'diagnostic_capture_id'], 'snapshot.coherence');
    if (coherence.state !== 'coherent') throw new Error('snapshot.coherence.state must be coherent');
    const transport = object(item.transport, 'snapshot.transport');
    const transportFields = ['counter_availability', 'active_websocket_clients', 'pongs_received', 'idle_closes', 'protocol_failures', 'rejected_upgrades', 'replayed_events', 'resync_requests', 'queue_overflows', 'close_reasons', 'dropped_events_total', 'coalesced_events_total'];
    exactFields(transport, transportFields, 'snapshot.transport');
    const counterState = availability(transport.counter_availability, 'snapshot.transport.counter_availability').state;
    numberField(transport, 'active_websocket_clients', 'snapshot.transport');
    const counters = ['pongs_received', 'idle_closes', 'protocol_failures', 'rejected_upgrades', 'replayed_events', 'resync_requests', 'queue_overflows', 'dropped_events_total', 'coalesced_events_total'];
    for (const field of counters) {
      if (counterState === 'available') numberField(transport, field, 'snapshot.transport');
      else if (transport[field] !== null) throw new Error(`snapshot.transport.${field} must be null when counters are unavailable`);
    }
    const closeReasons = object(transport.close_reasons, 'snapshot.transport.close_reasons');
    exactFields(closeReasons, ['normal', 'protocol', 'unsupported_data', 'invalid_utf8', 'too_big', 'policy', 'going_away', 'internal'], 'snapshot.transport.close_reasons');
    for (const field of Object.keys(closeReasons)) {
      if (counterState === 'available') numberField(closeReasons, field, 'snapshot.transport.close_reasons');
      else if (closeReasons[field] !== null) throw new Error(`snapshot.transport.close_reasons.${field} must be null when counters are unavailable`);
    }
    const revision = Number(item.config_revision); const generation = Number(item.generation); const run = Number(item.run_epoch);
    if (configuration.config_revision !== revision || graph.config_revision !== revision || runtime.config_revision !== revision
        || runtime.active_config_revision !== revision || metrics.active_config_revision !== revision || diagnostics.active_config_revision !== revision
        || configuration.etag !== configEtag || graph.etag !== configEtag || runtime.etag !== configEtag
        || runtime.active_config_etag !== configEtag || metrics.active_config_etag !== configEtag || diagnostics.active_config_etag !== configEtag
        || runtime.active_generation !== generation || metrics.active_generation !== generation || diagnostics.active_generation !== generation
        || runtime.active_run_epoch !== run || metrics.active_run_epoch !== run || diagnostics.active_run_epoch !== run
        || coherence.metric_capture_id !== metrics.capture_id || coherence.diagnostic_capture_id !== diagnostics.capture_id) {
      throw new Error('snapshot resources do not share one complete configuration/runtime/capture identity tuple');
    }
    return { ...item, schema: String(item.schema), publisher_epoch: publisherEpoch, latest_sequence: numberField(item, 'latest_sequence', 'snapshot'),
      config_revision: revision, config_etag: configEtag, generation, run_epoch: run,
      graph, configuration, runtime, metrics, diagnostics, coherence, transport } as SnapshotResource;
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
