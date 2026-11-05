import { expect, test, type Page } from '@playwright/test';
import { gzipSync } from 'node:zlib';
import { waitForAdapter } from './adapter';
import { collectErrors, waitUntilReady, zip } from './helpers';

/**
 * Files that aren't what the viewer expects, opened the way a user would:
 * each must give a clear message, or be read correctly, and leave the page
 * working. Someone checking this program will try these.
 */
const Trades = 'timestamp,side,price,amount\n1575158403572000,sell,7540.78,0.039741\n1575158403579000,buy,7540.79,1.5\n';

async function open(page: Page, name: string, content: Buffer | string) {
  await page.locator('input[type=file]').setInputFiles({
    name,
    mimeType: 'application/octet-stream',
    buffer: typeof content === 'string' ? Buffer.from(content) : content,
  });
}

/** Bytes that aren't anything, with no line ends and every value in them. */
function noise(size: number): Buffer {
  return Buffer.from(Array.from({ length: size }, (_, i) => (i * 37 + 3) % 256 || 1));
}

// A freshly launched browser may have no adapter for a moment (see adapter.ts).
test.beforeAll(async ({ browser }) => {
  const page = await browser.newPage();
  await page.goto(`${test.info().project.use.baseURL}/scene.html`);
  await waitForAdapter(page);
  await page.close();
});

test('files that can\'t be read say why, and leave the trades on show', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  await open(page, 'good.csv', Trades);
  await expect(page.getByTestId('trade-count')).toHaveText('2');

  // Large enough that a cut or damage falls in the middle of the data.
  const gzipped = gzipSync(Trades + '1575158403579000,buy,7540.79,1.5\n'.repeat(100_000));
  const damaged = Buffer.from(gzipped);
  const middle = Math.floor(damaged.length / 2);
  damaged.fill(0xff, middle, middle + 32);
  const zipped = zip('trades.csv', Trades);
  const stored = Buffer.from(zipped);
  stored.writeUInt16LE(99, 8);
  stored.writeUInt16LE(99, stored.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02])) + 10);
  const locked = Buffer.from(zipped);
  locked.writeUInt16LE(1, locked.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02])) + 8);

  const cases: [string, Buffer | string, string][] = [
    ['empty.csv', '', 'The file is empty'],
    ['blank.csv', '\n\n\n', 'The file is empty'],
    ['header.csv', 'timestamp,side,price,amount\n', 'The file has no trades'],
    ['page.html', '<!doctype html>\n<html></html>\n', 'Line 1: no "timestamp" column'],
    ['data.json', '{"trades": []}\n', 'Line 1: no "timestamp" column'],
    ['nan.csv', 'timestamp,side,price,amount\n1,buy,nan,1\n', 'Line 2: the price "nan" isn\'t a number above zero'],
    ['inf.csv', 'timestamp,side,price,amount\n1,buy,1,inf\n', 'Line 2: the amount "inf" isn\'t a number above zero'],
    ['huge.csv', 'timestamp,side,price,amount\n1,buy,1e300,1\n', 'Line 2: the price "1e300" is out of range'],
    ['far.csv', 'timestamp,side,price,amount\n9000000000000000000,buy,1,1\n', 'Line 2: the time "9000000000000000000" is out of range'],
    [
      'book.csv',
      'exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount\nbinance,BTCUSDT,1575158405045139,1575158405045139,true,ask,7541.38,0.085806\n',
      'Line 2: the side "ask" isn\'t buy or sell',
    ],
    ['cut-line.csv', 'timestamp,side,price,amount\n1,buy,1', 'Line 2: expected 4 fields, found 3'],
    ['noise.bin', noise(1000), 'Line 1: no "timestamp" column'],
    ['endless.csv', 'x'.repeat(5_000_000), 'Line 1 is too long for a trades file'],
    ['cut.csv.gz', gzipped.subarray(0, gzipped.length / 2), 'The file is damaged or cut off'],
    ['damaged.csv.gz', damaged, 'The file is damaged or cut off'],
    ['start-only.gz', gzipped.subarray(0, 4), 'The file is damaged or cut off'],
    ['cut.zip', zipped.subarray(0, zipped.length / 2), 'The zip file is damaged or cut off'],
    ['start-only.zip', Buffer.from('PK\x03\x04'), 'The zip file is damaged or cut off'],
    ['empty.zip', Buffer.from([0x50, 0x4b, 0x05, 0x06, ...new Array(18).fill(0)]), 'The zip file holds no files'],
    ['stored.zip', stored, 'The zip file uses compression method 99, which can\'t be read here'],
    ['locked.zip', locked, 'The zip file is password-protected'],
  ];
  for (const [name, content, message] of cases) {
    await test.step(name, async () => {
      await open(page, name, content);
      await expect(page.getByRole('alert')).toHaveText(`${name}: ${message}`);
      await expect(page.getByTestId('trade-count')).toHaveText('2');
      await expect(page.getByTestId('source')).toHaveText('good.csv');
    });
  }

  // Still working: the next good file shows, and the message goes.
  await open(page, 'again.csv', Trades);
  await expect(page.getByTestId('source')).toHaveText('again.csv');
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.getByRole('button', { name: 'Whole day' }).click();
  await expect(page.getByTestId('view-count')).toHaveText('2');

  expect(errors).toEqual([]);
});

