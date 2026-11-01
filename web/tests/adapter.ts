import { expect, type Page } from '@playwright/test';

/**
 * Waits until the page gets a WebGPU adapter. A freshly launched browser may
 * have none for a moment: headless Chromium's first request returns null
 * while SwiftShader starts up, so this also warms the browser up for the
 * tests. On a timeout, the failure says which step was missing: WebGPU
 * itself, or an adapter.
 */
export async function waitForAdapter(page: Page) {
  await expect
    .poll(
      () =>
        page.evaluate(async () => {
          if (!navigator.gpu) {
            return 'navigator.gpu is missing: WebGPU is off or blocklisted';
          }
          try {
            const adapter = await navigator.gpu.requestAdapter();
            return adapter ? 'adapter' : 'requestAdapter() returned null: no usable GPU or driver';
          } catch (error) {
            return `requestAdapter() threw: ${error}`;
          }
        }),
      { message: 'No WebGPU adapter within 15 s', timeout: 15_000 },
    )
    .toBe('adapter');
}
