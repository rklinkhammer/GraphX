import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { GraphView, toFlowElements } from '../src/GraphView';
import { Inspector } from '../src/Inspector';
import { TopologyTable } from '../src/TopologyTable';
import { layoutTopology } from '../src/layout';
import { DETECTOR_BANK_ID, toFHSSPresentation } from '../src/fhssPresentation';
import { toTopology } from '../src/topology';

const graph = JSON.parse(readFileSync(resolve(process.cwd(), '../../../../libdsp/config/fhss_phase2_binary_iq_receiver.json'), 'utf8'));
const model = toTopology(graph);

describe('Phase 1 topology presentation', () => {
  it('adapts every stable node and exact-port edge to immutable React Flow elements', () => {
    const flow = toFlowElements(model);
    expect(flow.nodes).toHaveLength(75); expect(flow.edges).toHaveLength(137);
    expect(flow.nodes.every((node) => node.connectable === false && node.deletable === false)).toBe(true);
    expect(flow.edges.every((edge) => edge.reconnectable === false && edge.deletable === false)).toBe(true);
    expect(flow.edges.find((edge) => edge.id === 'detector_63:0->merge:64')).toMatchObject({ sourceHandle: 'out-0', targetHandle: 'in-64' });
  });

  it('runs real ELK FIXED_ORDER layout deterministically', async () => {
    const first = await layoutTopology(model); const second = await layoutTopology(model);
    expect(first.size).toBe(75); expect([...first]).toEqual([...second]);
    expect(first.get('source')).toBeDefined(); expect(first.get('merge')).toBeDefined();
  });

  it('renders the expanded detector bank as a parent with 64 children and exact edges', () => {
    const presentation = toFHSSPresentation(model);
    const flow = toFlowElements(presentation.expanded);
    const parentIndex = flow.nodes.findIndex(({ id }) => id === DETECTOR_BANK_ID);
    const children = flow.nodes.filter(({ parentId }) => parentId === DETECTOR_BANK_ID);
    expect(parentIndex).toBeGreaterThanOrEqual(0);
    expect(children).toHaveLength(64);
    expect(children.every((node) => flow.nodes.indexOf(node) > parentIndex)).toBe(true);
    expect(flow.edges).toHaveLength(137);
    expect(flow.edges.find((edge) => edge.id === 'channelizer:63->detector_63:0'))
      .toMatchObject({ sourceHandle: 'out-63', targetHandle: 'in-0' });
    expect(flow.edges.find((edge) => edge.id === 'detector_63:0->merge:64'))
      .toMatchObject({ sourceHandle: 'out-0', targetHandle: 'in-64' });
  });

  it('lays out the hierarchical parent and all detector children deterministically', async () => {
    const presentation = toFHSSPresentation(model);
    const first = await layoutTopology(presentation.expanded);
    const second = await layoutTopology(presentation.expanded);
    expect(first.size).toBe(76);
    expect([...first]).toEqual([...second]);
    expect(first.get(DETECTOR_BANK_ID)).toBeDefined();
    expect(first.get('detector_0')).toEqual({ x: 45, y: 90 });
    expect(first.get('detector_63')).toEqual({ x: 1235, y: 776 });
  });

  it('uses presentation-only bundles without fabricated canonical ports when collapsed', () => {
    const presentation = toFHSSPresentation(model);
    const bundles = presentation.collapsed.edges.filter((edge) => 'kind' in edge);
    expect(bundles).toHaveLength(2);
    expect(bundles.every((edge) =>
      !('source_port' in edge) && !('target_port' in edge))).toBe(true);
    const flow = toFlowElements(presentation.collapsed);
    expect(flow.edges.filter((edge) => edge.className === 'presentation-bundle-edge')).toHaveLength(2);
  });

  it('renders graph controls and read-only presentation notice', () => {
    const selected = vi.fn();
    render(<GraphView model={model} selection={null} onSelection={selected} />);
    expect(screen.getByRole('heading', { name: 'Read-only GraphX topology' })).toBeTruthy();
    expect(screen.getByText(/Connection, reconnection, and deletion are disabled/)).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Reset layout' })).toBeTruthy();
    const sourceNode = document.querySelector('[data-id="source"]');
    expect(sourceNode).toBeTruthy(); fireEvent.click(sourceNode!);
    expect(selected).toHaveBeenCalledWith({ kind: 'node', id: 'source' });
  });

  it('keeps semantic nodes/edges equivalent, keyboard-selectable, and synchronized with inspector', () => {
    const selected = vi.fn();
    const { rerender } = render(<><TopologyTable model={model} selection={null} onSelection={selected} /><Inspector model={model} selection={null} /></>);
    expect(screen.getByText('Nodes (75)')).toBeTruthy(); expect(screen.getByText('Edges (137)')).toBeTruthy();
    const source = within(screen.getByRole('listbox', { name: 'GraphX nodes' })).getByRole('option', { name: /source/ }); fireEvent.click(source);
    expect(selected).toHaveBeenLastCalledWith({ kind: 'node', id: 'source' });
    fireEvent.keyDown(source, { key: 'ArrowDown' }); expect(selected).toHaveBeenLastCalledWith({ kind: 'node', id: 'downconverter' });
    rerender(<><TopologyTable model={model} selection={{ kind: 'node', id: 'source' }} onSelection={selected} /><Inspector model={model} selection={{ kind: 'node', id: 'source' }} /></>);
    expect(screen.getByText('Stable configuration identity').nextElementSibling?.textContent).toBe('source');
    expect(document.activeElement?.textContent).toContain('source');
    const edge = within(screen.getByRole('listbox', { name: 'GraphX exact-port edges' })).getByRole('option', { name: /source:0->downconverter:0/ });
    fireEvent.click(edge); expect(selected).toHaveBeenLastCalledWith({ kind: 'edge', id: 'source:0->downconverter:0' });
    rerender(<><TopologyTable model={model} selection={{ kind: 'edge', id: 'source:0->downconverter:0' }} onSelection={selected} /><Inspector model={model} selection={{ kind: 'edge', id: 'source:0->downconverter:0' }} /></>);
    expect(screen.getByText('Stable port-aware identity').nextElementSibling?.textContent).toContain('source:0');
  });
});
