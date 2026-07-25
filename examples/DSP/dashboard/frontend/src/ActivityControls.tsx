import { useEffect, useState } from 'react';

export const ACTIVITY_SPEEDS = [0.5, 1, 2] as const;

export interface ActivityPreferences {
  paused: boolean;
  speed: number;
  reducedMotion: boolean;
  systemReducedMotion: boolean;
}

export function ActivityControls({
  value, onChange, suppressed, coalescedUpdates = 0, queuedUpdates = 0,
}: {
  value: ActivityPreferences;
  onChange: (next: ActivityPreferences) => void;
  suppressed: number;
  coalescedUpdates?: number;
  queuedUpdates?: number;
}) {
  return <section className="activity-controls" aria-labelledby="activity-controls-heading">
    <div><h2 id="activity-controls-heading">Aggregate activity presentation</h2>
      <p>Controls affect representative motion only. Runtime execution and semantic metrics continue.</p></div>
    <button type="button" aria-pressed={value.paused}
      onClick={() => onChange({ ...value, paused: !value.paused })}>
      {value.paused ? 'Resume motion' : 'Pause motion'}
    </button>
    <label>Presentation speed
      <select value={value.speed} onChange={(event) => onChange({ ...value, speed: Number(event.target.value) })}>
        {ACTIVITY_SPEEDS.map((speed) => <option key={speed} value={speed}>{speed}×</option>)}
      </select>
    </label>
    <label><input type="checkbox" className="inline-control" checked={value.reducedMotion}
      onChange={(event) => onChange({ ...value, reducedMotion: event.target.checked })} />
      Reduce motion explicitly
    </label>
    <p role="status">System reduced motion: {value.systemReducedMotion ? 'requested' : 'not requested'}.
      Motion: {value.paused || value.reducedMotion || value.systemReducedMotion ? 'disabled' : 'enabled'}.
      Overload suppression: {suppressed > 0 ? `${suppressed} active edges represented without motion` : 'not active'}.
      Presentation coalescing: {coalescedUpdates} superseded updates;
      {queuedUpdates} latest update queued.</p>
  </section>;
}

export function useActivityPreferences(): [ActivityPreferences, (next: ActivityPreferences) => void] {
  const [value, setValue] = useState<ActivityPreferences>({
    paused: false, speed: 1, reducedMotion: false, systemReducedMotion: false,
  });
  useEffect(() => {
    const query = window.matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => setValue((current) => ({ ...current, systemReducedMotion: query.matches }));
    update(); query.addEventListener('change', update);
    return () => query.removeEventListener('change', update);
  }, []);
  return [value, setValue];
}
