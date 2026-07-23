import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'jsdom',
    setupFiles: ['./test-ui/setup.ts'],
    include: ['test-ui/**/*.test.ts', 'test-ui/**/*.test.tsx'],
    restoreMocks: true,
  },
});
