import assert from 'node:assert/strict';
import { test } from 'node:test';
import { classifySequence, nextReconnect, parseEventBatch, parseEventEnvelope } from '../test-dist/transportState.js';

const epoch = '0123456789abcdef0123456789abcdef';
const event = {schema: 'graphx.dashboard.event.v1', api_version: 'v1', publisher_epoch: epoch,
  sequence: 8, event_type: 'runtime_status', timestamp: '2026-07-22T12:00:00Z', generation: 1,
  run_epoch: 1, config_revision: 1, config_etag: 'etag', controller_epoch: null, job_id: null,
  correlation_id: null, semantic_class: null, payload: {}};

test('event transport validates representative envelopes and batches', () => {
  assert.equal(parseEventEnvelope(event).sequence, 8);
  const batch = {schema: 'graphx.dashboard.events_batch.v1', stream: '/api/v1/fhss/events',
    publisher_epoch: epoch, client_id: 'fhss-react', resync_required: false, reason: 'none',
    latest_sequence: 8, oldest_available_sequence: 8, newest_available_sequence: 8,
    truncated: false, events: [event], counters: {dropped_events: 0, dropped_events_total: 0, coalesced_events_total: 0, reconnects_total: 0}};
  assert.equal(parseEventBatch(batch, 'fhss-react').events.length, 1);
  assert.throws(() => parseEventBatch({...batch, client_id: 'foreign'}, 'fhss-react'), /client/);
});

test('event transport preserves duplicate, gap, epoch resync, and finite reconnect semantics', () => {
  assert.equal(classifySequence({epoch: '', lastSequence: 0}, epoch, 1), 'accept');
  assert.equal(classifySequence({epoch, lastSequence: 4}, epoch, 4), 'duplicate');
  assert.equal(classifySequence({epoch, lastSequence: 4}, epoch, 6), 'gap');
  assert.equal(classifySequence({epoch, lastSequence: 4}, 'abcdefabcdefabcdefabcdefabcdefab', 5), 'resync');
  let attempt = 0;
  for (let index = 0; index < 12; ++index) { const next = nextReconnect(attempt); assert.equal(next.allowed, true); attempt = next.attempt; }
  assert.deepEqual(nextReconnect(attempt), {allowed: false, attempt: 12, delayMs: 0});
});
