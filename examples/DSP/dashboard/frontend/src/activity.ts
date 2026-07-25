import type { MetricsResource, SnapshotResource } from './api';
import {
  isPresentationBundleEdge, type DisplayTopologyEdge, type DisplayTopologyModel,
} from './topology';

export const MAX_VISIBLE_ACTIVITY_MARKERS = 32;
export const MAX_ACTIVITY_MARKERS_PER_EDGE = 1;
export const MAX_AGGREGATE_UPDATES_PER_SECOND = 10;
export const MAX_VISUAL_REFRESH_HZ = 12;
export const MAX_ACTIVITY_HISTORY = 60;
export const MAX_QUEUED_PRESENTATION_UPDATES = 2;
export const MAX_EXPANDED_DETECTORS = 64;
export const PRESENTATION_UPDATE_INTERVAL_MS =
  1000 / Math.min(MAX_AGGREGATE_UPDATES_PER_SECOND, MAX_VISUAL_REFRESH_HZ);

export type MessageClass =
  | 'iq-block'
  | 'channel-iq'
  | 'pulse-candidate'
  | 'decoder-data'
  | 'assembled-message'
  | 'unknown/unclassified';

export interface EdgeActivity {
  edgeId: string;
  availability: 'available' | 'unavailable';
  unavailableReason: string | null;
  messageClass: MessageClass;
  messages: number | null;
  messagesPerSecond: number | null;
  intervalMs: number | null;
  memberCount: number;
  availableMembers: number;
}

export interface ActivityFrame {
  captureId: string;
  sampledAtMonotonicMs: number;
  edges: ReadonlyMap<string, EdgeActivity>;
}

export class BoundedActivityBuffer {
  private history: ActivityFrame[] = [];
  private queued?: ActivityFrame;
  private lastAcceptedMs = Number.NEGATIVE_INFINITY;
  private coalesced = 0;

  offer(frame: ActivityFrame, presentationNowMs: number): boolean {
    if (presentationNowMs - this.lastAcceptedMs < PRESENTATION_UPDATE_INTERVAL_MS) {
      this.queued = frame;
      this.coalesced += 1;
      return false;
    }
    this.accept(frame, presentationNowMs);
    return true;
  }

  promote(presentationNowMs: number): boolean {
    if (!this.queued
        || presentationNowMs - this.lastAcceptedMs < PRESENTATION_UPDATE_INTERVAL_MS) return false;
    const next = this.queued;
    this.accept(next, presentationNowMs);
    return true;
  }

  private accept(frame: ActivityFrame, presentationNowMs: number) {
    this.lastAcceptedMs = presentationNowMs;
    this.queued = undefined;
    this.history = [...this.history, frame].slice(-MAX_ACTIVITY_HISTORY);
  }

  latest(): ActivityFrame | undefined { return this.history.at(-1); }
  historySize(): number { return this.history.length; }
  queuedSize(): number { return this.queued ? 1 : 0; }
  coalescedUpdates(): number { return this.coalesced; }
  delayUntilPromotion(presentationNowMs: number): number {
    return Math.max(0, PRESENTATION_UPDATE_INTERVAL_MS
      - (presentationNowMs - this.lastAcceptedMs));
  }
  reset(): void {
    this.history = []; this.queued = undefined;
    this.lastAcceptedMs = Number.NEGATIVE_INFINITY; this.coalesced = 0;
  }
}

function asInteger(value: unknown): number | undefined {
  return Number.isSafeInteger(value) && Number(value) >= 0 ? Number(value) : undefined;
}

export function classifyMessage(messageType: unknown): MessageClass {
  const decorated = typeof messageType === 'string' ? messageType.trim() : '';
  const type = decorated.replace(/>+$/u, '').split('::').at(-1)?.toLowerCase() ?? '';
  const known: Record<string, MessageClass> = {
    fhsssyntheticiqoutputpacket: 'iq-block',
    fhssdownconvertediqpacket: 'iq-block',
    fhsschannelizediqpacket: 'channel-iq',
    fhssperchannelpulseevidencepacket: 'pulse-candidate',
    fhsspulsecandidateevidencepacket: 'pulse-candidate',
    fhsscpsmbranchmetricpacket: 'decoder-data',
    fhsscpsmsymboldecisionpacket: 'decoder-data',
    fhssdecodedpulsewordspacket: 'decoder-data',
    fhssassembledmessagepacket: 'assembled-message',
  };
  return known[type] ?? 'unknown/unclassified';
}

