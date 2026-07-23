import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { LoadStatePanel } from '../src/App';
import { getConfiguration, getSpectrum, parsers } from '../src/api';
import { Operations } from '../src/Operations';

const schema = (name: string, fields: Record<string, unknown>) => ({ schema: name, ...fields });
const sha = (digit: string) => digit.repeat(64);
const config = schema('graphx.dashboard.config.v1', {
  owner: 'receiver', config_revision: 4, etag: '"graphx-config-4"',
  effective: { nodes: [], edges: [] }, derived_paths: [],
});
const metrics = schema('graphx.dashboard.metrics.v1', {
  active_generation: 0, active_run_epoch: 0, active_config_revision: 0,
  active_config_etag: '', metric_definitions: Array.from({ length: 19 }, () => ({})),
  graph: {}, nodes: [], edges: [],
});
const expectedTruth = schema('graphx.dashboard.fhss_expected_truth.v1', {
  semantic_class: 'expected', scenario_id: 'synthetic-1', config_revision: 1,
  config_etag: '"graphx-config-1"', timing_basis: {}, messages: [], pulses: [],
  expected_receiver_message: {}, synthetic_impairments: {}, bounds: {}, truth_sha256: sha('0'),
});
const observation = schema('graphx.dashboard.fhss_receiver_observation.v1', {
  semantic_class: 'observed', generation: 0, run_epoch: 0, config_revision: 0,
  config_etag: '', observation_id: 'observation-g0-r0', availability: {},
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
    expect(() => parsers.spectrum({ ...spectrum, channel_index: 64 })).toThrow(/invalid/);
    expect(() => parsers.comparison({ ...comparison, evaluation_state: 'available' })).toThrow(/allowed/);
    expect(() => parsers.jobs(schema('graphx.dashboard.fhss_job_history.v1', { controller_epoch: '1', entries: [], bounds: {} }))).toThrow(/integer/);
    expect(() => parsers.configuration({ ...config, derived_paths: {} })).toThrow(/array/);
    expect(() => parsers.provenance(schema('graphx.dashboard.configuration_provenance.v1', {
      config_revision: 1, etag: '"graphx-config-1"', provenance: {},
    }))).toThrow(/array/);
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
