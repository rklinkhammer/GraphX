import type { TopologyEdge, TopologyModel, TopologyNode } from './topology';

export const DETECTOR_TYPE = 'FHSSAcquisitionPulseDetectorNode';
export const DETECTOR_BANK_ID = 'fhss-acquisition-detector-bank';
const CHANNELIZER_TYPE = 'FHSSProductionCandidateChannelizerNode';
const MERGE_TYPE = 'FHSSPulseMergeNode';
const CHANNEL_COUNT = 64;

export interface DisplayPort {
  graphPort: number;
  handle: string;
}

export interface DetectorMapping {
  node: TopologyNode;
  logicalChannel: number;
  physicalChannel: number;
  channelizerNodeId: string;
  channelizerOutputPort: number;
  detectorInputPort: number;
  detectorOutputPort: number;
  mergeNodeId: string;
  mergeInputPort: number;
  incomingEdge: TopologyEdge;
  outgoingEdge: TopologyEdge;
}

export interface DetectorBankGroup {
  id: typeof DETECTOR_BANK_ID;
  label: string;
  members: DetectorMapping[];
  channelizerNodeId: string;
  mergeNodeId: string;
  authoritativeNodeIds: string[];
  authoritativeEdgeIds: string[];
  inputPorts: DisplayPort[];
  outputPorts: DisplayPort[];
}

export interface FHSSPresentation {
  authoritative: TopologyModel;
  collapsed: TopologyModel;
  detectorBank?: DetectorBankGroup;
  diagnostics: string[];
}

const integer = (value: unknown): value is number =>
  Number.isSafeInteger(value) && Number(value) >= 0;

function canonicalDetectorConfiguration(configuration: Record<string, unknown>): string {
  const comparable = Object.entries(configuration)
    .filter(([name]) => name !== 'detector_id')
    .sort(([left], [right]) => left.localeCompare(right));
  return JSON.stringify(comparable);
}

function completeChannelSet(values: number[], first: number): boolean {
  return values.length === CHANNEL_COUNT
    && new Set(values).size === CHANNEL_COUNT
    && values.slice().sort((left, right) => left - right)
      .every((value, index) => value === index + first);
}

function reject(authoritative: TopologyModel, reason: string): FHSSPresentation {
  return { authoritative, collapsed: authoritative, diagnostics: [`Detector bank not collapsed: ${reason}`] };
}

export function recognizeDetectorBank(model: TopologyModel): {
  group?: DetectorBankGroup;
  diagnostics: string[];
} {
  const detectors = model.nodes.filter((node) => node.type === DETECTOR_TYPE);
  if (detectors.length === 0) return { diagnostics: ['No FHSS acquisition detector bank is present.'] };
  if (detectors.length !== CHANNEL_COUNT) {
    return { diagnostics: [`Expected ${CHANNEL_COUNT} acquisition detectors; found ${detectors.length}.`] };
  }

  const configurations = new Set(detectors.map((node) => canonicalDetectorConfiguration(node.configuration)));
  if (configurations.size !== 1) {
    return { diagnostics: ['Acquisition detector configurations are not equivalent except for detector_id.'] };
  }

  const mappings: DetectorMapping[] = [];
  for (const detector of detectors) {
    const logicalChannel = detector.configuration.detector_id;
    if (!integer(logicalChannel) || logicalChannel >= CHANNEL_COUNT) {
      return { diagnostics: [`Detector ${detector.id} has an invalid detector_id.`] };
    }
    const incoming = model.edges.filter((edge) => edge.target_node_id === detector.id);
    const outgoing = model.edges.filter((edge) => edge.source_node_id === detector.id);
    if (incoming.length !== 1 || outgoing.length !== 1) {
      return { diagnostics: [`Detector ${detector.id} must have exactly one predecessor and one successor edge.`] };
    }
    const incomingEdge = incoming[0];
    const outgoingEdge = outgoing[0];
    if (!incomingEdge || !outgoingEdge) {
      return { diagnostics: [`Detector ${detector.id} boundary edges are unavailable.`] };
    }
    const channelizer = model.nodes.find((node) => node.id === incomingEdge.source_node_id);
    const merge = model.nodes.find((node) => node.id === outgoingEdge.target_node_id);
    if (channelizer?.type !== CHANNELIZER_TYPE || merge?.type !== MERGE_TYPE) {
      return { diagnostics: [`Detector ${detector.id} does not connect the required channelizer and pulse merge types.`] };
    }
    if (incomingEdge.target_port !== 0 || outgoingEdge.source_port !== 0) {
      return { diagnostics: [`Detector ${detector.id} does not use exact detector ports input 0 and output 0.`] };
    }
    const channelIds = channelizer.configuration.channel_ids;
    const physicalIndices = channelizer.configuration.receiver_frequency_indices;
    if (!Array.isArray(channelIds) || !Array.isArray(physicalIndices)
      || channelIds.length !== CHANNEL_COUNT || physicalIndices.length !== CHANNEL_COUNT) {
      return { diagnostics: [`Channelizer ${channelizer.id} lacks complete logical and physical channel identity.`] };
    }
    const sourcePort = incomingEdge.source_port;
    const channelAtPort = channelIds[sourcePort];
    const physicalChannel = physicalIndices[sourcePort];
    if (!integer(channelAtPort) || channelAtPort !== logicalChannel
      || !integer(physicalChannel) || physicalChannel >= CHANNEL_COUNT) {
      return { diagnostics: [`Detector ${detector.id} disagrees with channelizer port ${sourcePort} channel identity.`] };
    }
    if (outgoingEdge.target_port !== physicalChannel + 1) {
      return { diagnostics: [`Detector ${detector.id} physical channel ${physicalChannel} must map to merge input ${physicalChannel + 1}.`] };
    }
    mappings.push({
      node: detector, logicalChannel, physicalChannel,
      channelizerNodeId: channelizer.id, channelizerOutputPort: sourcePort,
      detectorInputPort: incomingEdge.target_port, detectorOutputPort: outgoingEdge.source_port,
      mergeNodeId: merge.id, mergeInputPort: outgoingEdge.target_port,
      incomingEdge, outgoingEdge,
    });
  }

  const channelizers = new Set(mappings.map((entry) => entry.channelizerNodeId));
  const merges = new Set(mappings.map((entry) => entry.mergeNodeId));
  if (channelizers.size !== 1 || merges.size !== 1) {
    return { diagnostics: ['Acquisition detectors do not share exactly one channelizer predecessor and one pulse merge successor.'] };
  }
  const channelizerNodeId = firstValue(channelizers);
  const mergeNodeId = firstValue(merges);
  const bankInputEdgeIds = new Set(mappings.map((entry) => entry.incomingEdge.id));
  const bankOutputEdgeIds = new Set(mappings.map((entry) => entry.outgoingEdge.id));
  if (!channelizerNodeId || !mergeNodeId
    || model.edges.filter((edge) => edge.source_node_id === channelizerNodeId)
      .some((edge) => !bankInputEdgeIds.has(edge.id))
    || model.edges.filter((edge) => edge.target_node_id === mergeNodeId)
      .some((edge) => !bankOutputEdgeIds.has(edge.id))) {
    return { diagnostics: ['Detector bank channelizer outputs or merge inputs are ambiguous.'] };
  }
  if (!completeChannelSet(mappings.map((entry) => entry.logicalChannel), 0)
    || !completeChannelSet(mappings.map((entry) => entry.physicalChannel), 0)
    || !completeChannelSet(mappings.map((entry) => entry.channelizerOutputPort), 0)
    || !completeChannelSet(mappings.map((entry) => entry.mergeInputPort), 1)) {
    return { diagnostics: ['Detector bank channel, channelizer-port, or merge-port membership is incomplete or duplicated.'] };
  }

  mappings.sort((left, right) => left.physicalChannel - right.physicalChannel);
  const firstMapping = mappings[0];
  if (!firstMapping) return { diagnostics: ['Detector bank membership is unexpectedly empty.'] };
  const authoritativeEdgeIds = mappings.flatMap((entry) => [entry.incomingEdge.id, entry.outgoingEdge.id]);
  return {
    group: {
      id: DETECTOR_BANK_ID,
      label: 'Acquisition detector bank (64)',
      members: mappings,
      channelizerNodeId: firstMapping.channelizerNodeId,
      mergeNodeId: firstMapping.mergeNodeId,
      authoritativeNodeIds: mappings.map((entry) => entry.node.id),
      authoritativeEdgeIds,
      inputPorts: mappings.map((entry) => ({ graphPort: entry.channelizerOutputPort, handle: `out-${entry.channelizerOutputPort}` })),
      outputPorts: mappings.map((entry) => ({ graphPort: entry.mergeInputPort, handle: `in-${entry.mergeInputPort}` })),
    },
    diagnostics: [],
  };
}

