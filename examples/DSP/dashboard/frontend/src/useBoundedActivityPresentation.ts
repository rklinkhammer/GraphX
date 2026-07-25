import { useEffect, useRef, useState } from 'react';
import {
  BoundedActivityBuffer, type ActivityFrame,
} from './activity';

export interface BoundedActivityPresentation {
  frame: ActivityFrame | undefined;
  coalescedUpdates: number;
  queuedUpdates: number;
}

export function useBoundedActivityPresentation(
  input: ActivityFrame | undefined,
): BoundedActivityPresentation {
  const buffer = useRef(new BoundedActivityBuffer());
  const timer = useRef<number | undefined>(undefined);
  const [value, setValue] = useState<BoundedActivityPresentation>({
    frame: undefined, coalescedUpdates: 0, queuedUpdates: 0,
  });

  useEffect(() => {
    const clearTimer = () => {
      if (timer.current !== undefined) window.clearTimeout(timer.current);
      timer.current = undefined;
    };
    const publishState = () => setValue({
      frame: buffer.current.latest(),
      coalescedUpdates: buffer.current.coalescedUpdates(),
      queuedUpdates: buffer.current.queuedSize(),
    });
    const promote = () => {
      timer.current = undefined;
      if (buffer.current.promote(performance.now())) publishState();
      else if (buffer.current.queuedSize()) {
        timer.current = window.setTimeout(promote,
          buffer.current.delayUntilPromotion(performance.now()));
      }
    };

    clearTimer();
    if (!input) {
      buffer.current.reset();
      setValue({ frame: undefined, coalescedUpdates: 0, queuedUpdates: 0 });
      return clearTimer;
    }
    if (buffer.current.offer(input, performance.now())) publishState();
    else {
      publishState();
      timer.current = window.setTimeout(promote,
        buffer.current.delayUntilPromotion(performance.now()));
    }
    return clearTimer;
  }, [input]);

  useEffect(() => () => {
    if (timer.current !== undefined) window.clearTimeout(timer.current);
  }, []);
  return value;
}