function identityMismatch(previous: MetricsResource, current: MetricsResource): string | undefined {
  if (previous.active_generation !== current.active_generation) return 'graph generation changed';
  if (previous.active_run_epoch !== current.active_run_epoch) return 'run epoch changed';
  if (previous.active_config_revision !== current.active_config_revision) return 'configuration revision changed';
  if (previous.active_config_etag !== current.active_config_etag) return 'configuration ETag changed';
  return undefined;
}

function unavailable(edgeId: string, messageClass: MessageClass, reason: string): EdgeActivity {
  return {
    edgeId, availability: 'unavailable', unavailableReason: reason, messageClass,
    messages: null, messagesPerSecond: null, intervalMs: null, memberCount: 1,
    availableMembers: 0,
  };
}

function counterDefinition(metrics: MetricsResource): Record<string, unknown> | undefined {
  const definitions = Array.isArray(metrics.metric_definitions) ? metrics.metric_definitions : [];
  return definitions.find((value): value is Record<string, unknown> =>
    Boolean(value && typeof value === 'object'
      && (value as Record<string, unknown>).scope === 'edge'
      && (value as Record<string, unknown>).name === 'messages_dequeued'));
}

function counterDefinitionProblem(
  previous: MetricsResource | undefined,
  current: MetricsResource,
): string | undefined {
  const currentDefinition = counterDefinition(current);
  const previousDefinition = previous ? counterDefinition(previous) : undefined;
  if (!currentDefinition || !previousDefinition) return 'messages_dequeued metric definition is missing';
  const expected: Record<string, unknown> = {
    field: '/edges/*/messages_dequeued',
    scope: 'edge',
    name: 'messages_dequeued',
    kind: 'counter',
    unit: 'message',
    monotonic: true,
    availability: 'explicit',
    capture: 'atomic relaxed-load within one collector snapshot',
    reset: 'new_runtime_manager',
    aggregation: 'direct atomic edge counter; never re-summed by dashboard',
    overflow: 'unavailable_above_javascript_safe_integer',
    numeric_representation: 'non_negative_javascript_safe_integer',
  };
  for (const [field, value] of Object.entries(expected)) {
    if (currentDefinition[field] !== value) return `messages_dequeued metric definition has invalid ${field}`;
    if (previousDefinition[field] !== value) return `previous messages_dequeued metric definition has invalid ${field}`;
    if (previousDefinition[field] !== currentDefinition[field]) {
      return `messages_dequeued metric definition changed for ${field}`;
    }
  }
  return undefined;
}

export function deriveAuthoritativeActivity(
  previous: MetricsResource | undefined,
  current: MetricsResource | undefined,
): ActivityFrame | undefined {
  if (!current) return undefined;
  const sampledAt = asInteger(current.sampled_at_monotonic_ms) ?? 0;
  const result = new Map<string, EdgeActivity>();
  const currentEdges = new Map(current.edges.map((edge) => [String(edge.edge_id), edge]));
  const previousEdges = new Map((previous?.edges ?? []).map((edge) => [String(edge.edge_id), edge]));
  const mismatch = previous ? identityMismatch(previous, current) : 'a previous compatible capture is required';
  const definitionProblem = previous ? counterDefinitionProblem(previous, current) : undefined;
  const interval = current.collection_interval as { state?: unknown; duration_ms?: unknown; clock?: unknown } | undefined;
  const rateAvailability = current.rate_availability as { state?: unknown; reason?: unknown } | undefined;
  const previousSampledAt = asInteger(previous?.sampled_at_monotonic_ms);
  const intervalMs = previousSampledAt !== undefined && sampledAt > previousSampledAt
    ? sampledAt - previousSampledAt : undefined;

  for (const [edgeId, edge] of currentEdges) {
    const messageClass = classifyMessage(edge.message_type);
    const prior = previousEdges.get(edgeId);
    if (mismatch) { result.set(edgeId, unavailable(edgeId, messageClass, mismatch)); continue; }
    if (definitionProblem) { result.set(edgeId, unavailable(edgeId, messageClass, definitionProblem)); continue; }
    if (rateAvailability?.state !== 'available') {
      result.set(edgeId, unavailable(edgeId, messageClass,
        String(rateAvailability?.reason ?? 'qualified rate availability is unavailable'))); continue;
    }
    if (interval?.state !== 'available' || interval.clock !== 'steady_clock'
        || (asInteger(interval.duration_ms) ?? 0) < 1) {
      result.set(edgeId, unavailable(edgeId, messageClass, 'server steady-clock collection interval is unavailable')); continue;
    }
    if (!intervalMs) {
      result.set(edgeId, unavailable(edgeId, messageClass, 'sample is stale or server-monotonic time did not advance')); continue;
    }
    if (edge.availability !== 'available') {
      result.set(edgeId, unavailable(edgeId, messageClass, String(edge.unavailable_reason ?? 'metric is unavailable'))); continue;
    }
    if (!prior || prior.availability !== 'available') {
      result.set(edgeId, unavailable(edgeId, messageClass, 'previous edge sample is unavailable')); continue;
    }
    const currentCount = asInteger(edge.messages_dequeued);
    const previousCount = asInteger(prior.messages_dequeued);
    if (currentCount === undefined || previousCount === undefined) {
      result.set(edgeId, unavailable(edgeId, messageClass, 'counter is missing, overflowed, or not a JavaScript safe integer')); continue;
    }
    if (currentCount < previousCount) {
      result.set(edgeId, unavailable(edgeId, messageClass, 'monotonic counter reset')); continue;
    }
    const messages = currentCount - previousCount;
    result.set(edgeId, {
      edgeId, availability: 'available', unavailableReason: null, messageClass,
      messages, messagesPerSecond: messages * 1000 / intervalMs, intervalMs,
      memberCount: 1, availableMembers: 1,
    });
  }
  return { captureId: current.capture_id, sampledAtMonotonicMs: sampledAt, edges: result };
}

