import { useEffect, useMemo, useRef } from 'react';
import type { ObservationResource } from './api';
import type { Selection } from './GraphView';
import {
  DETECTOR_BANK_ID, detectorForPhysicalChannel,
  type DetectorMapping, type FHSSPresentation,
} from './fhssPresentation';
import { StructuredValue } from './Operations';
import { nextSelectionIndex } from './topology';

function observedCounts(observation: ObservationResource | undefined): number[] | undefined {
  if (!observation || observation.availability.state !== 'available') return undefined;
  const counts = Array.from({ length: 64 }, () => 0);
  for (const rawPulse of observation.observed_pulses) {
    if (!rawPulse || typeof rawPulse !== 'object' || Array.isArray(rawPulse)) continue;
    const channel = (rawPulse as Record<string, unknown>).physical_channel_index;
    if (Number.isInteger(channel) && Number(channel) >= 0 && Number(channel) < 64) {
      const index = Number(channel);
      counts[index] = (counts[index] ?? 0) + 1;
    }
  }
  return counts;
}

export function DetectorInspector({ presentation, selection, observation, onSelection }: {
  presentation: FHSSPresentation;
  selection: Selection | null;
  observation?: ObservationResource;
  onSelection: (selection: Selection) => void;
}) {
  const group = presentation.detectorBank;
  const detector = selection?.kind === 'node'
    ? group?.members.find((entry) => entry.node.id === selection.id)
    : undefined;
  const groupedBoundary = selection?.kind === 'edge' && group
    ? presentation.collapsed.edges.find((edge) => edge.id === selection.id
      && (edge.source_node_id === group.id || edge.target_node_id === group.id))
    : undefined;
  if (groupedBoundary && group) {
    const incoming = groupedBoundary.target_node_id === group.id;
    const mappings = group.members.map((entry) => incoming
      ? {
          graph_edge_id: entry.incomingEdge.id,
          channelizer_output_port: entry.channelizerOutputPort,
          detector_node_id: entry.node.id,
          detector_input_port: entry.detectorInputPort,
        }
      : {
          graph_edge_id: entry.outgoingEdge.id,
          detector_node_id: entry.node.id,
          detector_output_port: entry.detectorOutputPort,
          merge_input_port: entry.mergeInputPort,
        });
    return <aside className="inspector detector-inspector" aria-labelledby="detector-inspector-heading">
      <h2 id="detector-inspector-heading">Grouped boundary inspector</h2>
      <dl>
        <dt>Display edge</dt><dd>{groupedBoundary.id}</dd>
        <dt>Presentation only</dt><dd>Yes — this summarizes 64 authoritative exact-port edges.</dd>
        <dt>Direction</dt><dd>{incoming ? 'Channelizer fan-out into detector bank' : 'Detector bank fan-in to pulse merge'}</dd>
      </dl>
      <details open><summary>Authoritative exact-port mappings (64)</summary><StructuredValue value={mappings} /></details>
    </aside>;
  }
  if (selection?.kind === 'node' && selection.id === DETECTOR_BANK_ID && group) {
    return <aside className="inspector detector-inspector" aria-labelledby="detector-inspector-heading">
      <h2 id="detector-inspector-heading">Detector-bank inspector</h2>
      <dl>
        <dt>Presentation group</dt><dd>{group.id}</dd>
        <dt>Presentation only</dt><dd>Yes — GraphX execution remains the authoritative 75-node, 137-edge graph.</dd>
        <dt>Members</dt><dd>{group.members.length}</dd>
        <dt>Boundary nodes</dt><dd>{group.channelizerNodeId} → {group.mergeNodeId}</dd>
        <dt>Physical channels</dt><dd>0–63</dd>
        <dt>Exact boundary ports</dt><dd>channelizer outputs 0–63; merge inputs 1–64</dd>
      </dl>
      <details><summary>Authoritative member identities</summary><StructuredValue value={group.authoritativeNodeIds} /></details>
      <details><summary>Authoritative boundary edge identities</summary><StructuredValue value={group.authoritativeEdgeIds} /></details>
    </aside>;
  }
  if (!detector) return null;
  const counts = observedCounts(observation);
  return <aside className="inspector detector-inspector" aria-labelledby="detector-inspector-heading">
    <h2 id="detector-inspector-heading">Selected detector</h2>
    <dl>
      <dt>Authoritative node ID</dt><dd>{detector.node.id}</dd>
      <dt>Node type</dt><dd>{detector.node.type}</dd>
      <dt>Presentation group</dt><dd>{DETECTOR_BANK_ID}</dd>
      <dt>Logical channel</dt><dd>{detector.logicalChannel}</dd>
      <dt>Physical channel</dt><dd>{detector.physicalChannel}</dd>
      <dt>Channelizer predecessor</dt><dd>{detector.channelizerNodeId}, output port {detector.channelizerOutputPort}</dd>
      <dt>Detector ports</dt><dd>input {detector.detectorInputPort}; output {detector.detectorOutputPort}</dd>
      <dt>Pulse-merge successor</dt><dd>{detector.mergeNodeId}, input port {detector.mergeInputPort}</dd>
      <dt>Receiver observation</dt><dd>{counts ? `${counts[detector.physicalChannel]} observed pulses` : 'Unavailable (not zero activity)'}</dd>
    </dl>
    <details><summary>Relevant detector configuration</summary><StructuredValue value={detector.node.configuration} /></details>
    <div className="button-row">
      <button type="button" onClick={() => onSelection({ kind: 'node', id: detector.node.id })}>Select in semantic topology</button>
      <button type="button" onClick={() => onSelection({ kind: 'node', id: detector.node.id })}>Select corresponding channel</button>
    </div>
  </aside>;
}

