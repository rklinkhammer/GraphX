import "@testing-library/dom";

class TestResizeObserver implements ResizeObserver {
  observe(): void {}
  unobserve(): void {}
  disconnect(): void {}
}

Object.defineProperty(globalThis, "ResizeObserver", {
  configurable: true,
  value: TestResizeObserver,
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
