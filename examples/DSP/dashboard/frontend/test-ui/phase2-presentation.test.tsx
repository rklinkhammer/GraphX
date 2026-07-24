import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { parsers } from '../src/api';
import {
  authoritativeGraphDocument, selectionIsPresent, SELECTION_CLEARED_MESSAGE,
} from '../src/App';
import { DetectorBankView, DetectorInspector } from '../src/DetectorBankView';
import { selectPhysicalChannel } from '../src/DetectorBankView';
import { Visualization } from '../src/Operations';
import {
  DETECTOR_BANK_ID, DETECTOR_TYPE, recognizeDetectorBank, toFHSSPresentation,
} from '../src/fhssPresentation';
import { isPresentationBundleEdge, toTopology } from '../src/topology';
import { parseGraphResource, type GraphContract } from '../src/domain';

function independentGraph(): GraphContract {
  const nodes: GraphContract['nodes'] = [
    { id: 'binary', type: 'FHSSBinaryIqFileSourceNode', node_config: {} },
    { id: 'translate', type: 'FHSSDownconverterNode', node_config: {} },
    {
      id: 'fanout', type: 'FHSSProductionCandidateChannelizerNode',
      node_config: {
        channel_ids: Array.from({ length: 64 }, (_, channel) => channel),
        receiver_frequency_indices: Array.from({ length: 64 }, (_, channel) => channel),
      },
    },
    ...Array.from({ length: 64 }, (_, channel) => ({
      id: `renamed-member-${String(channel).padStart(2, '0')}`,
      type: DETECTOR_TYPE,
      node_config: { detector_id: channel, threshold: 8, enabled: true },
    })),
    { id: 'fanin', type: 'FHSSPulseMergeNode', node_config: {} },
    { id: 'candidate', type: 'FHSSPulseCandidateNode', node_config: {} },
    { id: 'metric', type: 'CPSMBranchMetricNode', node_config: {} },
    { id: 'viterbi', type: 'CPSMViterbiDecoderNode', node_config: {} },
    { id: 'word', type: 'FHSSPulseWordDecoderNode', node_config: {} },
    { id: 'preamble', type: 'FHSSPreambleDetectorNode', node_config: {} },
    { id: 'assembler', type: 'FHSSMessageAssemblerNode', node_config: {} },
    { id: 'sink', type: 'FHSSMessageSinkNode', node_config: {} },
  ];
  const edges: GraphContract['edges'] = [
    { source_node_id: 'binary', source_port: 0, target_node_id: 'translate', target_port: 0 },
    { source_node_id: 'translate', source_port: 0, target_node_id: 'fanout', target_port: 0 },
    ...Array.from({ length: 64 }, (_, channel) => ({
      source_node_id: 'fanout', source_port: channel,
      target_node_id: `renamed-member-${String(channel).padStart(2, '0')}`, target_port: 0,
    })),
    ...Array.from({ length: 64 }, (_, channel) => ({
      source_node_id: `renamed-member-${String(channel).padStart(2, '0')}`, source_port: 0,
      target_node_id: 'fanin', target_port: channel + 1,
    })),
    { source_node_id: 'fanin', source_port: 1, target_node_id: 'candidate', target_port: 0 },
    { source_node_id: 'candidate', source_port: 0, target_node_id: 'metric', target_port: 0 },
    { source_node_id: 'metric', source_port: 0, target_node_id: 'viterbi', target_port: 0 },
    { source_node_id: 'viterbi', source_port: 0, target_node_id: 'word', target_port: 0 },
    { source_node_id: 'word', source_port: 0, target_node_id: 'preamble', target_port: 0 },
    { source_node_id: 'preamble', source_port: 0, target_node_id: 'assembler', target_port: 0 },
    { source_node_id: 'assembler', source_port: 0, target_node_id: 'sink', target_port: 0 },
  ];
  return { nodes, edges };
}

const copyGraph = (): GraphContract => structuredClone(independentGraph());

