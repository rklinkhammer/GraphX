import type {
  DisplayTopologyModel, PresentationBundleEdge, TopologyEdge, TopologyModel, TopologyNode,
} from './topology';

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
  collapsed: DisplayTopologyModel;
  expanded: DisplayTopologyModel;
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
  return {
    authoritative,
    collapsed: authoritative,
    expanded: authoritative,
    diagnostics: [`Detector bank not collapsed: ${reason}`],
  };
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

function groupNode(group: DetectorBankGroup, expanded: boolean): TopologyNode {
  return {
    id: group.id,
    type: 'FHSSPresentationGroup',
    presentationRole: 'group',
    configuration: {
      presentation_only: true,
      expanded,
      label: group.label,
      member_count: group.members.length,
      authoritative_node_ids: group.authoritativeNodeIds,
      channelizer_output_ports: group.members.map((entry) => entry.channelizerOutputPort),
      merge_input_ports: group.members.map((entry) => entry.mergeInputPort),
      boundary_summary:
        `${group.channelizerNodeId} outputs 0–63 → detectors → ${group.mergeNodeId} inputs 1–64`,
    },
    inputPorts: [],
    outputPorts: [],
  };
}

function boundaryBundle(
  group: DetectorBankGroup,
  direction: 'incoming' | 'outgoing',
): PresentationBundleEdge {
  const incoming = direction === 'incoming';
  const mappings = group.members.map((entry) => {
    const edge = incoming ? entry.incomingEdge : entry.outgoingEdge;
    return {
      graph_edge_id: edge.id,
      source_node_id: edge.source_node_id,
      source_port: edge.source_port,
      target_node_id: edge.target_node_id,
      target_port: edge.target_port,
    };
  });
  return {
    kind: 'presentation-bundle',
    presentation_only: true,
    id: incoming
      ? `bundle:${group.channelizerNodeId}->${group.id}:inputs`
      : `bundle:${group.id}->${group.mergeNodeId}:outputs`,
    source_node_id: incoming ? group.channelizerNodeId : group.id,
    target_node_id: incoming ? group.id : group.mergeNodeId,
    label: incoming
      ? '64 exact channelizer-to-detector edges (outputs 0–63)'
      : '64 exact detector-to-merge edges (inputs 1–64)',
    authoritativeEdgeIds: mappings.map(({ graph_edge_id }) => graph_edge_id),
    mappings,
  };
}

function collapsedModel(model: TopologyModel, group: DetectorBankGroup): DisplayTopologyModel {
  const memberIds = new Set(group.authoritativeNodeIds);
  const boundaryEdgeIds = new Set(group.authoritativeEdgeIds);
  const firstMemberIndex = model.nodes.findIndex((node) => memberIds.has(node.id));
  const nodes = model.nodes.filter((node) => !memberIds.has(node.id));
  nodes.splice(Math.max(0, firstMemberIndex), 0, groupNode(group, false));
  return {
    nodes,
    edges: [
      ...model.edges.filter((edge) => !boundaryEdgeIds.has(edge.id)),
      boundaryBundle(group, 'incoming'),
      boundaryBundle(group, 'outgoing'),
    ],
  };
}

function expandedModel(model: TopologyModel, group: DetectorBankGroup): DisplayTopologyModel {
  const memberIds = new Set(group.authoritativeNodeIds);
  const parent = groupNode(group, true);
  const members = group.members.map(({ node }) => ({
    ...node,
    parentId: group.id,
    presentationRole: 'group-member' as const,
  }));
  const firstMemberIndex = model.nodes.findIndex((node) => memberIds.has(node.id));
  const nodes = model.nodes.filter((node) => !memberIds.has(node.id));
  nodes.splice(Math.max(0, firstMemberIndex), 0, parent, ...members);
  return { nodes, edges: model.edges };
}

export function toFHSSPresentation(model: TopologyModel): FHSSPresentation {
  const recognition = recognizeDetectorBank(model);
  if (!recognition.group) return reject(model, recognition.diagnostics.join(' '));
  return {
    authoritative: model,
    collapsed: collapsedModel(model, recognition.group),
    expanded: expandedModel(model, recognition.group),
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
