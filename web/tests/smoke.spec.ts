import { expect, test, type Page } from '@playwright/test';
import { gzipSync } from 'node:zlib';

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

/** Two trades, hand-written in Tardis's format and gzipped as Tardis serves it. */
const TwoTrades = gzipSync(
  'exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n' +
    'binance,BTCUSDT,1575158403572000,1575158403820370,1,sell,7540.78,0.039741\n' +
    'binance,BTCUSDT,1575158403579000,1575158403820405,2,buy,7540.79,1.5\n',
);

/** What the last drawn frame showed. */
interface Frame {
  /** RGBA in the bottom right corner, in the axis margins: plain background. */
  corner: number[];
  /** Pixels mostly green or red: buys and sells. */
  buyPixels: number;
  sellPixels: number;
}

/**
 * Before the page loads: counts requestAnimationFrame calls, and reads the
 * canvas right after each frame callback. Later, a presented WebGPU canvas
 * reads back empty.
 */
async function instrument(page: Page) {
  await page.addInitScript(() => {
    const probe = window as unknown as { frameRequests: number; lastFrame: Frame | null };
    probe.frameRequests = 0;
    probe.lastFrame = null;
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
          const data = context.getImageData(0, 0, canvas.width, canvas.height).data;
          let buyPixels = 0;
          let sellPixels = 0;
          for (let i = 0; i < data.length; i += 4) {
            buyPixels += data[i + 1] > data[i] + 50 ? 1 : 0;
            sellPixels += data[i] > data[i + 1] + 50 ? 1 : 0;
          }
          const corner = (canvas.width * canvas.height - 2) * 4;
          probe.lastFrame = { corner: Array.from(data.slice(corner, corner + 4)), buyPixels, sellPixels };
        }
      });
    };
  });
}

const frameRequests = (page: Page) =>
  page.evaluate(() => (window as unknown as { frameRequests: number }).frameRequests);

const lastFrame = (page: Page) => page.evaluate(() => (window as unknown as { lastFrame: Frame | null }).lastFrame);

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

test('starts WebGPU and draws the background and both sides\' trades', async ({ page }) => {
  const errors = collectErrors(page);
  await instrument(page);
  await page.goto('/');
  await waitUntilReady(page);

  await expect.poll(() => lastFrame(page)).not.toBeNull();
  const { corner, buyPixels, sellPixels } = (await lastFrame(page))!;
  // The stylesheet's --plot-background, #12141a.
  const [red, green, blue, alpha] = corner;
  expect(Math.abs(red - 18)).toBeLessThanOrEqual(2);
  expect(Math.abs(green - 20)).toBeLessThanOrEqual(2);
  expect(Math.abs(blue - 26)).toBeLessThanOrEqual(2);
  expect(alpha).toBe(255);
  expect(buyPixels).toBeGreaterThan(100);
  expect(sellPixels).toBeGreaterThan(100);

  expect(errors).toEqual([]);
});