function DetectorCell({ detector, count, selected, row, column, onSelect, buttonRef, onKeyDown }: {
  detector: DetectorMapping;
  count: number | undefined;
  selected: boolean;
  row: number;
  column: number;
  onSelect: () => void;
  buttonRef?: React.Ref<HTMLButtonElement>;
  onKeyDown: (event: React.KeyboardEvent) => void;
}) {
  return <button type="button" role="gridcell" aria-selected={selected}
    aria-rowindex={row} aria-colindex={column}
    className="detector-cell" ref={buttonRef} onClick={onSelect} onKeyDown={onKeyDown}>
    <strong>Ch {detector.physicalChannel}</strong>
    <span>{count === undefined ? 'Observation unavailable' : `${count} observed pulses`}</span>
    <small>{detector.node.id} · merge in {detector.mergeInputPort}</small>
  </button>;
}

export function DetectorBankView({ presentation, expanded, onExpanded, selection, onSelection, observation }: {
  presentation: FHSSPresentation;
  expanded: boolean;
  onExpanded: (expanded: boolean) => void;
  selection: Selection | null;
  onSelection: (selection: Selection) => void;
  observation?: ObservationResource;
}) {
  const group = presentation.detectorBank;
  const counts = useMemo(() => observedCounts(observation), [observation]);
  const selected = selection?.kind === 'node'
    ? group?.members.findIndex((entry) => entry.node.id === selection.id) ?? -1
    : -1;
  const selectedRef = useRef<HTMLButtonElement>(null);
  useEffect(() => { if (expanded && selected >= 0) selectedRef.current?.focus({ preventScroll: true }); }, [expanded, selected]);
  if (!group) {
    return <section className="detector-bank-card" role="status">
      <h2>FHSS detector-bank presentation</h2>
      {presentation.diagnostics.map((diagnostic) => <p key={diagnostic}>{diagnostic}</p>)}
      <p>The authoritative topology remains visible without unsafe collapsing.</p>
    </section>;
  }
  const selectIndex = (index: number) => {
    const detector = group.members[index];
    if (detector) onSelection({ kind: 'node', id: detector.node.id });
  };
  const chooseByKey = (event: React.KeyboardEvent, index: number) => {
    const horizontal = event.key === 'ArrowRight' ? index + 1 : event.key === 'ArrowLeft' ? index - 1 : index;
    const vertical = event.key === 'ArrowDown' ? index + 8 : event.key === 'ArrowUp' ? index - 8 : horizontal;
    const next = event.key === 'Home' || event.key === 'End'
      ? nextSelectionIndex(index, group.members.length, event.key)
      : ((vertical % group.members.length) + group.members.length) % group.members.length;
    if (!['ArrowRight', 'ArrowLeft', 'ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    selectIndex(next);
  };
  return <section className="detector-bank-card" aria-labelledby="detector-bank-heading">
    <div className="section-heading">
      <div>
        <h2 id="detector-bank-heading">Acquisition detector bank</h2>
        <p>64 structurally recognized detectors · presentation grouping only · authoritative graph remains 75 nodes / 137 edges</p>
      </div>
      <button type="button" aria-expanded={expanded} aria-controls="detector-grid"
        onClick={() => onExpanded(!expanded)}>
        {expanded ? 'Collapse detector bank' : 'Expand 8×8 detector bank'}
      </button>
    </div>
    <p>Receiver observations: {counts ? `${counts.reduce((sum, value) => sum + value, 0)} pulses across 64 physical channels` : 'unavailable; cells do not imply zero activity'}.</p>
    {expanded && <div id="detector-grid" className="detector-grid" role="grid" aria-label="64 acquisition detectors in physical-channel order" aria-rowcount={8} aria-colcount={8}>
      {Array.from({ length: 8 }, (_, row) => <div role="row" className="detector-row" key={row}>
        {group.members.slice(row * 8, row * 8 + 8).map((detector, column) => {
          const index = row * 8 + column;
          return <DetectorCell key={detector.node.id} detector={detector} count={counts?.[detector.physicalChannel]}
            row={row + 1} column={column + 1}
            selected={selected === index} buttonRef={selected === index ? selectedRef : undefined}
            onSelect={() => selectIndex(index)} onKeyDown={(event) => chooseByKey(event, index)} />;
        })}
      </div>)}
    </div>}
    {!expanded && <button className="collapsed-bank-summary" type="button"
      aria-pressed={selection?.kind === 'node' && selection.id === DETECTOR_BANK_ID}
      onClick={() => onSelection({ kind: 'node', id: DETECTOR_BANK_ID })}>
      Detector bank collapsed: channelizer outputs 0–63 → 64 acquisition detectors → merge inputs 1–64
    </button>}
  </section>;
}

export function selectedPhysicalChannel(
  presentation: FHSSPresentation,
  selection: Selection | null,
): number | undefined {
  if (selection?.kind !== 'node') return undefined;
  return presentation.detectorBank?.members.find((entry) => entry.node.id === selection.id)?.physicalChannel;
}

export function selectPhysicalChannel(
  presentation: FHSSPresentation,
  channel: number,
  onSelection: (selection: Selection) => void,
): void {
  const detector = detectorForPhysicalChannel(presentation, channel);
  if (detector) onSelection({ kind: 'node', id: detector.node.id });
}
