import "@testing-library/dom";

class TestResizeObserver implements ResizeObserver {
  observe(): void {}
  unobserve(): void {}
  disconnect(): void {}
}

class TestStorage implements Storage {
  #values = new Map<string, string>();
  get length(): number { return this.#values.size; }
  clear(): void { this.#values.clear(); }
  getItem(key: string): string | null { return this.#values.get(key) ?? null; }
  key(index: number): string | null { return [...this.#values.keys()][index] ?? null; }
  removeItem(key: string): void { this.#values.delete(key); }
  setItem(key: string, value: string): void { this.#values.set(key, String(value)); }
}

Object.defineProperty(globalThis, "ResizeObserver", {
  configurable: true,
  value: TestResizeObserver,
});

Object.defineProperty(globalThis, "localStorage", {
  configurable: true,
  value: new TestStorage(),
});

Object.defineProperty(HTMLElement.prototype, "getBoundingClientRect", {
  configurable: true,
  value() {
    return {
      x: 0,
      y: 0,
      width: 1000,
      height: 700,
      top: 0,
      right: 1000,
      bottom: 700,
      left: 0,
      toJSON: () => ({}),
    };
  },
});
