import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../src/App';
import type { GraphContract } from '../src/domain';

const graph = JSON.parse(readFileSync(
  resolve(process.cwd(), '../../../../libdsp/config/fhss_phase2_binary_iq_receiver.json'),
  'utf8',
)) as GraphContract;

const visualization = {
  schema: 'graphx.dashboard.fhss_visualization.v1',
  fixture_label: 'Synthetic integration fixture', config_revision: 0,
  schedule: {}, timeline: {}, bounds: {},
  heatmap: {
    channels: Array.from({ length: 64 }, (_, channel) => ({
      channel_index: channel, expected_pulse_count: channel === 17 ? 3 : 0,
    })),
  },
};

const spectrum = (channel: number | null) => ({
  schema: 'graphx.dashboard.fhss_receiver_spectrum.v1',
  semantic_class: 'observed', generation: 0, run_epoch: 0, config_revision: 0,
  config_etag: '"graphx-config-0"', channel_index: channel,
  availability: { state: 'available', reason: null }, bins: [],
});

class WebSocketStub {
  static readonly OPEN = 1;
  readonly readyState = 0;
  addEventListener() {}
  send() {}
  close() {}
}

function json(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), {
    status, headers: { 'Content-Type': 'application/json' },
  });
}

describe('Phase 2 application channel synchronization', () => {
  beforeEach(() => {
    vi.stubGlobal('WebSocket', WebSocketStub);
    vi.stubGlobal('fetch', vi.fn(async (input: string | URL | Request) => {
      const path = String(input);
      if (path === '/api/v1/fhss/graph') {
        return json({
          schema: 'graphx.dashboard.graph.v1', owner: 'receiver',
          config_revision: 0, graph,
        });
      }
      if (path === '/api/v1/fhss/visualization') return json(visualization);
      if (path.startsWith('/api/v1/fhss/spectrum')) {
        const match = path.match(/[?&]channel=([0-9]+)/);
        return json(spectrum(match ? Number(match[1]) : null));
      }
      return json({ detail: 'not needed by this focused integration test' }, 503);
    }));
  });

  afterEach(() => vi.unstubAllGlobals());

  it('routes a heatmap selection through App to every detector representation and spectrum', async () => {
    render(<App />);
    await screen.findByRole('heading', { name: 'Acquisition detector bank' });
    await screen.findByRole('button', { name: /Ch 17.*3 expected pulses/ });

    fireEvent.click(screen.getByRole('button', { name: 'Expand 8×8 detector bank' }));
    fireEvent.click(screen.getByRole('button', { name: /Ch 17.*3 expected pulses/ }));

    const grid = screen.getByRole('grid', {
      name: '64 acquisition detectors in physical-channel order',
    });
    await waitFor(() => expect(
      within(grid).getByRole('gridcell', { name: /Ch 17/ }).getAttribute('aria-selected'),
    ).toBe('true'));

    expect(screen.getByRole('option', { name: /detector_17/ }).getAttribute('aria-selected')).toBe('true');
    const inspector = screen.getByRole('heading', { name: 'Selected detector' }).parentElement!;
    expect(within(inspector).getByText('Physical channel').nextElementSibling?.textContent).toBe('17');
    await waitFor(() => expect(
      (screen.getByLabelText('Receiver spectrum physical channel') as HTMLSelectElement).value,
    ).toBe('17'));
    expect(screen.getByRole('button', { name: /Ch 17.*3 expected pulses/ }).getAttribute('aria-pressed')).toBe('true');
    expect(screen.getAllByText(/Expected truth is evaluator-side/).length).toBeGreaterThan(0);
  });
});
