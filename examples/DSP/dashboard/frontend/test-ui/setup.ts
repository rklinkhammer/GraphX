import { cleanup } from '@testing-library/react';
import { afterEach, vi } from 'vitest';

class ResizeObserverStub {
  observe() {}
  unobserve() {}
  disconnect() {}
}
vi.stubGlobal('ResizeObserver', ResizeObserverStub);
vi.stubGlobal('matchMedia', () => ({ matches: false, addEventListener() {}, removeEventListener() {} }));
Object.defineProperty(HTMLElement.prototype, 'offsetWidth', { configurable: true, value: 1024 });
Object.defineProperty(HTMLElement.prototype, 'offsetHeight', { configurable: true, value: 768 });
afterEach(() => cleanup());