describe('Phase 2 typed FHSS presentation adapter', () => {
  it('recognizes the canonical binary-IQ receiver graph without generator truth', () => {
    const canonical = JSON.parse(readFileSync(
      resolve(process.cwd(), '../../../../libdsp/config/fhss_phase2_binary_iq_receiver.json'),
      'utf8',
    )) as GraphContract;
    const presentation = toFHSSPresentation(toTopology(canonical));
    expect(presentation.detectorBank?.members).toHaveLength(64);
    expect(presentation.detectorBank?.members[0]).toMatchObject({
      logicalChannel: 0, physicalChannel: 0, channelizerOutputPort: 0, mergeInputPort: 1,
    });
    expect(presentation.detectorBank?.members[63]).toMatchObject({
      logicalChannel: 63, physicalChannel: 63, channelizerOutputPort: 63, mergeInputPort: 64,
    });
    expect('messages' in canonical).toBe(false);
  });

  it('recognizes renamed detectors structurally and preserves every exact mapping', () => {
    const source = independentGraph();
    const before = JSON.stringify(source);
    const presentation = toFHSSPresentation(toTopology(source));
    expect(JSON.stringify(source)).toBe(before);
    expect(presentation.detectorBank?.members).toHaveLength(64);
    expect(presentation.authoritative.nodes).toHaveLength(75);
    expect(presentation.authoritative.edges).toHaveLength(137);
    expect(presentation.collapsed.nodes).toHaveLength(12);
    expect(presentation.collapsed.edges).toHaveLength(11);
    expect(presentation.expanded.nodes).toHaveLength(76);
    expect(presentation.expanded.edges).toHaveLength(137);
    expect(presentation.collapsed.nodes.map((node) => node.id)).toEqual([
      'binary', 'translate', 'fanout', DETECTOR_BANK_ID, 'fanin', 'candidate',
      'metric', 'viterbi', 'word', 'preamble', 'assembler', 'sink',
    ]);
    const bundles = presentation.collapsed.edges.filter(isPresentationBundleEdge);
    expect(bundles).toHaveLength(2);
    expect(bundles.every((edge) =>
      !('source_port' in edge) && !('target_port' in edge)
      && edge.authoritativeEdgeIds.length === 64 && edge.mappings.length === 64)).toBe(true);
    expect(new Set(bundles.flatMap((edge) => edge.authoritativeEdgeIds))).toEqual(
      new Set(presentation.detectorBank?.authoritativeEdgeIds),
    );
    presentation.detectorBank?.members.forEach((mapping, physicalChannel) => {
      expect(mapping.logicalChannel).toBe(physicalChannel);
      expect(mapping.physicalChannel).toBe(physicalChannel);
      expect(mapping.channelizerOutputPort).toBe(physicalChannel);
      expect(mapping.detectorInputPort).toBe(0);
      expect(mapping.detectorOutputPort).toBe(0);
      expect(mapping.mergeInputPort).toBe(physicalChannel + 1);
      expect(mapping.incomingEdge.sourceHandle).toBe(`out-${physicalChannel}`);
      expect(mapping.outgoingEdge.targetHandle).toBe(`in-${physicalChannel + 1}`);
      expect(presentation.detectorBank?.authoritativeEdgeIds).toContain(mapping.incomingEdge.id);
      expect(presentation.detectorBank?.authoritativeEdgeIds).toContain(mapping.outgoingEdge.id);
      const child = presentation.expanded.nodes.find(({ id }) => id === mapping.node.id);
      expect(child?.parentId).toBe(DETECTOR_BANK_ID);
      expect(child?.presentationRole).toBe('group-member');
      expect(presentation.expanded.edges).toContain(mapping.incomingEdge);
      expect(presentation.expanded.edges).toContain(mapping.outgoingEdge);
    });
    expect(presentation.expanded.edges).toContain(
      presentation.detectorBank?.members[63]?.incomingEdge,
    );
    expect(presentation.expanded.edges).toContain(
      presentation.detectorBank?.members[63]?.outgoingEdge,
    );
  });

  it.each([
    ['missing member', (graph: GraphContract) => { graph.nodes.splice(3, 1); graph.edges = graph.edges.filter((edge) => edge.target_node_id !== 'renamed-member-00' && edge.source_node_id !== 'renamed-member-00'); }],
    ['duplicate channel', (graph: GraphContract) => { graph.nodes[4].node_config = { ...graph.nodes[4].node_config, detector_id: 0 }; }],
    ['broken port', (graph: GraphContract) => { const edge = graph.edges.find((item) => item.source_node_id === 'renamed-member-63')!; edge.target_port = 63; }],
    ['incompatible shape', (graph: GraphContract) => { graph.nodes[3].node_config = { ...graph.nodes[3].node_config, extra: 1 }; }],
    ['incompatible value', (graph: GraphContract) => { graph.nodes[3].node_config = { ...graph.nodes[3].node_config, threshold: 9 }; }],
    ['inconsistent successor', (graph: GraphContract) => { graph.nodes.push({ id: 'other-merge', type: 'FHSSPulseMergeNode', node_config: {} }); const edge = graph.edges.find((item) => item.source_node_id === 'renamed-member-20')!; edge.target_node_id = 'other-merge'; }],
    ['ambiguous channelizer output', (graph: GraphContract) => { graph.edges.push({ source_node_id: 'fanout', source_port: 0, target_node_id: 'candidate', target_port: 9 }); }],
  ])('leaves ordinary topology visible for %s', (_name, mutate) => {
    const graph = copyGraph(); mutate(graph);
    const model = toTopology(graph);
    const presentation = toFHSSPresentation(model);
    expect(presentation.detectorBank).toBeUndefined();
    expect(presentation.collapsed).toBe(model);
    expect(presentation.diagnostics.join(' ')).toMatch(/not collapsed/i);
  });

  it('rejects a prefix-only false positive', () => {
    const model = toTopology({
      nodes: [{ id: 'detector_0', type: 'UnrelatedNode', node_config: {} }],
      edges: [],
    });
    expect(recognizeDetectorBank(model).group).toBeUndefined();
  });
});

