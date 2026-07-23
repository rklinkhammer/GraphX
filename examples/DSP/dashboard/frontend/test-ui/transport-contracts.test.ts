import { describe, expect, it } from 'vitest';
import { classifySequence, nextReconnect, parseEventBatch, parseEventEnvelope, parseHeartbeat, parseHello, parseResyncRequired } from '../src/transportState';
import { persistSequenceState, restoredSequenceState } from '../src/useEventTransport';

const epoch = '0123456789abcdef0123456789abcdef';
const event = { schema: 'graphx.dashboard.event.v1', api_version: 'v1', publisher_epoch: epoch, sequence: 1, event_type: 'runtime_status', timestamp: '2026-07-22T12:00:00Z', generation: 1, run_epoch: 1, config_revision: 1, config_etag: 'etag', controller_epoch: null, job_id: null, correlation_id: null, semantic_class: null, payload: {} };
const limits = { frame_bytes: 125, message_bytes: 125, fragments_per_message: 1, commands_per_second: 1, events_per_second: 1, replay_events: 1, replay_bytes: 125, queue_events: 1, queue_bytes: 125, idle_timeout_ms: 1, max_lifetime_ms: 1 };
const counters = { dropped_events: 0, dropped_events_total: 0, coalesced_events_total: 0, reconnects_total: 0 };

describe('strict event recovery contracts', () => {
  it('validates hello, event, batch, heartbeat and resync documents', () => {
    expect(parseHello({ schema: 'graphx.dashboard.websocket_hello.v1', api_version: 'v1', publisher_epoch: epoch, latest_sequence: 1, oldest_available_sequence: 1, heartbeat_interval_ms: 1000, limits }).latest_sequence).toBe(1);
    expect(parseEventEnvelope(event).sequence).toBe(1);
    expect(parseEventBatch({ schema: 'graphx.dashboard.events_batch.v1', stream: '/api/v1/fhss/events', publisher_epoch: epoch, client_id: 'fhss-react', resync_required: false, reason: 'none', latest_sequence: 1, oldest_available_sequence: 1, newest_available_sequence: 1, truncated: false, events: [event], counters }, 'fhss-react').events).toHaveLength(1);
    expect(parseHeartbeat({ schema: 'graphx.dashboard.websocket_heartbeat.v1', publisher_epoch: epoch, timestamp: '2026-07-22T12:00:00Z' }, epoch).publisher_epoch).toBe(epoch);
    expect(parseResyncRequired({ schema: 'graphx.dashboard.websocket_resync_required.v1', publisher_epoch: epoch, latest_sequence: 1, snapshot_url: '/api/v1/fhss/snapshot', reason: 'retention_gap' }).reason).toBe('retention_gap');
  });

  it('rejects malformed API/client/epoch/range documents and classifies gaps', () => {
    expect(() => parseEventEnvelope({ ...event, api_version: 'v2' })).toThrow(/schema\/API/);
    expect(() => parseEventBatch({ schema: 'graphx.dashboard.events_batch.v1', stream: '/api/v1/fhss/events', publisher_epoch: epoch, client_id: 'foreign', resync_required: false, reason: 'none', latest_sequence: 1, oldest_available_sequence: 1, newest_available_sequence: 1, truncated: false, events: [event], counters }, 'fhss-react')).toThrow(/client/);
    expect(() => parseResyncRequired({ schema: 'graphx.dashboard.websocket_resync_required.v1', publisher_epoch: epoch, latest_sequence: 1, snapshot_url: 'https://invalid', reason: 'retention_gap' })).toThrow(/contract/);
    expect(classifySequence({ epoch, lastSequence: 1 }, epoch, 3)).toBe('gap');
    expect(() => parseHello({ schema: 'graphx.dashboard.websocket_hello.v1', api_version: 'v1', publisher_epoch: epoch, latest_sequence: 1, oldest_available_sequence: 1, heartbeat_interval_ms: 1000, limits: { ...limits, extra: 1 } })).toThrow(/unsupported field/);
    expect(() => parseEventEnvelope({ ...event, extra: true })).toThrow(/unsupported field/);
  });

  it('uses a finite reconnect budget with explicit exhaustion', () => {
    let attempt = 0; for (let count = 0; count < 12; ++count) { const next = nextReconnect(attempt); expect(next.allowed).toBe(true); attempt = next.attempt; }
    expect(nextReconnect(attempt)).toEqual({ allowed: false, attempt: 12, delayMs: 0 });
  });

  it('persists and restores publisher identity and last accepted sequence', () => {
    const values = new Map<string, string>();
    persistSequenceState({ setItem: (key, value) => values.set(key, value) }, { epoch, lastSequence: 17 });
    expect(restoredSequenceState({ getItem: (key) => values.get(key) ?? null })).toEqual({ epoch, lastSequence: 17 });
  });
});
