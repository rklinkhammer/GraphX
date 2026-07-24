import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { LoadStatePanel } from '../src/App';
import { getConfiguration, getSpectrum, parsers } from '../src/api';
import { inspectorEvidenceCoherent, QueueDepthSummary } from '../src/Inspector';
import { Operations } from '../src/Operations';

const schema = (name: string, fields: Record<string, unknown>) => ({ schema: name, ...fields });
const sha = (digit: string) => digit.repeat(64);
const config = schema('graphx.dashboard.config.v1', {
  owner: 'receiver', config_revision: 4, etag: '"graphx-config-4"',
  effective: { nodes: [], edges: [] }, derived_paths: [],
});
const metricKeys = [
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
];
const metrics = schema('graphx.dashboard.metrics.v1', {
  active_generation: 0, active_run_epoch: 0, active_config_revision: 0,
  active_config_etag: '', capture_id: 'inactive', sampled_at_monotonic_ms: 1,
  collection_interval: { state: 'unavailable', reason: 'no previous sample', clock: 'steady_clock', duration_ms: null },
  rate_availability: { state: 'unavailable', reason: 'no previous sample' },
  qualified_rates: [],
  identity_availability: { state: 'unavailable', reason: 'no runtime' },
  metric_definitions: metricKeys.map((key) => {
    const [scope, name] = key.split(':');
    return {
    name, field: `/${scope}${scope === 'graph' ? '' : 's/*'}/${name}`, scope,
    kind: name.includes('duration') ? 'distribution' : name === 'activity_state' || ['diagnostics_available', 'initialized', 'started', 'thread_active'].includes(name) ? 'state' : name.startsWith('peak_') || name === 'connected_edges' || name === 'current_queue_depth' ? 'gauge' : 'counter', unit: name.includes('duration') ? 'nanosecond' : 'message',
    monotonic: true, availability: 'explicit', capture: 'atomic sample',
    reset: name.includes('duration') && name !== 'transfer_service_duration' ? 'not_collected' : 'new_runtime_manager', aggregation: 'defined aggregation',
    overflow: name === 'activity_state' || ['diagnostics_available', 'initialized', 'started', 'thread_active'].includes(name) ? 'not_applicable' : 'unavailable_above_javascript_safe_integer',
    numeric_representation: name.includes('duration') ? 'structured_duration' : name === 'activity_state' ? 'enumerated_string' : ['diagnostics_available', 'initialized', 'started', 'thread_active'].includes(name) ? 'boolean' : 'non_negative_javascript_safe_integer',
  }; }),
  graph: {
    availability: 'unavailable', unavailable_reason: 'no runtime',
    total_items_processed: null, total_items_rejected: null, total_messages_processed: null,
    graph_total_enqueued: null, graph_total_dequeued: null, backpressure_events: null,
    peak_queue_depth: null, peak_active_threads: null,
  }, nodes: [], edges: [],
});
const expectedTruth = schema('graphx.dashboard.fhss_expected_truth.v1', {
  semantic_class: 'expected', scenario_id: 'synthetic-1', config_revision: 1,
  config_etag: '"graphx-config-1"', timing_basis: {}, messages: [], pulses: [],
  expected_receiver_message: {}, synthetic_impairments: {}, bounds: {}, truth_sha256: sha('0'),
});
const observation = schema('graphx.dashboard.fhss_receiver_observation.v1', {
  semantic_class: 'observed', generation: 0, run_epoch: 0, config_revision: 0,
  config_etag: '', observation_id: 'observation-g0-r0',
  availability: { state: 'available', reason: null },
  timing_basis: {}, sample_rate: {}, observed_pulses: [], detected_count: null,
  rejected_count: null, count_availability: {}, count_semantics: {},
  rejection_reason_codes: [], preamble: {}, receiver_derived_active_frequencies: {},
  assembler: {}, receiver_message_result: null, terminal_result: null, sources: [],
  provenance: [], truncation: {}, observation_sha256: sha('1'),
});
const comparison = schema('graphx.dashboard.fhss_comparison_result.v1', {
  semantic_class: 'comparison', evaluation_state: 'indeterminate',
  expected_truth_sha256: sha('0'), receiver_observation_sha256: sha('1'),
  generation: 0, run_epoch: 0, config_identity: {}, algorithm: {}, availability: {},
  matches: [], missed_expected_indices: [], unexpected_observed_indices: [],
  ambiguous: [], terminal_result_agrees: null, execution_lifecycle: {},
  comparison_sha256: sha('2'),
});
const spectrum = schema('graphx.dashboard.fhss_receiver_spectrum.v1', {
  semantic_class: 'observed', generation: 0, run_epoch: 0, config_revision: 0,
  config_etag: '', channel_index: 17, availability: {}, bins: [],
});
const graph = schema('graphx.dashboard.graph.v1', {
  owner: 'receiver', config_revision: 4, etag: '"graphx-config-4"',
  graph: { nodes: [], edges: [] },
});
const runtime = schema('graphx.dashboard.runtime_status.v1', {
  lifecycle_state: 'stopped', ready: true, rebuild_allowed: true,
  rebuild_blocked: false, active_generation: 7, active_run_epoch: 8,
  rebuild_attempts: 1, successful_rebuilds: 1, active_config_revision: 4,
  active_config_etag: '"graphx-config-4"', config_revision: 4,
  etag: '"graphx-config-4"', rebuild_required: false,
  configuration_stale: false, stop_requested: false, started_at: null,
  terminal_at: null, terminal_result: null, last_error: null,
});
const coherentMetrics = {
  ...metrics, active_generation: 7, active_run_epoch: 8,
  active_config_revision: 4, active_config_etag: '"graphx-config-4"',
  capture_id: 'g7-r8-c4-m1',
};
const diagnostics = schema('graphx.dashboard.diagnostics.v1', {
  active_generation: 7, active_run_epoch: 8, active_config_revision: 4,
  active_config_etag: '"graphx-config-4"', capture_id: 'g7-r8-c4-d1',
  sampled_at_monotonic_ms: 2,
  identity_availability: { state: 'available', reason: null }, nodes: [],
});
const snapshot = schema('graphx.dashboard.fhss_snapshot.v1', {
  publisher_epoch: 'a'.repeat(32), latest_sequence: 3,
  captured_at: '2026-07-23T00:00:00Z', config_revision: 4,
  config_etag: '"graphx-config-4"', generation: 7, run_epoch: 8,
  coherence: { state: 'coherent', metric_capture_id: 'g7-r8-c4-m1', diagnostic_capture_id: 'g7-r8-c4-d1' },
  configuration: config, graph, runtime, metrics: coherentMetrics, diagnostics,
  transport: {
    counter_availability: { state: 'available', reason: null },
    active_websocket_clients: 0, pongs_received: 0, idle_closes: 0,
    protocol_failures: 0, rejected_upgrades: 0, replayed_events: 0,
    resync_requests: 0, queue_overflows: 0,
    close_reasons: { normal: 0, protocol: 0, unsupported_data: 0, invalid_utf8: 0, too_big: 0, policy: 0, going_away: 0, internal: 0 },
    dropped_events_total: 0, coalesced_events_total: 0,
  },
});

