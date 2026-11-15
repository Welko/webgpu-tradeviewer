import { expect, test } from '@playwright/test';
import { waitForAdapter } from './adapter';
import { collectErrors, waitUntilReady } from './helpers';

/**
 * Everything that goes wrong reaches one place on the page, including what
 * nothing expected, and the page stays usable or says to reload it.
 */
const Noon = '1575201600000000';
const Header = 'timestamp,side,price,amount\n';

// A freshly launched browser may have no adapter for a moment (see adapter.ts).
test.beforeAll(async ({ browser }) => {
  const page = await browser.newPage();
  await page.goto(`${test.info().project.use.baseURL}/scene.html`);
  await waitForAdapter(page);
  await page.close();
});

test.beforeEach(async ({ page }) => {
  await page.goto('/');
  await waitUntilReady(page);
});

test('files that load but look odd warn, until other trades replace them', async ({ page }) => {
  const errors = collectErrors(page);
  const input = page.locator('input[type=file]');
  const open = (name: string, content: string) => input.setInputFiles({ name, mimeType: 'text/csv', buffer: Buffer.from(content) });
  await expect(page.getByRole('status')).toHaveCount(0);

  await open('seconds.csv', `${Header}1575201600,buy,7500,1\n`);
  await expect(page.getByRole('status')).toHaveText("seconds.csv: These trades are dated around 1970. If that's wrong, their times may be in another unit");
  await expect(page.getByTestId('trade-count')).toHaveText('1');

  // Refused: the trades on show stay, and so does what's odd about them.
  await open('bad.csv', `${Header}1,hold,1,1\n`);
  await expect(page.getByRole('alert')).toHaveText('bad.csv: Line 2: the side "hold" isn\'t buy or sell');
  await expect(page.getByRole('status')).toHaveCount(1);

  await open('fine.csv', `${Header}${Noon},buy,7500,1\n`);
  await expect(page.getByTestId('source')).toHaveText('fine.csv');
  await expect(page.getByRole('status')).toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);

  await open('tiny.csv', `${Header}${Noon},buy,7500,1e-300\n`);
  const warnings = page.getByRole('status');
  await expect(warnings).toHaveCount(2);
  await expect(warnings.nth(0)).toContainText('more decimals than totals can count');
  await expect(warnings.nth(1)).toContainText('too small to count');

  // Dismissed, it stays gone.
  await page.getByRole('button', { name: 'Dismiss' }).first().click();
  await expect(warnings).toHaveCount(1);
  expect(errors).toEqual([]);
});

test('errors the viewer logs reach the page, counted when they repeat', async ({ page }) => {
  await page.evaluate(() => {
    for (let i = 0; i < 3; i++) {
      window.viewerTesting!.logError('The summaries could not be uploaded');
    }
  });
  await expect(page.getByRole('alert')).toHaveText('The summaries could not be uploaded (3 times)');
  await expect(page.getByRole('button', { name: 'Reload' })).toBeVisible();

  // A flood of different ones can't fill the page.
  await page.evaluate(() => {
    for (let i = 0; i < 100; i++) {
      window.viewerTesting!.logError(`Error number ${i}`);
    }
  });
  await expect(page.getByRole('alert')).toHaveCount(6);
  await expect(page.getByRole('alert').last()).toContainText('There are more errors');
});

test('errors nothing expected say so, and Reload starts over', async ({ page }) => {
  await page.evaluate(() => {
    setTimeout(() => {
      throw new Error('boom');
    });
  });
  await expect(page.getByRole('alert')).toHaveText('Something unexpected went wrong: boom');

  await page.evaluate(() => {
    void Promise.reject(new Error('rejected'));
  });
  await expect(page.getByRole('alert').nth(1)).toHaveText('Something unexpected went wrong: rejected');

  // The viewer still works.
  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(page.getByTestId('view-range')).toContainText('2019-12-02 00:00:00');

  await page.getByRole('button', { name: 'Reload' }).first().click();
  await waitUntilReady(page);
  await expect(page.getByTestId('notices')).toHaveCount(0);
});

test('a crash inside the wasm code starts the viewer over, and says so', async ({ page }) => {
  const first = await page.evaluate(() => window.viewer!.tradeStats().count);
  await page.evaluate(() => {
    // Uncaught, as in a frame callback.
    setTimeout(() => window.viewerTesting!.trap());
  });
  await expect(page.getByRole('alert')).toHaveText(/^Something went wrong, and the viewer started over with the demo data/, { timeout: 30_000 });
  await expect(page.getByRole('alert')).toHaveCount(1);
  await expect(page.locator('[data-status]')).toHaveAttribute('data-status', 'ready');
  await expect.poll(() => page.evaluate(() => window.viewer?.tradeStats().count)).toBe(first);
});

test('if drawing the page itself fails, it says so instead of going blank', async ({ page }) => {
  await page.evaluate(() => {
    (window.viewer as unknown as { axes: () => unknown }).axes = () => ({ times: null, prices: null });
  });
  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(page.getByRole('alert')).toContainText('The page stopped because something went wrong');
  await page.getByRole('button', { name: 'Reload' }).click();
  await waitUntilReady(page);
  await expect(page.getByTestId('notices')).toHaveCount(0);
});
