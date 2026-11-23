import { expect, test, type Page } from '@playwright/test';
import { waitForAdapter } from './adapter';
import { waitUntilReady } from './helpers';

/**
 * Telemetry sends nothing until the user agrees, nothing that identifies them
 * or their trades after, and stops when they change their mind. The page
 * keeps events in window.telemetryEvents instead of sending them
 * (window.recordTelemetry), so this needs no PostHog key, and no network.
 */
const Header = 'timestamp,side,price,amount\n';
// Prices and a file name no event may carry.
const Trades = `${Header}1575201600000000,buy,7123.45,1.5\n1575201660000000,sell,7124.56,0.25\n`;
const FileName = 'secret-strategy.csv';

test.beforeAll(async ({ browser }) => {
  const page = await browser.newPage();
  await page.goto(`${test.info().project.use.baseURL}/scene.html`);
  await waitForAdapter(page);
  await page.close();
});

test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => {
    window.recordTelemetry = true;
  });
  // Nothing may reach PostHog, even when recording.
  await page.route(/posthog\.com/, (route) => {
    throw new Error(`Telemetry reached the network: ${route.request().url()}`);
  });
  await page.goto('/');
  await waitUntilReady(page);
});

function getEvents(page: Page) {
  return page.evaluate(() => window.telemetryEvents ?? []);
}

async function getNames(page: Page) {
  return (await getEvents(page)).map((event) => event.name);
}

function open(page: Page, name: string, content: string) {
  return page.locator('input[type=file]').setInputFiles({ name, mimeType: 'text/csv', buffer: Buffer.from(content) });
}

test('nothing is sent, or even loaded, until the user agrees', async ({ page }) => {
  const consent = page.getByTestId('telemetry-consent');
  await expect(consent).not.toBeChecked();
  await open(page, FileName, Trades);
  await expect(page.getByTestId('trade-count')).toHaveText('2');
  await page.evaluate(() => window.viewer!.flushTelemetry());
  expect(await getEvents(page)).toEqual([]);
});

test('with consent, sessions, loads, view and speed summaries and errors are sent, and no trades', async ({ page }) => {
  await page.getByTestId('telemetry-consent').check();
  await expect.poll(() => getNames(page)).toEqual(['session_started']);
  const [started] = await getEvents(page);
  expect(started.properties).toMatchObject({ schema: 1, platform: 'web', browser: 'Chrome' });
  expect(started.properties.gpuNameSource).toMatch(/^(webgpu|webgl|none)$/);
  expect(started.properties.session).toMatch(/^[0-9a-f]{16}$/);

  await open(page, FileName, Trades);
  await expect(page.getByTestId('trade-count')).toHaveText('2');
  await open(page, 'bad.csv', `${Header}1,hold,1,1\n`);
  await expect(page.getByRole('alert')).toHaveCount(1);
  await page.locator('canvas').hover();
  await page.mouse.wheel(0, -200);
  await page.evaluate(() => {
    window.viewerTesting!.logError('The summaries could not be uploaded');
    window.viewer!.flushTelemetry();
  });

  await expect
    .poll(() => getNames(page))
    .toEqual(expect.arrayContaining(['data_loaded', 'data_load_failed', 'error', 'view_summary', 'perf_summary']));
  const events = await getEvents(page);
  const find = (name: string) => events.find((event) => event.name === name)!.properties;
  expect(find('data_loaded')).toMatchObject({ source: 'file', rows: 2 });
  expect(find('data_load_failed')).toMatchObject({ source: 'file', stage: 'parse' });
  expect(find('error')).toMatchObject({
    kind: 'viewer',
    message: 'The summaries could not be uploaded',
    session: started.properties.session,
  });
  expect(find('view_summary').viewChanges).toBeGreaterThan(0);
  expect(find('perf_summary').frames).toBeGreaterThan(0);
  // Every event belongs to one session, and none holds a price or the file's name.
  for (const event of events) {
    expect(event.properties.session).toBe(started.properties.session);
  }
  const sent = JSON.stringify(events);
  for (const secret of ['7123', '7124', 'secret', 'hold']) {
    expect(sent).not.toContain(secret);
  }
});

test('the choice is remembered, and revoking stops sending at once', async ({ page }) => {
  await page.getByTestId('telemetry-consent').check();
  await page.reload();
  await waitUntilReady(page);
  await expect(page.getByTestId('telemetry-consent')).toBeChecked();
  await expect.poll(() => getNames(page)).toEqual(['session_started']);

  await page.getByTestId('telemetry-consent').uncheck();
  await open(page, FileName, Trades);
  await expect(page.getByTestId('trade-count')).toHaveText('2');
  await page.evaluate(() => {
    window.viewerTesting!.logError('The summaries could not be uploaded');
    window.viewer!.flushTelemetry();
  });
  expect(await getNames(page)).toEqual(['session_started']);

  await page.reload();
  await waitUntilReady(page);
  await expect(page.getByTestId('telemetry-consent')).not.toBeChecked();
  expect(await getEvents(page)).toEqual([]);
});