describe('typed API boundaries', () => {
  afterEach(() => vi.unstubAllGlobals());

  it('captures the authoritative HTTP ETag and rejects disagreement', () => {
    expect(parsers.configuration(config, '"graphx-config-4"').httpEtag).toBe('"graphx-config-4"');
    expect(() => parsers.configuration(config, '"graphx-config-3"')).toThrow(/disagree/);
  });

  it('uses the response ETag verbatim and requests the selected physical channel', async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response(JSON.stringify(config), { status: 200, headers: { ETag: '"graphx-config-4"', 'Content-Type': 'application/json' } }))
      .mockResolvedValueOnce(new Response(JSON.stringify(spectrum), { status: 200, headers: { 'Content-Type': 'application/json' } }));
    vi.stubGlobal('fetch', fetchMock);
    expect((await getConfiguration()).httpEtag).toBe('"graphx-config-4"');
    expect((await getSpectrum(17)).channel_index).toBe(17);
    expect(fetchMock).toHaveBeenNthCalledWith(2, '/api/v1/fhss/spectrum?channel=17', undefined);
  });

  it('accepts representative resources and rejects malformed shapes', () => {
    expect(parsers.metrics(metrics).nodes).toHaveLength(0);
    expect(parsers.expectedTruth(expectedTruth).messages).toHaveLength(0);
    expect(parsers.observation(observation).observed_pulses).toHaveLength(0);
    expect(parsers.comparison(comparison).evaluation_state).toBe('indeterminate');
    expect(parsers.spectrum(spectrum).channel_index).toBe(17);
    expect(parsers.jobs(schema('graphx.dashboard.fhss_job_history.v1', { controller_epoch: 1, entries: [], bounds: {} })).controller_epoch).toBe(1);
    expect(parsers.investigations(schema('graphx.dashboard.fhss_investigation_operations.v1', { entries: [], bounds: {} })).entries).toHaveLength(0);
    expect(parsers.provenance(schema('graphx.dashboard.configuration_provenance.v1', {
      config_revision: 1, etag: '"graphx-config-1"', provenance: [],
    })).provenance).toEqual([]);
    expect(() => parsers.metrics({ ...metrics, nodes: 'bad' })).toThrow(/array/);
    expect(() => parsers.metrics({ ...metrics, positional_overlay: [] })).toThrow(/unexpected field/);
    expect(() => parsers.metrics({ ...metrics, metric_definitions: [
      ...metrics.metric_definitions.slice(0, 30), metrics.metric_definitions[0],
    ] })).toThrow(/duplicate/);
    expect(() => parsers.spectrum({ ...spectrum, channel_index: 64 })).toThrow(/invalid/);
    expect(() => parsers.comparison({ ...comparison, evaluation_state: 'available' })).toThrow(/allowed/);
    expect(() => parsers.jobs(schema('graphx.dashboard.fhss_job_history.v1', { controller_epoch: '1', entries: [], bounds: {} }))).toThrow(/integer/);
    expect(() => parsers.configuration({ ...config, derived_paths: {} })).toThrow(/array/);
    expect(() => parsers.provenance(schema('graphx.dashboard.configuration_provenance.v1', {
      config_revision: 1, etag: '"graphx-config-1"', provenance: {},
    }))).toThrow(/array/);
  });

  it('rejects semantic metric and diagnostic contradictions', () => {
    expect(() => parsers.metrics({
      ...metrics,
      graph: { ...metrics.graph, total_items_processed: 0 },
    })).toThrow(/must be null/);
    expect(() => parsers.metrics({
      ...metrics,
      rate_availability: { state: 'unavailable', reason: 'not compatible' },
      qualified_rates: [{ name: 'graph_total_enqueued', scope: 'graph', unit: 'messages_per_second', interval_ms: 1, value: 0 }],
    })).toThrow(/must be empty/);
    expect(() => parsers.metrics({
      ...metrics,
      metric_definitions: metrics.metric_definitions.map((entry, index) =>
        index === 0 ? { ...entry, name: 'invented_metric', field: '/graph/invented_metric' } : entry),
    })).toThrow(/missing/);
    expect(() => parsers.diagnostics({
      ...diagnostics,
      nodes: [{ node_id: 'source', availability: 'unavailable', unavailable_reason: null, node_index: 0, name: 'source', type: 'Source', diagnostics: {} }],
    })).toThrow(/unavailable diagnostics contract/);
  });

  it('accepts only one complete coherent snapshot identity tuple', () => {
    const parsed = parsers.snapshot(snapshot);
    expect(inspectorEvidenceCoherent(parsed.graph, parsed)).toBe(true);
    expect(inspectorEvidenceCoherent({ ...parsed.graph, etag: '"stale-same-revision"' }, parsed)).toBe(false);
    expect(() => parsers.snapshot({
      ...snapshot,
      graph: { ...graph, etag: '"stale-same-revision"' },
    })).toThrow(/identity tuple/);
    expect(() => parsers.snapshot({
      ...snapshot,
      runtime: { ...runtime, active_run_epoch: 9 },
    })).toThrow(/identity tuple/);
    expect(() => parsers.snapshot({
      ...snapshot,
      coherence: { ...snapshot.coherence, metric_capture_id: 'wrong-capture' },
    })).toThrow(/identity tuple/);
    expect(() => parsers.snapshot({
      ...snapshot,
      transport: { ...snapshot.transport, pongs_received: Number.MAX_SAFE_INTEGER + 1 },
    })).toThrow(/safe integer/);
    expect(() => parsers.snapshot({ ...snapshot, independently_sampled_metrics: {} })).toThrow(/unexpected field/);
  });

  it('distinguishes an unavailable current queue sample from observed zero', () => {
    const edgeMetric = {
      availability: 'available',
      current_queue_depth: null,
      current_queue_depth_availability: {
        state: 'unavailable',
        reason: 'edge occupancy could not be sampled',
      },
      peak_queue_depth: 3,
    };
    render(<QueueDepthSummary edgeMetric={edgeMetric} />);
    expect(screen.getByText(/Current queue depth: unavailable \(edge occupancy could not be sampled\)/)).toBeTruthy();
    expect(screen.getByText(/Peak queue depth: 3 messages/)).toBeTruthy();
    expect(screen.queryByText(/null messages/)).toBeNull();
  });
});

