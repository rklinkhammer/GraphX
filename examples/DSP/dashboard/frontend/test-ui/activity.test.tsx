import { describe, expect, it, vi } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { act, fireEvent, render, renderHook, screen } from '@testing-library/react';
import {
  activityForDisplayModel, animatedEdgeIds, BoundedActivityBuffer, classifyMessage,
  coherentMetrics, deriveAuthoritativeActivity, MAX_ACTIVITY_HISTORY, MAX_AGGREGATE_UPDATES_PER_SECOND,
  MAX_QUEUED_PRESENTATION_UPDATES, MAX_VISIBLE_ACTIVITY_MARKERS,
  MAX_ACTIVITY_MARKERS_PER_EDGE, MAX_VISUAL_REFRESH_HZ,
  MAX_EXPANDED_DETECTORS,
  PRESENTATION_UPDATE_INTERVAL_MS,
} from '../src/activity';
import type { MetricsResource } from '../src/api';
import type { DisplayTopologyModel } from '../src/topology';
import { ActivityControls, useActivityPreferences } from '../src/ActivityControls';
import { useBoundedActivityPresentation } from '../src/useBoundedActivityPresentation';
import { toTopology } from '../src/topology';
import { toFHSSPresentation } from '../src/fhssPresentation';

function metric(capture: string, sampled: number, count: number, overrides: Record<string, unknown> = {}): MetricsResource {
  const definition = {
    name: 'messages_dequeued', field: '/edges/*/messages_dequeued', scope: 'edge',
    kind: 'counter', unit: 'message', monotonic: true, availability: 'explicit',
    capture: 'atomic relaxed-load within one collector snapshot', reset: 'new_runtime_manager',
    aggregation: 'direct atomic edge counter; never re-summed by dashboard',
    overflow: 'unavailable_above_javascript_safe_integer',
    numeric_representation: 'non_negative_javascript_safe_integer',
  };
  return {
    schema: 'graphx.dashboard.metrics.v1', active_generation: 3, active_run_epoch: 2,
    active_config_revision: 7, active_config_etag: 'etag', capture_id: capture,
    sampled_at_monotonic_ms: sampled,
    rate_availability: { state: 'available', reason: null },
    metric_definitions: [definition],
    collection_interval: { state: 'available', reason: null, clock: 'steady_clock', duration_ms: 200 },
    graph: {}, nodes: [], edges: [{
      edge_id: 'source:63->sink:64', source_node_id: 'source', source_port: 63,
      destination_node_id: 'sink', destination_port: 64, availability: 'available',
      unavailable_reason: null, message_type: 'graph::Message<dsp::FHSSChannelizedIqPacket>', messages_dequeued: count,
      ...overrides,
    }],
  };
}