function aggregateBundle(edge: DisplayTopologyEdge, frame: ActivityFrame | undefined): EdgeActivity {
  if (!isPresentationBundleEdge(edge)) throw new Error('aggregateBundle requires a presentation bundle');
  const members = edge.authoritativeEdgeIds.map((id) => frame?.edges.get(id));
  const available = members.filter((member): member is EdgeActivity => member?.availability === 'available');
  const classes = new Set(available.map(({ messageClass }) => messageClass));
  const messageClass = classes.size === 1 ? [...classes][0]! : 'unknown/unclassified';
  if (available.length !== members.length) {
    return {
      ...unavailable(edge.id, messageClass, `${available.length}/${members.length} authoritative members available; aggregate total withheld`),
      memberCount: members.length, availableMembers: available.length,
    };
  }
  const intervalMs = available[0]?.intervalMs ?? null;
  if (available.some((member) => member.intervalMs !== intervalMs)) {
    return {
      ...unavailable(edge.id, messageClass, 'authoritative members have incompatible intervals'),
      memberCount: members.length, availableMembers: available.length,
    };
  }
  return {
    edgeId: edge.id, availability: 'available', unavailableReason: null, messageClass,
    messages: available.reduce((sum, member) => sum + (member.messages ?? 0), 0),
    messagesPerSecond: available.reduce((sum, member) => sum + (member.messagesPerSecond ?? 0), 0),
    intervalMs, memberCount: members.length, availableMembers: available.length,
  };
}

export function activityForDisplayModel(
  model: DisplayTopologyModel,
  frame: ActivityFrame | undefined,
): ReadonlyMap<string, EdgeActivity> {
  return new Map(model.edges.map((edge) => [
    edge.id,
    isPresentationBundleEdge(edge)
      ? aggregateBundle(edge, frame)
      : frame?.edges.get(edge.id) ?? unavailable(edge.id, 'unknown/unclassified', 'no compatible activity sample'),
  ]));
}

export function coherentMetrics(snapshot: SnapshotResource | undefined): MetricsResource | undefined {
  if (!snapshot || snapshot.coherence.state !== 'coherent') return undefined;
  const metrics = snapshot.metrics;
  if (metrics.active_generation !== snapshot.generation
      || metrics.active_run_epoch !== snapshot.run_epoch
      || metrics.active_config_revision !== snapshot.config_revision
      || metrics.active_config_etag !== snapshot.config_etag
      || snapshot.coherence.metric_capture_id !== metrics.capture_id) return undefined;
  return metrics;
}

export function animatedEdgeIds(
  activity: ReadonlyMap<string, EdgeActivity>,
  paused: boolean,
  reducedMotion: boolean,
): ReadonlySet<string> {
  if (paused || reducedMotion) return new Set();
  return new Set([...activity.values()]
    .filter((edge) => edge.availability === 'available' && (edge.messages ?? 0) > 0)
    .sort((left, right) => (right.messagesPerSecond ?? 0) - (left.messagesPerSecond ?? 0))
    .slice(0, MAX_VISIBLE_ACTIVITY_MARKERS)
    .map(({ edgeId }) => edgeId));
}
