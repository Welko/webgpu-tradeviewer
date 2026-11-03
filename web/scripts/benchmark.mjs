#!/usr/bin/env node
/**
 * Runs the web benchmark (benchmark.html, see src/benchmark.ts) in headless
 * Chromium and writes what it measured as BenchmarkApp does natively: a
 * Markdown table, and beside it <table>.json, which
 * tools/check-regressions.mjs checks.
 *
 * Usage: node scripts/benchmark.mjs <table.md> [trades.csv[.gz]] [--url <site>] [--commit <sha>]
 *   --url: where the built site is served (npm run build, then
 *   npx vite preview); http://localhost:4173 by default. The trades file is
 *   served to the page from here, so the site needs no copy of it; without
 *   one, the synthetic day. --commit names the commit measured, as natively.
 */
import { chromium } from '@playwright/test';
import { createReadStream, statSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { cpus } from 'node:os';
import { basename } from 'node:path';
import { chromiumLaunchOptions } from './chromium.mjs';

// The page takes about a minute in CI; more means it's stuck.
const TimeoutMs = 5 * 60_000;

function option(args, name) {
  const at = args.indexOf(name);
  return at >= 0 ? args.splice(at, 2)[1] : undefined;
}

/** The CPU's model and how many threads it runs, as BenchmarkApp describes it. */
function describeCpu() {
  return `${cpus()[0]?.model ?? 'an unknown CPU'} · ${cpus().length} threads`;
}

/**
 * Serves `path` to the page on a port of its own: Playwright's routes would
 * pass a day's file through the browser's debugging protocol, which is slow.
 * Resolves to its URL.
 */
function serveFile(path) {
  const server = createServer((request, response) => {
    response.writeHead(200, {
      'Content-Length': statSync(path).size,
      'Access-Control-Allow-Origin': '*',
    });
    createReadStream(path).pipe(response);
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => resolve({ server, url: `http://127.0.0.1:${server.address().port}/${basename(path)}` }));
  });
}

async function main(args) {
  const site = (option(args, '--url') ?? 'http://localhost:4173').replace(/\/$/, '');
  const commit = option(args, '--commit');
  const [tablePath, tradesPath] = args;
  if (!tablePath || args.length > 2) {
    console.error('Usage: node scripts/benchmark.mjs <table.md> [trades.csv[.gz]] [--url <site>] [--commit <sha>]');
    return 2;
  }

  const trades = tradesPath ? await serveFile(tradesPath) : undefined;
  const browser = await chromium.launch(chromiumLaunchOptions);
  try {
    const page = await browser.newPage();
    page.on('console', (message) => message.type() === 'error' && console.error(`Page: ${message.text()}`));
    page.on('pageerror', (error) => console.error(`Page: ${error.message}`));

    // A freshly launched Chromium may have no adapter for a moment while
    // SwiftShader starts up (see tests/adapter.ts): wait on a page that
    // doesn't need one.
    await page.goto(`${site}/scene.html`);
    const deadline = Date.now() + 15_000;
    while (!(await page.evaluate(async () => !!(await navigator.gpu?.requestAdapter())))) {
      if (Date.now() > deadline) {
        console.error('No WebGPU adapter within 15 s');
        return 1;
      }
      await new Promise((resolve) => setTimeout(resolve, 250));
    }

    const query = trades ? `?trades=${encodeURIComponent(trades.url)}` : '';
    await page.goto(`${site}/benchmark.html${query}`);
    await page.waitForFunction(() => document.body.dataset.status !== 'running', null, { timeout: TimeoutMs, polling: 500 });
    const { result, error } = await page.evaluate(() => ({ result: window.benchmarkResult, error: window.benchmarkError }));
    if (!result) {
      console.error(error ?? 'The benchmark page finished without a result');
      return 1;
    }

    const gpu = `${result.gpu} in Chromium ${browser.version()}`;
    writeFileSync(tablePath, result.table.replace(`GPU: ${result.gpu}.`, `GPU: ${gpu}.`));
    const json = { platform: result.platform, gpu, cpu: describeCpu(), trades: result.trades, metrics: result.metrics };
    if (commit) {
      json.commit = commit;
    }
    writeFileSync(tablePath.replace(/\.md$/, '') + '.json', `${JSON.stringify(json, null, 2)}\n`);
    return 0;
  } finally {
    await browser.close();
    trades?.server.close();
  }
}

process.exit(await main(process.argv.slice(2)));
