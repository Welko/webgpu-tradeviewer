/**
 * How headless Chromium gets WebGPU without a GPU, which CI doesn't have: on
 * SwiftShader, its own software Vulkan. Shared by the Playwright tests
 * (playwright.config.ts) and the benchmark (scripts/benchmark.mjs).
 */
export const chromiumLaunchOptions = {
  // Full Chromium in its new headless mode; the default headless shell has no WebGPU.
  channel: 'chromium',
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
};
