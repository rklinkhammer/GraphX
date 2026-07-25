import { render, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import type { EdgeActivity } from '../src/activity';
import type { DisplayTopologyModel } from '../src/topology';

const mocks = vi.hoisted(() => ({
  layout: vi.fn(async () => new Map([['source', { x: 0, y: 0 }], ['sink', { x: 300, y: 0 }]])),
}));
vi.mock('../src/layout', () => ({ layoutTopology: mocks.layout }));

import { GraphView } from '../src/GraphView';

const model: DisplayTopologyModel = {
  nodes: [
    { id: 'source', type: 'Source', configuration: {}, inputPorts: [], outputPorts: [63] },
    { id: 'sink', type: 'Sink', configuration: {}, inputPorts: [64], outputPorts: [] },
  ],
  edges: [{
    id: 'source:63->sink:64', source_node_id: 'source', source_port: 63,
    target_node_id: 'sink', target_port: 64, sourceHandle: 'out-63', targetHandle: 'in-64',
  }],
};

const available: EdgeActivity = {
  edgeId: 'source:63->sink:64', availability: 'available', unavailableReason: null,
  messageClass: 'channel-iq', messages: 4, messagesPerSecond: 20, intervalMs: 200,
  memberCount: 1, availableMembers: 1,
};

describe('activity canvas isolation', () => {
  it('does not rerun ELK or recreate topology for a metric-only update', async () => {
    mocks.layout.mockClear();
    const first = new Map([['source:63->sink:64', available]]);
    const view = render(<GraphView model={model} selection={null} onSelection={() => undefined}
      activity={first} animatedEdges={new Set(['source:63->sink:64'])} />);
    await waitFor(() => expect(mocks.layout).toHaveBeenCalledTimes(1));
    const nodes = document.querySelectorAll('.react-flow__node').length;
    view.rerender(<GraphView model={model} selection={null} onSelection={() => undefined}
      activity={new Map([['source:63->sink:64', { ...available, messages: 9 }]])}
      animatedEdges={new Set(['source:63->sink:64'])} />);
    await Promise.resolve();
    expect(mocks.layout).toHaveBeenCalledTimes(1);
    expect(document.querySelectorAll('.react-flow__node')).toHaveLength(nodes);
  });
});