describe('operator state presentation', () => {
  afterEach(() => vi.unstubAllGlobals());

  it.each([
    [{ kind: 'loading' } as const, 'Loading topology…'],
    [{ kind: 'empty', message: 'none' } as const, 'Empty topology'],
    [{ kind: 'disconnected', message: 'offline' } as const, 'Dashboard unavailable'],
    [{ kind: 'error', message: 'bad' } as const, 'Dashboard unavailable'],
    [{ kind: 'stale', graph: { nodes: [], edges: [] }, message: 'stale data' } as const, 'stale data'],
  ])('renders %s state', (state, text) => {
    const retry = vi.fn(); const { unmount } = render(<LoadStatePanel load={state} retry={retry} />);
    expect(screen.getByText(text)).toBeTruthy(); const button = screen.queryByRole('button', { name: 'Retry' }); if (button) { fireEvent.click(button); expect(retry).toHaveBeenCalled(); } unmount();
  });

  it('keeps the workbench mounted when operator resources are unavailable', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('unavailable')));
    render(<Operations refreshToken={0} />);
    await waitFor(() => expect(screen.getByText('Operator resources refreshed')).toBeTruthy());
    expect(screen.getByRole('heading', { name: 'Complete-message jobs' })).toBeTruthy();
    expect(screen.getByRole('heading', { name: 'Investigation workflow' })).toBeTruthy();
  });
});
