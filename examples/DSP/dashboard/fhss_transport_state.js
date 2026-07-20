// SPDX-License-Identifier: MIT
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.GraphXFhssTransport = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';

  const reasons = new Set([
    'none', 'publisher_epoch_changed', 'retention_gap', 'sequence_ahead',
    'sequence_gap', 'replay_limit', 'queue_overflow', 'client_limit'
  ]);
  const limitKeys = [
    'frame_bytes', 'message_bytes', 'fragments_per_message',
    'commands_per_second', 'events_per_second', 'replay_events',
    'replay_bytes', 'queue_events', 'queue_bytes', 'idle_timeout_ms',
    'max_lifetime_ms'
  ];
  const counterKeys = [
    'dropped_events', 'dropped_events_total', 'coalesced_events_total',
    'reconnects_total'
  ];
  const rfc3339 =
    /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+-]\d{2}:\d{2})$/;
  const validSequence = (value) => Number.isSafeInteger(value) && value >= 0;
  const exactKeys = (value, expected) => {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
    const actual = Object.keys(value).sort();
    const wanted = [...expected].sort();
    return actual.length === wanted.length &&
      actual.every((key, index) => key === wanted[index]);
  };
  const validTimestamp = (value) => typeof value === 'string' &&
    rfc3339.test(value) && Number.isFinite(Date.parse(value));
  const positiveSafeInteger = (value) => Number.isSafeInteger(value) && value > 0;

  function validateHello(value) {
    if (!exactKeys(value, ['schema', 'api_version', 'publisher_epoch',
      'latest_sequence', 'oldest_available_sequence', 'heartbeat_interval_ms',
      'limits'])) return false;
    if (value.schema !== 'graphx.dashboard.websocket_hello.v1' ||
        value.api_version !== 'v1' || typeof value.publisher_epoch !== 'string' ||
        !value.publisher_epoch || !validSequence(value.latest_sequence) ||
        !validSequence(value.oldest_available_sequence) ||
        value.oldest_available_sequence > value.latest_sequence + 1 ||
        !positiveSafeInteger(value.heartbeat_interval_ms) ||
        !exactKeys(value.limits, limitKeys)) return false;
    return limitKeys.every((key) => positiveSafeInteger(value.limits[key])) &&
      value.limits.frame_bytes <= value.limits.message_bytes &&
      value.limits.replay_events > 0 && value.limits.queue_events > 0;
  }

  function validateHeartbeat(value, expectedEpoch) {
    return exactKeys(value, ['schema', 'publisher_epoch', 'timestamp']) &&
      value.schema === 'graphx.dashboard.websocket_heartbeat.v1' &&
      value.publisher_epoch === expectedEpoch && validTimestamp(value.timestamp);
  }

  function validateResync(value, snapshotRoute) {
    return exactKeys(value, ['schema', 'publisher_epoch', 'snapshot_url',
      'reason', 'latest_sequence']) &&
      value.schema === 'graphx.dashboard.websocket_resync_required.v1' &&
      typeof value.publisher_epoch === 'string' && !!value.publisher_epoch &&
      value.snapshot_url === snapshotRoute && reasons.has(value.reason) &&
      value.reason !== 'none' && validSequence(value.latest_sequence);
  }

  function validateBatch(value, expectedClientId) {
    const keys = ['schema', 'stream', 'client_id', 'publisher_epoch',
      'latest_sequence', 'oldest_available_sequence', 'newest_available_sequence',
      'events', 'resync_required', 'reason', 'truncated', 'counters'];
    if (!exactKeys(value, keys) ||
        value.schema !== 'graphx.dashboard.events_batch.v1' ||
        value.stream !== '/api/v1/fhss/events' ||
        value.client_id !== expectedClientId ||
        typeof value.publisher_epoch !== 'string' || !value.publisher_epoch ||
        !validSequence(value.latest_sequence) ||
        !validSequence(value.oldest_available_sequence) ||
        !validSequence(value.newest_available_sequence) ||
        value.newest_available_sequence !== value.latest_sequence ||
        value.oldest_available_sequence > value.newest_available_sequence + 1 ||
        typeof value.resync_required !== 'boolean' ||
        typeof value.truncated !== 'boolean' || !reasons.has(value.reason) ||
        !Array.isArray(value.events) || !exactKeys(value.counters, counterKeys) ||
        !counterKeys.every((key) => validSequence(value.counters[key]))) return false;
    if ((value.resync_required && value.reason === 'none') ||
        (!value.resync_required && value.reason !== 'none') ||
        (value.reason === 'replay_limit') !== value.truncated ||
        (value.resync_required && value.events.length !== 0)) return false;
    let previous = null;
    for (const event of value.events) {
      if (!event || !validSequence(event.sequence) ||
          event.publisher_epoch !== value.publisher_epoch ||
          event.sequence > value.latest_sequence ||
          (previous !== null && event.sequence !== previous + 1)) return false;
      previous = event.sequence;
    }
    return true;
  }

  function classifyEvent(value, epoch, sequence) {
    if (!value || value.schema !== 'graphx.dashboard.event.v1' ||
        value.api_version !== 'v1' || typeof value.publisher_epoch !== 'string' ||
        !value.publisher_epoch || !validSequence(value.sequence) ||
        typeof value.event_type !== 'string' || !value.event_type ||
        !validTimestamp(value.timestamp) || !value.payload ||
        typeof value.payload !== 'object' || Array.isArray(value.payload)) return 'resync';
    if (epoch && value.publisher_epoch !== epoch) return 'resync';
    if (epoch && value.sequence === sequence) return 'duplicate';
    return value.sequence === sequence + 1 ? 'accepted' : 'resync';
  }

  function nextReconnect(attempt, maxAttempts = 12, maxDelayMs = 30000) {
    if (!Number.isSafeInteger(attempt) || attempt < 0 || attempt >= maxAttempts)
      return {allowed: false, attempt: Math.max(0, attempt), delay_ms: 0};
    const base = Math.min(maxDelayMs, 500 * (2 ** Math.min(6, attempt)));
    return {allowed: true, attempt: attempt + 1,
      delay_ms: Math.min(maxDelayMs, base + (attempt % 5) * 97)};
  }

  return Object.freeze({reasons, validSequence, validTimestamp, validateHello,
    validateHeartbeat, validateResync, validateBatch, classifyEvent,
    nextReconnect});
});
