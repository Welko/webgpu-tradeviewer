import { defineConfig, type ReporterDescription } from '@playwright/test';

// Set in CI, as for the C++ tests (see cpp/cmake/testing.cmake).
const junitDir = process.env.TRADEVIEWER_JUNIT_DIR;
const junit: ReporterDescription[] = junitDir ? [['junit', { outputFile: `${junitDir}/e2e/Playwright.xml` }]] : [];

/**
 * Tests of the built site (docs/, from `npm run build`): smoke tests in
 * Chromium, and the image-test scenes in Chromium and Firefox. Neither needs
 * a GPU, which CI doesn't have: Chromium runs WebGPU on SwiftShader, its own
 * software Vulkan, and Firefox on the system's Vulkan driver, lavapipe in CI.
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
  },
  projects: [
    {
      name: 'chromium',
      use: {
        // Full Chromium in its new headless mode; the default headless shell has no WebGPU.
        browserName: 'chromium',
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
    },
    {
      name: 'firefox',
      testMatch: 'scenes.spec.ts',
      // In CI, Firefox now and then finds no WebGPU adapter for as long as we
      // wait (main's run #27; the same commit passed on dev). A retry starts
      // a new browser, and the run's page marks the test flaky.
      retries: process.env.CI ? 2 : 0,
      use: {
        browserName: 'firefox',
        // Off by default in Playwright's Firefox on Linux.
        launchOptions: { firefoxUserPrefs: { 'dom.webgpu.enabled': true } },
      },
    },
  ],
  webServer: {
    // Serves the build output configured in vite.config.ts.
    command: 'npx vite preview --port 4173 --strictPort',
    url: 'http://localhost:4173',
    reuseExistingServer: !process.env.CI,
  },
});