test('what a file holds decides how it\'s read, not what it\'s called', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const cases: [string, Buffer | string][] = [
    ['plain-named.csv.gz', Trades],
    ['plain-named.zip', Trades],
    ['gzip-named.csv', gzipSync(Trades)],
    ['zip-named.csv', zip('trades.csv', Trades)],
    ['no-extension', gzipSync(Trades)],
    ['marked.csv', `﻿${Trades}`],
  ];
  for (const [name, content] of cases) {
    await test.step(name, async () => {
      await open(page, name, content);
      await expect(page.getByTestId('source')).toHaveText(name);
      await expect(page.getByRole('alert')).toHaveCount(0);
      await expect(page.getByTestId('trade-count')).toHaveText('2');
    });
  }
  expect(errors).toEqual([]);
});

test('odd but valid trades show without breaking the page', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const noon = '1575201600000000';
  const cases: [string, string][] = [
    ['one trade', `${noon},buy,7500,1\n`],
    ['the same moment', `${noon},buy,7500,1\n${noon},sell,7501,1\n`],
    ['many decimals', `${noon},buy,0.123456789012345678901234567890,0.123456789012345678901234567890\n`],
    ['tiny price', `${noon},buy,1e-18,1\n`],
    ['enormous price', `${noon},buy,1e18,1\n`],
    ['tiny amount', `${noon},buy,7500,1e-300\n`],
    ['enormous amount', `${noon},buy,7500,1e17\n`],
    ['before 1970', '-1000000000,buy,7500,1\n'],
    ['the last moment', '8640000000000000000,buy,7500,1\n'],
    ['the first moment', '-8640000000000000000,buy,7500,1\n'],
  ];
  const box = (await page.locator('canvas').boundingBox())!;
  for (const [name, rows] of cases) {
    await test.step(name, async () => {
      await open(page, `${name}.csv`, `timestamp,side,price,amount\n${rows}`);
      await expect(page.getByTestId('source')).toHaveText(`${name}.csv`);
      await expect(page.getByRole('alert')).toHaveCount(0);
      await page.getByRole('button', { name: 'Whole day' }).click();
      await page.getByLabel('Size').selectOption('log');
      await page.getByLabel('Size').selectOption('area');
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
      await page.mouse.wheel(0, -300);
      await page.mouse.move(box.x + 1, box.y + 1);
      await page.getByRole('button', { name: '12:00–13:00' }).click();
      await expect(page.locator('[data-status]')).toHaveAttribute('data-status', 'ready');
      await expect(page.getByTestId('view-range')).toBeVisible();
    });
  }
  expect(errors).toEqual([]);
});

test('the Binance sample says so when it can\'t be fetched', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('/');
  await waitUntilReady(page);
  const load = page.getByRole('button', { name: /Load Binance sample/ });

  await page.route('https://data.binance.vision/**', (route) => route.fulfill({ status: 404, body: 'Not found' }));
  await load.click();
  await expect(page.getByRole('alert')).toContainText('answered 404');
  await page.unrouteAll();

  await page.route('https://data.binance.vision/**', (route) => route.abort());
  await load.click();
  await expect(page.getByRole('alert')).toContainText("Can't reach Binance");
  await page.unrouteAll();

  await page.route('https://data.binance.vision/**', (route) => route.fulfill({ status: 200, body: 'not a zip file' }));
  await load.click();
  await expect(page.getByRole('alert')).toContainText('The zip file is damaged or cut off');

  // The trades on show stay, and the page keeps working.
  await expect(page.getByTestId('source')).toContainText('Synthetic');
  // The browser logs a failed request by itself; nothing else may go wrong.
  expect(errors.filter((error) => !error.includes('Failed to load resource'))).toEqual([]);
});