test('the wheel zooms, dragging pans, and the buttons jump back', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const range = page.getByTestId('view-range');
  const hour = '2019-12-01 12:00:00.000 UTC – 2019-12-01 13:00:00.000 UTC';
  await expect(range).toHaveText(hour);
  const timeLabels = page.getByTestId('time-label');
  await expect(timeLabels.first()).toHaveText('12:00');
  await expect(timeLabels.filter({ hasText: /^12:30$/ })).toHaveCount(1);
  expect(await page.getByTestId('price-label').count()).toBeGreaterThan(2);

  // Zooming in around the middle keeps 12:30 in the middle.
  const box = (await page.locator('canvas').boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.wheel(0, -500);
  await expect(range).toHaveText(/^2019-12-01 12:\d\d:\d\d\.\d{3} UTC – 2019-12-01 12:\d\d:\d\d\.\d{3} UTC$/);
  const zoomed = await range.textContent();

  // Dragging right shows earlier trades.
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 100, box.y + box.height / 2);
  await page.mouse.up();
  await expect(range).not.toHaveText(zoomed!);
  expect((await range.textContent())! < zoomed!).toBe(true);

  // Few trades from here: drawing the whole synthetic day takes SwiftShader many seconds.
  await page.locator('input[type=file]').setInputFiles({ name: 'trades.csv.gz', mimeType: 'application/gzip', buffer: TwoTrades });
  await expect(page.getByTestId('trade-count')).toHaveText('2');
  await expect(range).toHaveText(hour);
  // Both trades are a few seconds after midnight.
  await expect(page.getByTestId('view-count')).toHaveText('0');
  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(range).toHaveText('2019-12-01 00:00:00.000 UTC – 2019-12-02 00:00:00.000 UTC');
  await expect(timeLabels.first()).toHaveText('2019-12-01');
  await expect(page.getByTestId('view-count')).toHaveText('2');
  await expect(page.getByTestId('view-amount')).toHaveText('1.539741');
  await page.getByRole('button', { name: '12:00–13:00' }).click();
  await expect(range).toHaveText(hour);

  expect(errors).toEqual([]);
});

test('the stylesheet sets the plot\'s colors, and the legend follows', async ({ page }) => {
  const errors = collectErrors(page);
  await instrument(page);
  await page.addInitScript(() => {
    document.addEventListener('DOMContentLoaded', () => {
      document.documentElement.style.setProperty('--plot-background', '#ffffff');
      document.documentElement.style.setProperty('--buy', 'rgb(0, 0, 255)');
    });
  });
  await page.goto('/');
  await waitUntilReady(page);

  await expect.poll(() => lastFrame(page)).not.toBeNull();
  const { corner, buyPixels, sellPixels } = (await lastFrame(page))!;
  expect(corner).toEqual([255, 255, 255, 255]);
  // Buys are blue now, so only sells count as green or red.
  expect(buyPixels).toBe(0);
  expect(sellPixels).toBeGreaterThan(100);
  const buySwatch = page.getByTestId('legend').getByText('Buy').locator('circle');
  await expect(buySwatch).toHaveAttribute('fill', 'rgb(0 0 255)');

  expect(errors).toEqual([]);
});

test('the legend shows both sides and the sizes of the chosen scale', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const legend = page.getByTestId('legend');
  await expect(legend).toContainText('Buy');
  await expect(legend).toContainText('Sell');

  // Each size ends where the scale stops growing.
  const sizes = page.getByTestId('legend-size');
  await expect(sizes).toHaveText(['≤ 0.0625', '0.25', '1', '4', '≥ 7.11']);
  const radius = (index: number) => sizes.nth(index).locator('circle').getAttribute('r');
  expect(Number(await radius(2))).toBe(6);

  await page.getByLabel('Size').selectOption('log');
  await expect(sizes).toHaveText(['≤ 0.0001', '0.01', '1', '≥ 100']);
  expect(Number(await radius(3))).toBe(16);

  expect(errors).toEqual([]);
});

