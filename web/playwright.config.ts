import { defineConfig, type ReporterDescription } from '@playwright/test';

// Set in CI, as for the C++ tests (see cpp/cmake/testing.cmake).
const junitDir = process.env.TRADEVIEWER_JUNIT_DIR;
const junit: ReporterDescription[] = junitDir ? [['junit', { outputFile: `${junitDir}/e2e/Playwright.xml` }]] : [];

/**
 * Smoke tests of the built site (docs/, from `npm run build`) in Chromium.
 * WebGPU runs on SwiftShader, Chrome's software Vulkan, so no GPU is needed;
 * CI has none.
 */
export default defineConfig({
  testDir: './tests',
  timeout: 60_000,
  // One browser for all tests, warmed up once (see tests/smoke.spec.ts).
  workers: 1,
  forbidOnly: !!process.env.CI,
  reporter: [[process.env.CI ? 'github' : 'list'], ...junit],
  use: {
    baseURL: 'http://localhost:4173',
    // Full Chromium in its new headless mode; the default headless shell has no WebGPU.
    channel: 'chromium',
    launchOptions: {
      args: [
        '--enable-unsafe-webgpu',
        '--enable-features=Vulkan',
        // SwiftShader for ANGLE too, not the machine's Vulkan driver: CI has
        // none, and without one the canvas reads back blank.
        '--use-angle=swiftshader',
        '--use-vulkan=swiftshader',
        '--use-webgpu-adapter=swiftshader',
        // Not --disable-vulkan-surface, which some guides add: with it,
        // headless Chromium never composites the WebGPU canvas, so it can't
        // be read back.
      ],
    },
  },
  webServer: {
    // Serves the build output configured in vite.config.ts.
    command: 'npx vite preview --port 4173 --strictPort',
    url: 'http://localhost:4173',
    reuseExistingServer: !process.env.CI,
  },
});