describe('bounded aggregate activity', () => {
  it('derives a qualified counter delta using exact edge identity', () => {
    const frame = deriveAuthoritativeActivity(metric('a', 1000, 10), metric('b', 1200, 18));
    expect(frame?.edges.get('source:63->sink:64')).toMatchObject({
      availability: 'available', messages: 8, messagesPerSecond: 40, intervalMs: 200,
      messageClass: 'channel-iq',
    });
  });

  it('uses the full server-monotonic span when browser delivery skips a capture', () => {
    const current = metric('c', 1600, 34);
    current.collection_interval = {
      state: 'available', reason: null, clock: 'steady_clock', duration_ms: 200,
    };
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), current)
      ?.edges.get('source:63->sink:64')).toMatchObject({
      messages: 24, intervalMs: 600, messagesPerSecond: 40,
    });
  });

  it.each([
    ['generation mismatch', { active_generation: 4 }, 'graph generation changed'],
    ['run mismatch', { active_run_epoch: 4 }, 'run epoch changed'],
    ['revision mismatch', { active_config_revision: 8 }, 'configuration revision changed'],
    ['etag mismatch', { active_config_etag: 'other' }, 'configuration ETag changed'],
  ])('marks %s unavailable', (_label, patch, reason) => {
    const frame = deriveAuthoritativeActivity(metric('a', 1000, 10), { ...metric('b', 1200, 18), ...patch });
    expect(frame?.edges.get('source:63->sink:64')).toMatchObject({
      availability: 'unavailable', unavailableReason: reason, messages: null,
    });
  });

  it('does not turn reset, missing, stale, or unavailable counters into zero', () => {
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), metric('b', 1200, 2))
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('reset');
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), metric('b', 1000, 12))
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('stale');
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), metric('b', 1200, 12, {
      availability: 'unavailable', unavailable_reason: 'runtime stopped', messages_dequeued: null,
    }))?.edges.get('source:63->sink:64')).toMatchObject({ availability: 'unavailable', messages: null });
  });

  it('uses an explicit bounded vocabulary', () => {
    const expected = new Map([
      ['FHSSSyntheticIqOutputPacket>', 'iq-block'],
      ['FHSSDownconvertedIqPacket>', 'iq-block'],
      ['FHSSChannelizedIqPacket>', 'channel-iq'],
      ['FHSSPerChannelPulseEvidencePacket>', 'pulse-candidate'],
      ['FHSSPulseCandidateEvidencePacket>', 'pulse-candidate'],
      ['FHSSCpsmBranchMetricPacket>', 'decoder-data'],
      ['FHSSCpsmSymbolDecisionPacket>', 'decoder-data'],
      ['FHSSDecodedPulseWordsPacket>', 'decoder-data'],
      ['FHSSAssembledMessagePacket>', 'assembled-message'],
    ]);
    for (const [type, messageClass] of expected) {
      expect(classifyMessage(`graph::Message<graphx::dsp::${type}`)).toBe(messageClass);
    }
    expect(classifyMessage('opaque')).toBe('unknown/unclassified');
    expect(classifyMessage('ControlMessage')).toBe('unknown/unclassified');
    expect(classifyMessage('CandidateSet')).toBe('unknown/unclassified');
    expect(classifyMessage('UnrelatedComplexPayload')).toBe('unknown/unclassified');
    expect(classifyMessage('FHSSChannelizedIqPacketExtra>')).toBe('unknown/unclassified');
    expect(classifyMessage('FakeFHSSAssembledMessagePacket>')).toBe('unknown/unclassified');
  });

  it.each([
    ['unit', 'item'],
    ['reset', 'sample_replaced'],
    ['monotonic', false],
    ['capture', 'some other collection'],
  ])('rejects incompatible messages_dequeued definition %s', (field, replacement) => {
    const current = metric('b', 1200, 18);
    current.metric_definitions = [{
      ...(current.metric_definitions as Record<string, unknown>[])[0],
      [field]: replacement,
    }];
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), current)
      ?.edges.get('source:63->sink:64')).toMatchObject({
      availability: 'unavailable',
      unavailableReason: expect.stringContaining(`invalid ${field}`),
      messages: null,
    });
  });

  it('rejects unsafe, missing, unavailable, and incompatible counter evidence', () => {
    const edge = (document: MetricsResource) => document.edges[0]!;
    const unsafe = metric('b', 1200, 18);
    edge(unsafe).messages_dequeued = Number.MAX_SAFE_INTEGER + 1;
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), unsafe)
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('safe integer');

    const missingCurrent = metric('b', 1200, 18);
    delete edge(missingCurrent).messages_dequeued;
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), missingCurrent)
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('missing');

    const missingPriorCounter = metric('a', 1000, 10);
    delete edge(missingPriorCounter).messages_dequeued;
    expect(deriveAuthoritativeActivity(missingPriorCounter, metric('b', 1200, 18))
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('missing');

    const missingPriorEdge = metric('a', 1000, 10); missingPriorEdge.edges = [];
    expect(deriveAuthoritativeActivity(missingPriorEdge, metric('b', 1200, 18))
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('previous edge');

    for (const interval of [
      { state: 'unavailable', reason: 'stopped', clock: null, duration_ms: null },
      { state: 'available', reason: null, clock: 'system_clock', duration_ms: 200 },
      { state: 'available', reason: null, clock: 'steady_clock', duration_ms: 0 },
    ]) {
      const current = metric('b', 1200, 18); current.collection_interval = interval;
      expect(deriveAuthoritativeActivity(metric('a', 1000, 10), current)
        ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('steady-clock');
    }
    const noRate = metric('b', 1200, 18);
    noRate.rate_availability = { state: 'unavailable', reason: 'no compatible sample' };
    expect(deriveAuthoritativeActivity(metric('a', 1000, 10), noRate)
      ?.edges.get('source:63->sink:64')?.unavailableReason).toContain('no compatible sample');
  });

  it('rejects a coherent snapshot whose metric capture identity does not match', () => {
    const metrics = metric('metric-capture', 1200, 18);
    const snapshot = {
      coherence: { state: 'coherent', metric_capture_id: 'different' },
      metrics, generation: metrics.active_generation, run_epoch: metrics.active_run_epoch,
      config_revision: metrics.active_config_revision, config_etag: metrics.active_config_etag,
    };
    expect(coherentMetrics(snapshot as never)).toBeUndefined();
  });

  it('withholds a partial bundle total and retains member availability', () => {
    const frame = deriveAuthoritativeActivity(metric('a', 1000, 10), metric('b', 1200, 18));
    const model = {
      nodes: [], edges: [{
        id: 'bundle', kind: 'presentation-bundle', presentation_only: true,
        source_node_id: 'source', target_node_id: 'bank', sourceHandle: undefined, targetHandle: undefined,
        label: '64 channels', authoritativeEdgeIds: ['source:63->sink:64', 'missing'],
        mappings: [],
      }],
    } as unknown as DisplayTopologyModel;
    expect(activityForDisplayModel(model, frame).get('bundle')).toMatchObject({
      availability: 'unavailable', availableMembers: 1, memberCount: 2, messages: null,
    });
  });

  it('correlates all 64 exact detector mappings and aggregates only their member identities', () => {
    const graph = JSON.parse(readFileSync(resolve(
      process.cwd(), '../../../../libdsp/config/fhss_phase2_binary_iq_receiver.json',
    ), 'utf8'));
    const presentation = toFHSSPresentation(toTopology(graph));
    expect(presentation.detectorBank?.members).toHaveLength(MAX_EXPANDED_DETECTORS);
    const edgeActivity = new Map(presentation.authoritative.edges.map((edge) => [edge.id, {
      edgeId: edge.id, availability: 'available' as const, unavailableReason: null,
      messageClass: 'channel-iq' as const, messages: 1, messagesPerSecond: 5,
      intervalMs: 200, memberCount: 1, availableMembers: 1,
    }]));
    const frame = { captureId: 'fixture', sampledAtMonotonicMs: 1000, edges: edgeActivity };
    const expanded = activityForDisplayModel(presentation.expanded, frame);
    for (let channel = 0; channel < 64; channel += 1) {
      expect(expanded.get(`channelizer:${channel}->detector_${channel}:0`)?.messages).toBe(1);
      expect(expanded.get(`detector_${channel}:0->merge:${channel + 1}`)?.messages).toBe(1);
    }
    const collapsed = activityForDisplayModel(presentation.collapsed, frame);
    const bundles = presentation.collapsed.edges.filter((edge) => 'kind' in edge);
    expect(bundles).toHaveLength(2);
    expect(bundles.map((edge) => collapsed.get(edge.id)?.messages)).toEqual([64, 64]);
  });

  it('caps animation, queue depth, update rate, and history', () => {
    const edgeMap = new Map(Array.from({ length: 80 }, (_, index) => [`e${index}`, {
      edgeId: `e${index}`, availability: 'available' as const, unavailableReason: null,
      messageClass: 'iq-block' as const, messages: index + 1, messagesPerSecond: index + 1,
      intervalMs: 1000, memberCount: 1, availableMembers: 1,
    }]));
    expect(animatedEdgeIds(edgeMap, false, false).size).toBe(MAX_VISIBLE_ACTIVITY_MARKERS);
    expect(MAX_ACTIVITY_MARKERS_PER_EDGE).toBe(1);
    expect(MAX_AGGREGATE_UPDATES_PER_SECOND).toBeLessThanOrEqual(MAX_VISUAL_REFRESH_HZ);
    expect(animatedEdgeIds(edgeMap, true, false).size).toBe(0);
    expect(animatedEdgeIds(edgeMap, false, true).size).toBe(0);

    const buffer = new BoundedActivityBuffer();
    for (let index = 0; index < MAX_ACTIVITY_HISTORY + 20; index += 1) {
      buffer.offer({ captureId: String(index), sampledAtMonotonicMs: index * 1000, edges: edgeMap }, index * 1000);
    }
    expect(buffer.historySize()).toBe(MAX_ACTIVITY_HISTORY);
    const base = (MAX_ACTIVITY_HISTORY + 20) * 1000;
    for (let index = 1; index <= MAX_AGGREGATE_UPDATES_PER_SECOND; index += 1) {
      buffer.offer({ captureId: `q${index}`, sampledAtMonotonicMs: base + index, edges: edgeMap }, base + index);
    }
    expect(buffer.queuedSize()).toBeLessThanOrEqual(MAX_QUEUED_PRESENTATION_UPDATES);
    expect(buffer.coalescedUpdates()).toBeGreaterThan(0);
  });

  it('promotes the latest coalesced frame at cadence and cleans its timer', () => {
    vi.useFakeTimers();
    let now = 0;
    const clock = vi.spyOn(performance, 'now').mockImplementation(() => now);
    const edges = new Map();
    const first = { captureId: 'first', sampledAtMonotonicMs: 1000, edges };
    const second = { captureId: 'second', sampledAtMonotonicMs: 1010, edges };
    const final = { captureId: 'final', sampledAtMonotonicMs: 1020, edges };
    const hook = renderHook(({ frame }) => useBoundedActivityPresentation(frame), {
      initialProps: { frame: first },
    });
    expect(hook.result.current.frame?.captureId).toBe('first');
    now = 10; hook.rerender({ frame: second });
    now = 20; hook.rerender({ frame: final });
    expect(hook.result.current).toMatchObject({ coalescedUpdates: 2, queuedUpdates: 1 });
    now = PRESENTATION_UPDATE_INTERVAL_MS;
    act(() => vi.advanceTimersByTime(PRESENTATION_UPDATE_INTERVAL_MS));
    expect(hook.result.current).toMatchObject({
      frame: expect.objectContaining({ captureId: 'final' }), queuedUpdates: 0,
    });
    hook.unmount();
    expect(vi.getTimerCount()).toBe(0);
    clock.mockRestore(); vi.useRealTimers();
  });

  it('clears a superseded queued frame when a later frame is accepted directly', () => {
    const buffer = new BoundedActivityBuffer();
    const edges = new Map();
    const first = { captureId: 'first', sampledAtMonotonicMs: 1000, edges };
    const stale = { captureId: 'stale', sampledAtMonotonicMs: 1010, edges };
    const current = { captureId: 'current', sampledAtMonotonicMs: 1200, edges };
    expect(buffer.offer(first, 0)).toBe(true);
    expect(buffer.offer(stale, 10)).toBe(false);
    expect(buffer.queuedSize()).toBe(1);
    expect(buffer.offer(current, PRESENTATION_UPDATE_INTERVAL_MS)).toBe(true);
    expect(buffer.queuedSize()).toBe(0);
    expect(buffer.latest()?.captureId).toBe('current');
    expect(buffer.promote(PRESENTATION_UPDATE_INTERVAL_MS * 2)).toBe(false);
  });
});