function firstValue(values: Set<string>): string | undefined {
  return values.values().next().value;
}

function collapsedModel(model: TopologyModel, group: DetectorBankGroup): TopologyModel {
  const memberIds = new Set(group.authoritativeNodeIds);
  const boundaryEdgeIds = new Set(group.authoritativeEdgeIds);
  const groupNode: TopologyNode = {
    id: group.id,
    type: 'FHSSPresentationGroup',
    configuration: {
      presentation_only: true,
      label: group.label,
      member_count: group.members.length,
      authoritative_node_ids: group.authoritativeNodeIds,
      channelizer_output_ports: group.members.map((entry) => entry.channelizerOutputPort),
      merge_input_ports: group.members.map((entry) => entry.mergeInputPort),
    },
    inputPorts: [0],
    outputPorts: [0],
  };
  const first = group.members[0]!;
  const last = group.members.at(-1)!;
  const fanout: TopologyEdge = {
    id: `${group.channelizerNodeId}:0-63->${group.id}:0`,
    source_node_id: group.channelizerNodeId, source_port: 0,
    target_node_id: group.id, target_port: 0,
    sourceHandle: 'out-0', targetHandle: 'in-0',
  };
  const fanin: TopologyEdge = {
    id: `${group.id}:0->${group.mergeNodeId}:1-64`,
    source_node_id: group.id, source_port: 0,
    target_node_id: group.mergeNodeId, target_port: 1,
    sourceHandle: 'out-0', targetHandle: 'in-1',
  };
  groupNode.configuration.boundary_summary =
    `${first.channelizerNodeId} outputs 0–63 → detectors → ${last.mergeNodeId} inputs 1–64`;
  const firstMemberIndex = model.nodes.findIndex((node) => memberIds.has(node.id));
  const nodes = model.nodes.filter((node) => !memberIds.has(node.id));
  nodes.splice(Math.max(0, firstMemberIndex), 0, groupNode);
  return {
    nodes,
    edges: [...model.edges.filter((edge) => !boundaryEdgeIds.has(edge.id)), fanout, fanin],
  };
}

export function toFHSSPresentation(model: TopologyModel): FHSSPresentation {
  const recognition = recognizeDetectorBank(model);
  if (!recognition.group) return reject(model, recognition.diagnostics.join(' '));
  return {
    authoritative: model,
    collapsed: collapsedModel(model, recognition.group),
    detectorBank: recognition.group,
    diagnostics: [],
  };
}

export function detectorForPhysicalChannel(
  presentation: FHSSPresentation,
  channel: number,
): DetectorMapping | undefined {
  return presentation.detectorBank?.members.find((entry) => entry.physicalChannel === channel);
}
