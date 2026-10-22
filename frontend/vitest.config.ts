import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

// Tailwind is not needed for behaviour tests: CSS is skipped so tests stay fast and deterministic.
export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'jsdom',
    setupFiles: './src/test-setup.ts',
    css: false
  }
});
