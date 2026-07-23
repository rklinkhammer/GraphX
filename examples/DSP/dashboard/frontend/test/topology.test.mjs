import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { parseGraphResource } from '../test-dist/domain.js';
import {
  detectorMergeOffsets, edgeIdentity, isTopologyMutationAllowed,
  nextSelectionIndex, normalizeLayout, toTopology,
} from '../test-dist/topology.js';

const graphPath = new URL('../../../../../libdsp/config/fhss_phase2_binary_iq_receiver.json', import.meta.url);
const fixture = JSON.parse(await readFile(graphPath, 'utf8'));

test('canonical receiver graph has stable identities and all exact ports', () => {
  const resource = parseGraphResource({ schema: 'graphx.dashboard.graph.v1', owner: 'receiver', config_revision: 1, graph: fixture });
  const topology = toTopology(resource.graph);
  assert.equal(topology.nodes.length, 75);
  assert.equal(topology.edges.length, 137);
  assert.equal(new Set(topology.nodes.map(({ id }) => id)).size, 75);
  assert.equal(new Set(topology.edges.map(({ id }) => id)).size, 137);
  assert.equal(detectorMergeOffsets(topology), true);
  assert.ok(topology.edges.some((edge) => edge.id === 'detector_0:0->merge:1'
    && edge.sourceHandle === 'out-0' && edge.targetHandle === 'in-1'));
  assert.ok(topology.edges.some((edge) => edge.id === 'detector_63:0->merge:64'
    && edge.sourceHandle === 'out-0' && edge.targetHandle === 'in-64'));
});

test('edge identity is stable and port-aware', () => {
  assert.equal(edgeIdentity({ source_node_id: 'a', source_port: 2, target_node_id: 'b', target_port: 7 }), 'a:2->b:7');
});

test('layout normalization is deterministic across order and translation', () => {
  const first = normalizeLayout([{ id: 'b', x: 15.00001, y: 27 }, { id: 'a', x: 10, y: 20 }]);
  const second = normalizeLayout([{ id: 'a', x: 110, y: 120 }, { id: 'b', x: 115.00001, y: 127 }]);
  assert.deepEqual(first, second);
});

test('topology mutations are disabled and keyboard selection wraps', () => {
  assert.equal(isTopologyMutationAllowed(), false);
  assert.equal(nextSelectionIndex(0, 75, 'ArrowUp'), 74);
  assert.equal(nextSelectionIndex(74, 75, 'ArrowDown'), 0);
  assert.equal(nextSelectionIndex(42, 75, 'Home'), 0);
  assert.equal(nextSelectionIndex(0, 75, 'End'), 74);
});

test('malformed graph resources fail without position-derived identity', () => {
  const resource = (graph) => ({ schema: 'graphx.dashboard.graph.v1', owner: 'receiver', config_revision: 1, graph });
  assert.throws(() => parseGraphResource(resource({ nodes: [{}], edges: [] })), /stable id and type/);
  assert.throws(() => parseGraphResource(resource({ nodes: [{ id: 'a', type: 'T' }, { id: 'a', type: 'T' }], edges: [] })), /duplicate/);
  assert.throws(() => parseGraphResource(resource({ nodes: [{ id: 'a', type: 'T' }], edges: [{ source_node_id: 'a', source_port: 0, target_node_id: 'missing', target_port: 0 }] })), /unknown node/);
});
