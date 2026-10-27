import { expect, test, type Page } from '@playwright/test';

/** Collects console errors and uncaught exceptions from the page. */
function collectErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on('console', (message) => {
    if (message.type() === 'error') {
      errors.push(message.text());
    }
  });
  page.on('pageerror', (error) => errors.push(error.message));
  return errors;
}

/** Waits until the viewer reports that WebGPU is ready, failing with its error otherwise. */
async function waitUntilReady(page: Page) {
  const status = page.locator('[data-status]');
  await expect(status).not.toHaveAttribute('data-status', 'loading', { timeout: 30_000 });
  await expect(status, await status.textContent() ?? '').toHaveAttribute('data-status', 'ready');
}

/**
 * Instruments the page before it loads: counts requestAnimationFrame calls,
 * and right after each frame callback copies the canvas's centre pixel.
 * Reading in the same task as the drawing matters: once the frame has been
 * presented, reading a WebGPU canvas returns nothing. (Screenshots do still
 * show it.)
 */
async function instrument(page: Page) {
  await page.addInitScript(() => {
    const probe = window as unknown as { frameRequests: number; lastPixel: number[] | null };
    probe.frameRequests = 0;
    probe.lastPixel = null;
    const request = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = (callback) => {
      probe.frameRequests += 1;
      return request((time) => {
        callback(time);
        const canvas = document.querySelector('canvas');
        if (canvas && canvas.width > 0 && canvas.height > 0) {
          const copy = new OffscreenCanvas(canvas.width, canvas.height);
          const context = copy.getContext('2d')!;
          context.drawImage(canvas, 0, 0);
          probe.lastPixel = Array.from(context.getImageData(canvas.width >> 1, canvas.height >> 1, 1, 1).data);
        }
      });
    };
  });
}

const frameRequests = (page: Page) =>
  page.evaluate(() => (window as unknown as { frameRequests: number }).frameRequests);

/** The RGBA of the canvas's centre pixel in the last drawn frame. */
const lastPixel = (page: Page) =>
  page.evaluate(() => (window as unknown as { lastPixel: number[] | null }).lastPixel);

// In a freshly launched headless Chromium, the first adapter request returns
// null while SwiftShader starts up; later ones succeed. Warm up the browser
// so the tests see what a user would.
test.beforeAll(async ({ browser }) => {
  const page = await browser.newPage();
  await page.goto(test.info().project.use.baseURL!);
  await expect
    .poll(() => page.evaluate(async () => (await navigator.gpu?.requestAdapter()) != null), { timeout: 15_000 })
    .toBe(true);
  await page.close();
});

test('starts WebGPU and draws the background', async ({ page }) => {
  const errors = collectErrors(page);
  await instrument(page);
  await page.goto('/');
  await waitUntilReady(page);

  // The engine's background, (0.07, 0.08, 0.10), in 8-bit RGBA.
  await expect.poll(() => lastPixel(page)).not.toBeNull();
  const [red, green, blue, alpha] = (await lastPixel(page))!;
  expect(Math.abs(red - 18)).toBeLessThanOrEqual(2);
  expect(Math.abs(green - 20)).toBeLessThanOrEqual(2);
  expect(Math.abs(blue - 26)).toBeLessThanOrEqual(2);
  expect(alpha).toBe(255);

  expect(errors).toEqual([]);
});

test('an idle page requests no frames, and a resize draws again', async ({ page }) => {
  await instrument(page);
  await page.goto('/');
  await waitUntilReady(page);

  await page.waitForTimeout(500);
  const settled = await frameRequests(page);
  await page.waitForTimeout(1000);
  expect(await frameRequests(page)).toBe(settled);

  await page.setViewportSize({ width: 900, height: 600 });
  await expect.poll(() => frameRequests(page)).toBeGreaterThan(settled);
});
