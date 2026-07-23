import { useEffect, useRef, useState } from 'react';
import { parsers, requestJson } from './api';
import {
  classifySequence, nextReconnect, parseEventBatch, parseEventEnvelope,
  parseHeartbeat, parseHello, parseResyncRequired, type SequenceState,
} from './transportState';

const CLIENT = 'fhss-react';
const EPOCH_KEY = 'graphx.fhss.publisher_epoch';
const SEQUENCE_KEY = 'graphx.fhss.last_sequence';

export function restoredSequenceState(storage: Pick<Storage, 'getItem'>): SequenceState {
  const epoch = storage.getItem(EPOCH_KEY) ?? '';
  const sequence = Number(storage.getItem(SEQUENCE_KEY) ?? 0);
  return { epoch, lastSequence: Number.isSafeInteger(sequence) && sequence >= 0 ? sequence : 0 };
}

export function persistSequenceState(storage: Pick<Storage, 'setItem'>, state: SequenceState): void {
  storage.setItem(EPOCH_KEY, state.epoch); storage.setItem(SEQUENCE_KEY, String(state.lastSequence));
}

export function useEventTransport(onRefresh: () => void) {
  const [status, setStatus] = useState('Event transport: connecting');
  const state = useRef<SequenceState>(restoredSequenceState(sessionStorage));
  const refresh = useRef(onRefresh); refresh.current = onRefresh;
  useEffect(() => {
    let stopped = false; let socket: WebSocket | undefined; let pollTimer = 0; let reconnectTimer = 0; let stableTimer = 0;
    let reconnectAttempt = 0; let replayThrough = 0; let resyncRequired = false;
    const store = () => persistSequenceState(sessionStorage, state.current);
    const resync = async (expectedEpoch = '') => {
      const snapshot = parsers.snapshot(await requestJson('/api/v1/fhss/snapshot'));
      if (expectedEpoch && snapshot.publisher_epoch !== expectedEpoch) throw new Error('snapshot publisher epoch mismatch');
      state.current = { epoch: snapshot.publisher_epoch, lastSequence: snapshot.latest_sequence }; store(); resyncRequired = false;
      setStatus(`Event transport: coherent resync at sequence ${snapshot.latest_sequence}`); refresh.current();
    };
    const accept = async (value: unknown, delivery: 'websocket' | 'polling') => {
      const document = parseEventEnvelope(value); const decision = classifySequence(state.current, document.publisher_epoch, document.sequence);
      if (decision === 'gap' || decision === 'resync') { resyncRequired = true; await resync(document.publisher_epoch); return; }
      if (decision === 'duplicate') return;
      state.current = { epoch: document.publisher_epoch, lastSequence: document.sequence }; store();
      const replayed = delivery === 'websocket' && document.sequence <= replayThrough;
      setStatus(replayed ? `Event transport: WebSocket replay through sequence ${document.sequence}` : `Event transport: ${delivery} sequence ${document.sequence}`); refresh.current();
    };
    const schedulePoll = () => {
      window.clearTimeout(pollTimer);
      pollTimer = window.setTimeout(async () => {
        if (stopped || socket?.readyState === WebSocket.OPEN) return;
        try {
          const batch = parseEventBatch(await requestJson(`/api/v1/fhss/events?client_id=${CLIENT}&last_sequence=${state.current.lastSequence}`), CLIENT);
          if (batch.resync_required || (state.current.epoch && batch.publisher_epoch !== state.current.epoch)) await resync(batch.publisher_epoch);
          else for (const event of batch.events) await accept(event, 'polling');
          setStatus(`Event transport: bounded polling at sequence ${state.current.lastSequence}; reconnecting`);
        } catch (error) { resyncRequired = true; setStatus(`Event transport: polling error — ${String(error)}`); }
        schedulePoll();
      }, 2000);
    };
    const connect = () => {
      if (stopped) return;
      const scheme = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(`${scheme}//${window.location.host}/api/v1/fhss/events/stream`);
      socket.addEventListener('open', () => { window.clearTimeout(pollTimer); setStatus('Event transport: WebSocket connected'); });
      socket.addEventListener('message', (message) => { void (async () => {
        try {
          const raw: unknown = JSON.parse(String(message.data)); const candidate = raw as { schema?: unknown };
          if (candidate.schema === 'graphx.dashboard.websocket_hello.v1') {
            const hello = parseHello(raw); replayThrough = hello.latest_sequence;
            if (resyncRequired || (state.current.epoch && state.current.epoch !== hello.publisher_epoch)
                || (state.current.lastSequence && state.current.lastSequence < hello.oldest_available_sequence - 1)
                || state.current.lastSequence > hello.latest_sequence) await resync(hello.publisher_epoch);
            socket?.send(JSON.stringify({ action: 'subscribe', client_id: CLIENT, publisher_epoch: state.current.epoch, last_sequence: state.current.lastSequence }));
            window.clearTimeout(stableTimer); stableTimer = window.setTimeout(() => { if (socket?.readyState === WebSocket.OPEN) reconnectAttempt = 0; }, Math.max(5000, hello.heartbeat_interval_ms * 2));
          } else if (candidate.schema === 'graphx.dashboard.websocket_heartbeat.v1') {
            const heartbeat = parseHeartbeat(raw, state.current.epoch); socket?.send(JSON.stringify({ action: 'heartbeat_ack', publisher_epoch: heartbeat.publisher_epoch }));
          } else if (candidate.schema === 'graphx.dashboard.websocket_resync_required.v1') {
            const notice = parseResyncRequired(raw); await resync(notice.publisher_epoch);
          } else await accept(raw, 'websocket');
        } catch { resyncRequired = true; socket?.close(1002, 'invalid event contract'); }
      })(); });
      socket.addEventListener('close', () => {
        window.clearTimeout(stableTimer); if (stopped) return;
        schedulePoll(); const reconnect = nextReconnect(reconnectAttempt);
        if (!reconnect.allowed) { setStatus('Event transport: bounded polling fallback; reconnect budget exhausted — reload to retry'); return; }
        reconnectAttempt = reconnect.attempt; setStatus(`Event transport: bounded polling fallback; reconnect ${reconnectAttempt}/12`);
        reconnectTimer = window.setTimeout(connect, reconnect.delayMs);
      });
      socket.addEventListener('error', schedulePoll);
    };
    connect();
    return () => { stopped = true; window.clearTimeout(pollTimer); window.clearTimeout(reconnectTimer); window.clearTimeout(stableTimer); socket?.close(1000, 'dashboard unmounted'); };
  }, []);
  return status;
}
