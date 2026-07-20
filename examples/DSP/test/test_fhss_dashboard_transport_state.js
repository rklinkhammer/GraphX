// SPDX-License-Identifier: MIT
'use strict';

const assert = require('node:assert/strict');
const transport = require(process.argv[2]);

const limits = {
  frame_bytes: 65536, message_bytes: 262144, fragments_per_message: 32,
  commands_per_second: 16, events_per_second: 256, replay_events: 256,
  replay_bytes: 2097152, queue_events: 128, queue_bytes: 2097152,
  idle_timeout_ms: 1200, max_lifetime_ms: 3600000
};
const hello = {
  schema: 'graphx.dashboard.websocket_hello.v1', api_version: 'v1',
  publisher_epoch: 'epoch-a', latest_sequence: 9,
  oldest_available_sequence: 1, heartbeat_interval_ms: 200, limits
};
assert.equal(transport.validateHello(hello), true);
for (const mutation of [
  {...hello, heartbeat_interval_ms: '200'},
  {...hello, heartbeat_interval_ms: 0},
  {...hello, oldest_available_sequence: 11},
  {...hello, limits: {...limits, frame_bytes: 300000}},
  {...hello, limits: {...limits, unexpected: 1}},
  {...hello, unexpected: true}
]) assert.equal(transport.validateHello(mutation), false);

const heartbeat = {schema: 'graphx.dashboard.websocket_heartbeat.v1',
  publisher_epoch: 'epoch-a', timestamp: '2026-07-20T01:02:03.456Z'};
assert.equal(transport.validateHeartbeat(heartbeat, 'epoch-a'), true);
assert.equal(transport.validateHeartbeat({...heartbeat, timestamp: 'now'}, 'epoch-a'), false);
assert.equal(transport.validateHeartbeat({...heartbeat, publisher_epoch: 'epoch-b'}, 'epoch-a'), false);
assert.equal(transport.validateHeartbeat({...heartbeat, extra: 1}, 'epoch-a'), false);

const event = {schema: 'graphx.dashboard.event.v1', api_version: 'v1',
  publisher_epoch: 'epoch-a', sequence: 8, event_type: 'runtime_status',
  timestamp: '2026-07-20T01:02:03Z', payload: {state: 'running'}};
assert.equal(transport.classifyEvent(event, 'epoch-a', 7), 'accepted');
assert.equal(transport.classifyEvent(event, 'epoch-a', 8), 'duplicate');
assert.equal(transport.classifyEvent({...event, sequence: 10}, 'epoch-a', 7), 'resync');
assert.equal(transport.classifyEvent({...event, publisher_epoch: 'epoch-b'}, 'epoch-a', 7), 'resync');
assert.equal(transport.classifyEvent({...event, timestamp: 'not-a-date'}, 'epoch-a', 7), 'resync');

const counters = {dropped_events: 0, dropped_events_total: 0,
  coalesced_events_total: 0, reconnects_total: 1};
const batch = {schema: 'graphx.dashboard.events_batch.v1',
  stream: '/api/v1/fhss/events', client_id: 'fhss-browser-poll',
  publisher_epoch: 'epoch-a', latest_sequence: 8,
  oldest_available_sequence: 1, newest_available_sequence: 8,
  events: [event], resync_required: false, reason: 'none', truncated: false,
  counters};
assert.equal(transport.validateBatch(batch, 'fhss-browser-poll'), true);
for (const mutation of [
  {...batch, client_id: 'foreign'},
  {...batch, stream: '/attacker'},
  {...batch, resync_required: 'false'},
  {...batch, latest_sequence: 9},
  {...batch, oldest_available_sequence: 10},
  {...batch, reason: 'invented'},
  {...batch, truncated: true},
  {...batch, counters: {...counters, dropped_events: -1}},
  {...batch, counters: {...counters, extra: 1}},
  {...batch, unexpected: true}
]) assert.equal(transport.validateBatch(mutation, 'fhss-browser-poll'), false);
assert.equal(transport.validateBatch({...batch, events: [], resync_required: true,
  reason: 'replay_limit', truncated: true}, 'fhss-browser-poll'), true);

const resync = {schema: 'graphx.dashboard.websocket_resync_required.v1',
  publisher_epoch: 'epoch-a', snapshot_url: '/api/v1/fhss/snapshot',
  reason: 'retention_gap', latest_sequence: 8};
assert.equal(transport.validateResync(resync, '/api/v1/fhss/snapshot'), true);
assert.equal(transport.validateResync({...resync, snapshot_url: 'https://attacker.invalid'},
  '/api/v1/fhss/snapshot'), false);
assert.equal(transport.validateResync({...resync, reason: 'none'},
  '/api/v1/fhss/snapshot'), false);

let attempt = 0;
for (let index = 0; index < 12; ++index) {
  const next = transport.nextReconnect(attempt);
  assert.equal(next.allowed, true);
  assert.ok(next.delay_ms > 0 && next.delay_ms <= 30000);
  attempt = next.attempt;
}
assert.deepEqual(transport.nextReconnect(attempt),
  {allowed: false, attempt: 12, delay_ms: 0});
// The production page resets this attempt only from its stable-open timer;
// the pure transition remains finite until that explicit stable signal.
assert.equal(transport.nextReconnect(12).allowed, false);

console.log('FHSS dashboard transport state behavioral qualification: PASS');