describe('activity presentation controls', () => {
  it('keeps pause, speed, reduced motion, and overload suppression explicit', () => {
    const changes: unknown[] = [];
    const value = { paused: false, speed: 1, reducedMotion: false, systemReducedMotion: false };
    render(<ActivityControls value={value} onChange={(next) => changes.push(next)} suppressed={17} />);
    fireEvent.click(screen.getByRole('button', { name: 'Pause motion' }));
    expect(changes.at(-1)).toMatchObject({ paused: true });
    fireEvent.change(screen.getByLabelText('Presentation speed'), { target: { value: '2' } });
    expect(changes.at(-1)).toMatchObject({ speed: 2, paused: false });
    fireEvent.click(screen.getByLabelText('Reduce motion explicitly'));
    expect(changes.at(-1)).toMatchObject({ reducedMotion: true });
    expect(screen.getByRole('status').textContent).toContain('17 active edges represented without motion');
  });

  it('observes system reduced motion and cleans up its subscription', () => {
    const original = window.matchMedia;
    const add = vi.fn(); const remove = vi.fn();
    window.matchMedia = vi.fn(() => ({
      matches: true, media: '(prefers-reduced-motion: reduce)', onchange: null,
      addEventListener: add, removeEventListener: remove,
      addListener: vi.fn(), removeListener: vi.fn(), dispatchEvent: vi.fn(),
    }));
    const hook = renderHook(() => useActivityPreferences());
    expect(hook.result.current[0].systemReducedMotion).toBe(true);
    expect(add).toHaveBeenCalledWith('change', expect.any(Function));
    hook.unmount();
    expect(remove).toHaveBeenCalledWith('change', expect.any(Function));
    window.matchMedia = original;
  });
});