test('pointing at a trade shows its time, price, amount and side', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  await page.locator('input[type=file]').setInputFiles({ name: 'trades.csv.gz', mimeType: 'application/gzip', buffer: TwoTrades });
  await expect(page.getByTestId('trade-count')).toHaveText('2');
  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(page.getByTestId('view-count')).toHaveText('2');

  // Both trades are 3.5 s into the day, at the left edge. The price axis fits
  // them with 5% of a price's 0.001% on either side: the buy (7540.79) is 53%
  // of the way up the plot, the sell (7540.78) 47%.
  const box = (await page.locator('canvas').boundingBox())!;
  const plotHeightCssPx = box.height - 22;  // AxisMarginsCssPx.bottom
  const heightOf = (fraction: number) => box.y + plotHeightCssPx * (1 - fraction);
  const tooltip = page.getByTestId('trade-tooltip');

  await page.mouse.move(box.x + 2, heightOf(0.53));
  await expect(tooltip.getByTestId('trade-side')).toHaveText('Buy');
  await expect(tooltip.getByTestId('trade-time')).toHaveText('2019-12-01 00:00:03.579000 UTC');
  await expect(tooltip.getByTestId('trade-price')).toHaveText('7,540.79');
  await expect(tooltip.getByTestId('trade-amount')).toHaveText('1.5');

  await page.mouse.move(box.x + 2, heightOf(0.47));
  await expect(tooltip.getByTestId('trade-side')).toHaveText('Sell');
  await expect(tooltip.getByTestId('trade-time')).toHaveText('2019-12-01 00:00:03.572000 UTC');
  await expect(tooltip.getByTestId('trade-amount')).toHaveText('0.039741');

  // Nothing within 24 pixels, then off the canvas.
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await expect(tooltip).toHaveCount(0);
  await page.mouse.move(box.x + 2, heightOf(0.53));
  await expect(tooltip).toHaveCount(1);
  await page.mouse.move(box.x - 5, box.y - 5);
  await expect(tooltip).toHaveCount(0);

  expect(errors).toEqual([]);
});

test('a crowded view is summarized, says so, and can show every trade instead', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const summaries = page.getByTestId('summaries');
  // About 20 synthetic trades per pixel column in the hour: below the threshold of 32.
  await expect(summaries).toHaveCount(0);
  await expect(page.getByTestId('legend-size').first()).toBeVisible();

  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(summaries).toHaveText(/^Summarized: [\d,]+ bars, ~\d+ trades each$/);
  await expect(page.getByTestId('legend-bars')).toBeVisible();
  await expect(page.getByTestId('legend-size')).toHaveCount(0);

  // Down the middle of the plot until a bar is within reach.
  const box = (await page.locator('canvas').boundingBox())!;
  const tooltip = page.getByTestId('column-tooltip');
  for (let y = box.y + 5; y < box.y + box.height - 25 && (await tooltip.count()) === 0; y += 10) {
    await page.mouse.move(box.x + box.width / 2, y);
  }
  await expect(tooltip.getByTestId('column-count')).toHaveText(/^[\d,]+ trades: [\d,]+ buys, [\d,]+ sells$/);
  await expect(tooltip.getByTestId('column-time')).toContainText('2019-12-01 ');
  await page.mouse.move(box.x - 5, box.y - 5);
  await expect(tooltip).toHaveCount(0);

  // Narrow, the hour is crowded too, and quick to draw in full: SwiftShader
  // takes seconds over every trade of the day, delaying the next tests.
  await page.getByRole('button', { name: '12:00–13:00' }).click();
  await page.setViewportSize({ width: 500, height: 600 });
  await expect(summaries).toHaveCount(1);
  await page.getByLabel('Every trade').check();
  await expect(summaries).toHaveCount(0);
  await expect(page.getByTestId('legend-size').first()).toBeVisible();
  await page.getByLabel('Every trade').uncheck();
  await expect(summaries).toHaveCount(1);

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

test('shows synthetic trades until a file is opened', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  await expect(page.getByTestId('source')).toContainText('Synthetic');
  await expect(page.getByTestId('trade-count')).not.toHaveText('0');

  const fileInput = page.locator('input[type=file]');
  await fileInput.setInputFiles({ name: 'trades.csv.gz', mimeType: 'application/gzip', buffer: TwoTrades });
  await expect(page.getByTestId('source')).toHaveText('trades.csv.gz');
  await expect(page.getByTestId('trade-count')).toHaveText('2');

  // A file that isn't trades leaves them as they were.
  await fileInput.setInputFiles({ name: 'page.csv', mimeType: 'text/csv', buffer: Buffer.from('<html>\n') });
  await expect(page.getByRole('alert')).toHaveText('page.csv: Line 1: no "timestamp" column');
  await expect(page.getByTestId('trade-count')).toHaveText('2');

  expect(errors).toEqual([]);
});
