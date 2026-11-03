import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';

/**
 * Makes benchmark.html cross-origin isolated when served by vite preview,
 * as CI and the benchmark script do, so performance.now() and the viewer's
 * own timing count in microseconds: Chromium rounds them to 0.1 ms otherwise.
 * GitHub Pages can't send these headers, so there it times coarsely.
 */
const isolateBenchmark: Plugin = {
  name: 'isolate-benchmark',
  configurePreviewServer(server) {
    server.middlewares.use((request, response, next) => {
      // Typed without Node's types, which the site's tsconfig leaves out.
      if ((request as { url?: string }).url?.startsWith('/benchmark.html')) {
        response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
        response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      }
      next();
    });
  },
};

export default defineConfig({
  plugins: [react(), isolateBenchmark],
  // Relative asset URLs, so the build works under GitHub Pages' /<repo>/ path.
  base: './',
  build: {
    // Published to GitHub Pages by .github/workflows/ci.yml.
    outDir: '../docs',
    emptyOutDir: true,
    rollupOptions: {
      // The viewer, the image-test scenes (see src/scene.ts) and the benchmark (src/benchmark.ts).
      input: { main: 'index.html', scene: 'scene.html', benchmark: 'benchmark.html' },
    },
  },
});