describe('Phase 2 detector-bank interaction', () => {
  const presentation = toFHSSPresentation(toTopology(independentGraph()));
  const available = { state: 'available', reason: null };
  const unavailable = { state: 'unavailable', reason: 'no_receiver_samples' };
  const pulse = (observedIndex: number, channel: number) => ({
    observed_index: observedIndex, global_start_sample: observedIndex * 100,
    duration_samples: 20, logical_frequency_index: channel,
    physical_channel_index: channel, rf_frequency_hz: 1_240_000_000,
    iq_offset_frequency_hz: 0, estimated_center_frequency_hz: 1_240_000_000,
    detector_frequency_error_hz_unqualified: 0, confidence_score_uncalibrated: 1,
    viterbi_path_metric: 0, viterbi_second_best_path_metric: 1,
    decoded_value: 0, source_node_id: `renamed-member-${String(channel).padStart(2, '0')}`,
  });
  const observationDocument = (availability: typeof available | typeof unavailable, observedPulses: unknown[]) => ({
    schema: 'graphx.dashboard.fhss_receiver_observation.v1',
    semantic_class: 'observed', generation: 0, run_epoch: 0, config_revision: 0,
    config_etag: '"graphx-config-0"', observation_id: 'observation-g0-r0',
    availability, timing_basis: { unit: 'input_samples', global: true },
    sample_rate: { availability: unavailable }, observed_pulses: observedPulses,
    detected_count: availability.state === 'available' ? observedPulses.length : null,
    rejected_count: availability.state === 'available' ? 0 : null,
    count_availability: availability,
    count_semantics: {
      detected: availability.state === 'available'
        ? 'sum_of_distinct_acquisition_detector_counts' : 'unavailable',
      rejected: availability.state === 'available'
        ? 'sum_of_distinct_acquisition_detector_counts' : 'unavailable',
      deduplication_rule: 'terminal sink counts supersede upstream detector counts; source kinds are never added together',
    },
    rejection_reason_codes: [], preamble: { availability: unavailable },
    receiver_derived_active_frequencies: { availability: unavailable, indices: [] },
    assembler: { availability: unavailable },
    receiver_message_result: { availability: unavailable },
    terminal_result: { availability: unavailable }, sources: [], provenance: [],
    truncation: {
      truncated: false, original_pulse_count: observedPulses.length,
      returned_pulse_count: observedPulses.length, max_pulses: 512,
      max_response_bytes: 1_048_576,
    },
    observation_sha256: '1'.repeat(64),
  });
  const observation = parsers.observation(observationDocument(available, [
    pulse(0, 0), pulse(1, 0), pulse(2, 63),
  ]));

  it('defaults collapsed, expands exactly 64 keyboard-selectable cells, and reports text counts', () => {
    const select = vi.fn(); const expand = vi.fn();
    const { rerender } = render(<DetectorBankView presentation={presentation} expanded={false}
      onExpanded={expand} selection={null} onSelection={select} observation={observation} />);
    fireEvent.click(screen.getByRole('button', { name: 'Expand 8×8 detector bank' }));
    expect(expand).toHaveBeenCalledWith(true);
    rerender(<DetectorBankView presentation={presentation} expanded onExpanded={expand}
      selection={null} onSelection={select} observation={observation} />);
    const grid = screen.getByRole('grid', { name: '64 acquisition detectors in physical-channel order' });
    const cells = within(grid).getAllByRole('gridcell');
    expect(cells).toHaveLength(64);
    expect(cells[0].textContent).toContain('2 observed pulses');
    expect(cells[1].textContent).toContain('0 observed pulses');
    expect(cells[63].textContent).toContain('1 observed pulses');
    fireEvent.keyDown(cells[0], { key: 'ArrowDown' });
    expect(select).toHaveBeenLastCalledWith({ kind: 'node', id: 'renamed-member-08' });
    fireEvent.click(cells[63]);
    expect(select).toHaveBeenLastCalledWith({ kind: 'node', id: 'renamed-member-63' });
  });

  it('distinguishes schema-valid unavailable observations from available zero activity', () => {
    const select = vi.fn();
    const unavailableObservation = parsers.observation(observationDocument(unavailable, []));
    const { rerender } = render(<DetectorBankView presentation={presentation} expanded onExpanded={vi.fn()}
      selection={{ kind: 'node', id: 'renamed-member-63' }} onSelection={select}
      observation={unavailableObservation} />);
    expect(screen.getAllByText('Observation unavailable')).toHaveLength(64);
    rerender(<DetectorBankView presentation={presentation} expanded onExpanded={vi.fn()}
      selection={{ kind: 'node', id: 'renamed-member-63' }} onSelection={select}
      observation={{ ...observation, observed_pulses: [] }} />);
    expect(screen.getAllByText('0 observed pulses')).toHaveLength(64);
  });

  it('exposes exact selected-detector details when receiver observation is unavailable', () => {
    const select = vi.fn();
    render(<><DetectorBankView presentation={presentation} expanded onExpanded={vi.fn()}
      selection={{ kind: 'node', id: 'renamed-member-63' }} onSelection={select}
      observation={parsers.observation(observationDocument(unavailable, []))} />
    <DetectorInspector presentation={presentation} selection={{ kind: 'node', id: 'renamed-member-63' }}
      observation={parsers.observation(observationDocument(unavailable, []))}
      onSelection={select} /></>);
    expect(screen.getAllByText('Observation unavailable')).toHaveLength(64);
    expect(screen.getByText('Selected detector')).toBeTruthy();
    expect(screen.getByText('Physical channel').nextElementSibling?.textContent).toBe('63');
    expect(screen.getByText('Pulse-merge successor').nextElementSibling?.textContent).toBe('fanin, input port 64');
    expect(screen.getByText('Receiver observation').nextElementSibling?.textContent).toMatch(/Unavailable/);
  });

  it('traces a grouped boundary display edge to all 64 authoritative exact-port edges', () => {
    render(<DetectorInspector presentation={presentation}
      selection={{ kind: 'edge', id: 'bundle:fanout->fhss-acquisition-detector-bank:inputs' }}
      onSelection={vi.fn()} />);
    expect(screen.getByText('Grouped boundary inspector')).toBeTruthy();
    expect(screen.getByText(/summarizes 64 authoritative exact-port edges/)).toBeTruthy();
    expect(screen.getByText(/has no canonical numeric ports/)).toBeTruthy();
    expect(screen.getByText('Authoritative exact-port mappings (64)')).toBeTruthy();
    expect(screen.getAllByText('graph edge id')).toHaveLength(64);
  });

  it('cross-selects a detector from the evaluator-side heatmap without changing receiver input', () => {
    let selection: { kind: 'node'; id: string } | null = null;
    const select = vi.fn((channel: number) =>
      selectPhysicalChannel(presentation, channel, (next) => { selection = next as { kind: 'node'; id: string }; }));
    const { rerender } = render(<Visualization value={{
      fixture_label: 'test', schedule: {}, timeline: {},
      heatmap: { channels: [{ channel_index: 17, expected_pulse_count: 3 }] },
    }} selectedChannel={17} onChannel={select} />);
    const channel = screen.getByRole('button', { name: /Ch 17.*3 expected pulses/ });
    expect(channel.getAttribute('aria-pressed')).toBe('true');
    fireEvent.click(channel);
    expect(select).toHaveBeenCalledWith(17);
    expect(selection).toEqual({ kind: 'node', id: 'renamed-member-17' });
    expect(screen.getByText(/presentation focus only/)).toBeTruthy();
    rerender(<><DetectorBankView presentation={presentation} expanded onExpanded={vi.fn()}
      selection={selection} onSelection={vi.fn()} observation={observation} />
    <DetectorInspector presentation={presentation} selection={selection}
      onSelection={vi.fn()} observation={observation} /></>);
    expect(screen.getByRole('gridcell', { name: /Ch 17/ }).getAttribute('aria-selected')).toBe('true');
    expect(screen.getByText('Physical channel').nextElementSibling?.textContent).toBe('17');
  });

  it('uses coherent 8×8 ARIA ownership and physical-channel keyboard movement', () => {
    const select = vi.fn();
    render(<DetectorBankView presentation={presentation} expanded onExpanded={vi.fn()}
      selection={null} onSelection={select} observation={observation} />);
    expect(screen.getAllByRole('row')).toHaveLength(8);
    const cell = screen.getByRole('gridcell', { name: /Ch 17/ });
    expect(cell.getAttribute('aria-rowindex')).toBe('3');
    expect(cell.getAttribute('aria-colindex')).toBe('2');
    fireEvent.keyDown(cell, { key: 'ArrowDown' });
    expect(select).toHaveBeenCalledWith({ kind: 'node', id: 'renamed-member-25' });
  });

  it('deterministically identifies a selection removed by a topology refresh', () => {
    const selection = { kind: 'node' as const, id: 'renamed-member-17' };
    expect(selectionIsPresent(selection, presentation.authoritative, presentation)).toBe(true);
    const changed = copyGraph();
    changed.nodes = changed.nodes.filter((node) => node.id !== selection.id);
    changed.edges = changed.edges.filter((edge) =>
      edge.source_node_id !== selection.id && edge.target_node_id !== selection.id);
    const changedModel = toTopology(changed);
    const changedPresentation = toFHSSPresentation(changedModel);
    expect(selectionIsPresent(selection, changedModel, changedPresentation)).toBe(false);
    expect(SELECTION_CLEARED_MESSAGE).toMatch(/Selection cleared.*not present/);
  });

  it('retains the complete authoritative graph resource wrapper for raw diagnostics', () => {
    const graph = independentGraph();
    const resource = {
      schema: 'graphx.dashboard.graph.v1', owner: 'receiver', config_revision: 19, etag: '"graphx-config-19"', graph,
    };
    expect(authoritativeGraphDocument({
      kind: 'ready', graph, revision: 19, resource,
    }, graph)).toBe(resource);
    expect(authoritativeGraphDocument({
      kind: 'stale', graph, revision: 19, resource, message: 'offline',
    }, graph)).toBe(resource);
  });
});

describe('Phase 2 malformed graph handling', () => {
  it('rejects duplicate exact-port edges at the graph contract boundary', () => {
    const graph = independentGraph();
    graph.edges.push({ ...graph.edges[0] });
    expect(() => parseGraphResource({
      schema: 'graphx.dashboard.graph.v1', owner: 'receiver', config_revision: 0, etag: '"graphx-config-0"', graph,
    })).toThrow(/duplicate port-aware edge identities/);
  });
});
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
