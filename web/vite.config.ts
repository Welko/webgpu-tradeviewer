import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  // Relative asset URLs, so the build works under GitHub Pages' /<repo>/ path.
  base: './',
  build: {
    // Published to GitHub Pages by .github/workflows/ci.yml.
    outDir: '../docs',
    emptyOutDir: true,
    rollupOptions: {
      // The viewer, and the image-test scenes (see src/scene.ts).
      input: { main: 'index.html', scene: 'scene.html' },
    },
  },
});
