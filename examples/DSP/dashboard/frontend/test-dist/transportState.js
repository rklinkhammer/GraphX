const epochPattern = /^[0-9a-f]{32}$/;
const clientPattern = /^[A-Za-z0-9._-]{1,64}$/;
const reasons = new Set(['none', 'publisher_epoch_changed', 'retention_gap', 'sequence_ahead', 'sequence_gap', 'replay_limit', 'queue_overflow', 'client_limit']);
const resyncReasons = new Set([...reasons].filter((reason) => reason !== 'none').concat('resync_required'));
const object = (value, label) => {
    if (!value || typeof value !== 'object' || Array.isArray(value))
        throw new Error(`${label} must be an object`);
    return value;
};
const safeInteger = (value, label, minimum = 0) => {
    if (!Number.isSafeInteger(value) || Number(value) < minimum)
        throw new Error(`${label} must be a safe integer >= ${minimum}`);
    return Number(value);
};
const boundedInteger = (value, label, minimum, maximum) => {
    const result = safeInteger(value, label, minimum);
    if (result > maximum)
        throw new Error(`${label} must be <= ${maximum}`);
    return result;
};
const text = (value, label) => {
    if (typeof value !== 'string' || !value)
        throw new Error(`${label} must be a non-empty string`);
    return value;
};
const epoch = (value, label) => {
    const result = text(value, label);
    if (!epochPattern.test(result))
        throw new Error(`${label} must be a publisher epoch`);
    return result;
};
const dateTime = (value, label) => {
    const result = text(value, label);
    if (Number.isNaN(Date.parse(result)))
        throw new Error(`${label} must be a date-time`);
    return result;
};
const requireFields = (value, fields, label) => {
    for (const field of fields)
        if (!(field in value))
            throw new Error(`${label} is missing ${field}`);
};
const exactFields = (value, fields, label) => {
    requireFields(value, fields, label);
    const permitted = new Set(fields);
    for (const field of Object.keys(value))
        if (!permitted.has(field))
            throw new Error(`${label} contains unsupported field ${field}`);
};
export function parseEventEnvelope(value) {
    const item = object(value, 'event');
    exactFields(item, ['schema', 'api_version', 'publisher_epoch', 'sequence', 'event_type', 'timestamp', 'generation', 'run_epoch', 'config_revision', 'config_etag', 'controller_epoch', 'job_id', 'correlation_id', 'semantic_class', 'payload'], 'event');
    if (item.schema !== 'graphx.dashboard.event.v1' || item.api_version !== 'v1')
        throw new Error('event schema/API mismatch');
    safeInteger(item.generation, 'event.generation');
    safeInteger(item.run_epoch, 'event.run_epoch');
    safeInteger(item.config_revision, 'event.config_revision');
    text(item.config_etag, 'event.config_etag');
    return { ...item, schema: item.schema, api_version: item.api_version, publisher_epoch: epoch(item.publisher_epoch, 'event.publisher_epoch'), sequence: safeInteger(item.sequence, 'event.sequence', 1), event_type: text(item.event_type, 'event.event_type'), timestamp: dateTime(item.timestamp, 'event.timestamp'), payload: object(item.payload, 'event.payload') };
}
export function parseEventBatch(value, expectedClient) {
    const item = object(value, 'event batch');
    exactFields(item, ['schema', 'stream', 'publisher_epoch', 'client_id', 'resync_required', 'reason', 'latest_sequence', 'oldest_available_sequence', 'newest_available_sequence', 'truncated', 'events', 'counters'], 'event batch');
    if (item.schema !== 'graphx.dashboard.events_batch.v1' || item.stream !== '/api/v1/fhss/events')
        throw new Error('event batch schema/stream mismatch');
    if (item.client_id !== expectedClient || typeof item.client_id !== 'string' || !clientPattern.test(item.client_id))
        throw new Error('event batch client mismatch');
    if (typeof item.resync_required !== 'boolean' || typeof item.truncated !== 'boolean' || typeof item.reason !== 'string' || !reasons.has(item.reason))
        throw new Error('event batch flags are malformed');
    if (!Array.isArray(item.events) || item.events.length > 256)
        throw new Error('event batch events are malformed');
    const publisherEpoch = epoch(item.publisher_epoch, 'event batch publisher_epoch');
    const latest = safeInteger(item.latest_sequence, 'event batch latest_sequence');
    const oldest = safeInteger(item.oldest_available_sequence, 'event batch oldest_available_sequence');
    const newest = safeInteger(item.newest_available_sequence, 'event batch newest_available_sequence');
    if (oldest > newest || newest > latest)
        throw new Error('event batch sequence range is incoherent');
    const events = item.events.map(parseEventEnvelope);
    for (let index = 0; index < events.length; ++index) {
        const event = events[index];
        if (event.publisher_epoch !== publisherEpoch || event.sequence < oldest || event.sequence > newest || (index && event.sequence !== events[index - 1].sequence + 1))
            throw new Error('event batch event range is incoherent');
    }
    const countersDocument = object(item.counters, 'event batch counters');
    const counterFields = ['dropped_events', 'dropped_events_total', 'coalesced_events_total', 'reconnects_total'];
    exactFields(countersDocument, counterFields, 'event batch counters');
    const counters = Object.fromEntries(counterFields.map((field) => [field, safeInteger(countersDocument[field], `event batch counters.${field}`)]));
    return { ...item, schema: item.schema, stream: item.stream, publisher_epoch: publisherEpoch, client_id: item.client_id, resync_required: item.resync_required, reason: item.reason, latest_sequence: latest, oldest_available_sequence: oldest, newest_available_sequence: newest, truncated: item.truncated, events, counters };
}
export function parseHello(value) {
    const item = object(value, 'hello');
    exactFields(item, ['schema', 'api_version', 'publisher_epoch', 'latest_sequence', 'oldest_available_sequence', 'heartbeat_interval_ms', 'limits'], 'hello');
    if (item.schema !== 'graphx.dashboard.websocket_hello.v1' || item.api_version !== 'v1')
        throw new Error('hello schema/API mismatch');
    const latest = safeInteger(item.latest_sequence, 'hello.latest_sequence');
    const oldest = safeInteger(item.oldest_available_sequence, 'hello.oldest_available_sequence');
    if (oldest > latest + 1)
        throw new Error('hello sequence range is incoherent');
    const limitsDocument = object(item.limits, 'hello.limits');
    const limitBounds = {
        frame_bytes: [125, 262144], message_bytes: [125, 262144], fragments_per_message: [1, 1024], commands_per_second: [1, 65536], events_per_second: [1, 65536], replay_events: [1, 4096], replay_bytes: [125, 8388608], queue_events: [1, 4096], queue_bytes: [125, 8388608], idle_timeout_ms: [1, 3600000], max_lifetime_ms: [1, 86400000]
    };
    exactFields(limitsDocument, Object.keys(limitBounds), 'hello.limits');
    const limits = Object.fromEntries(Object.entries(limitBounds).map(([field, [minimum, maximum]]) => [field, boundedInteger(limitsDocument[field], `hello.limits.${field}`, minimum, maximum)]));
    return { ...item, schema: item.schema, api_version: item.api_version, publisher_epoch: epoch(item.publisher_epoch, 'hello.publisher_epoch'), latest_sequence: latest, oldest_available_sequence: oldest, heartbeat_interval_ms: boundedInteger(item.heartbeat_interval_ms, 'hello.heartbeat_interval_ms', 1, 3600000), limits };
}
export function parseHeartbeat(value, expectedEpoch) {
    const item = object(value, 'heartbeat');
    exactFields(item, ['schema', 'publisher_epoch', 'timestamp'], 'heartbeat');
    if (item.schema !== 'graphx.dashboard.websocket_heartbeat.v1')
        throw new Error('heartbeat schema mismatch');
    const publisherEpoch = epoch(item.publisher_epoch, 'heartbeat.publisher_epoch');
    if (expectedEpoch && publisherEpoch !== expectedEpoch)
        throw new Error('heartbeat epoch mismatch');
    return { schema: item.schema, publisher_epoch: publisherEpoch, timestamp: dateTime(item.timestamp, 'heartbeat.timestamp') };
}
export function parseResyncRequired(value) {
    const item = object(value, 'resync');
    exactFields(item, ['schema', 'publisher_epoch', 'latest_sequence', 'snapshot_url', 'reason'], 'resync');
    if (item.schema !== 'graphx.dashboard.websocket_resync_required.v1' || item.snapshot_url !== '/api/v1/fhss/snapshot' || typeof item.reason !== 'string' || !resyncReasons.has(item.reason))
        throw new Error('resync contract mismatch');
    return { schema: item.schema, publisher_epoch: epoch(item.publisher_epoch, 'resync.publisher_epoch'), latest_sequence: safeInteger(item.latest_sequence, 'resync.latest_sequence'), snapshot_url: item.snapshot_url, reason: item.reason };
}
export function classifySequence(state, epochValue, sequence) {
    if (!Number.isSafeInteger(sequence) || sequence < 1 || !epochPattern.test(epochValue))
        return 'resync';
    if (state.epoch && epochValue !== state.epoch)
        return 'resync';
    if (sequence <= state.lastSequence)
        return 'duplicate';
    if (state.lastSequence > 0 && sequence !== state.lastSequence + 1)
        return 'gap';
    return 'accept';
}
export function nextReconnect(attempt, maximum = 12, maximumDelayMs = 30000) {
    if (!Number.isInteger(attempt) || attempt < 0 || attempt >= maximum)
        return { allowed: false, attempt: maximum, delayMs: 0 };
    return { allowed: true, attempt: attempt + 1, delayMs: Math.min(maximumDelayMs, 500 * (2 ** Math.min(attempt, 6))) };
}
