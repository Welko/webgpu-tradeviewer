/**
 * Measures what showing trades costs in this browser, as BenchmarkApp does
 * natively (see cpp/apps/BenchmarkApp/src/Main.cpp), without drawing:
 * benchmark.html, or benchmark.html?trades=<url> for a trades file (.csv or
 * .csv.gz) instead of the synthetic day. Once done, the body's data-status
 * is "done" or "error", and window.benchmarkResult holds what
 * web/scripts/benchmark.mjs writes to benchmark.json, with the table.
 *
 * Frames aren't measured: CI has no GPU, and on Chromium's software renderer
 * (SwiftShader) they took up to 12 s each, for CPU times far below what the
 * checks can tell apart and GPU times that say nothing about real GPUs.
 * Commit 3f8e4be measured them, should CI ever get GPUs.
 */
import { createViewer, type Viewer } from './viewer/viewerModule';

// Task 3's viewport, at a pixel ratio of 1, as natively.
const WidthPx = 1600;
const HeightPx = 900;
const WarmUpUpdates = 10;
const MeasuredUpdates = 60;
// What the table says instead of frame times, which BenchmarkApp measures natively.
const NotMeasured = 'not measured: no GPU in CI';

/** A number compared between commits (see tools/check-regressions.mjs). */
export interface Metric {
  /** A CSV column: letters, digits and underscores. */
  key: string;
  label: string;
  /** "bytes", compared exactly; "ms", within a margin. */
  unit: 'bytes' | 'ms';
  value: number;
  /** Too unsteady between runs to fail a build on: recorded and shown, never checked. */
  flaky: boolean;
}

/** What the page measured: benchmark.json, less what only the script knows (the CPU, the commit). */
export interface BenchmarkResult {
  /** Its own baseline: wasm32's memory differs, and so do its times. */
  platform: 'web';
  gpu: string;
  trades: number;
  metrics: Metric[];
  /** The view updates as a Markdown table. */
  table: string;
}

declare global {
  interface Window {
    benchmarkResult?: BenchmarkResult;
    benchmarkError?: string;
  }
}

/** A view to measure, and how to get there. */
interface Case {
  name: string;
  /** Starts its metric's key. */
  key: string;
  show: (viewer: Viewer) => void;
}

/** The value `share` of the way up `values`, by the nearest-rank method. */
function getPercentile(values: number[], share: number): number {
  const sorted = [...values].sort((a, b) => a - b);
  const rank = Math.ceil(share * sorted.length);
  return sorted[Math.min(Math.max(rank, 1), sorted.length) - 1];
}

/**
 * What the synthetic day takes in memory, with the whole day in view so
 * column summaries are counted, as natively; the wasm heap besides.
 */
function measureMemory(viewer: Viewer): Metric[] {
  viewer.setSummariesAllowed(true);
  viewer.showDay();
  const memory = viewer.memoryStats();
  const metric = (key: string, label: string, value: number): Metric => ({ key, label, unit: 'bytes', value, flaky: false });
  return [
    metric('memory_trades_bytes', 'Memory: trades (synthetic day)', memory.tradesBytes),
    metric('memory_totals_bytes', 'Memory: running totals', memory.totalsBytes),
    metric('memory_price_tree_bytes', 'Memory: price tree', memory.priceTreeBytes),
    metric('memory_summaries_bytes', 'Memory: column summaries (whole day)', memory.summariesBytes),
    metric('memory_gpu_bytes', 'Memory: GPU buffers and textures', memory.gpuBytes),
    metric('memory_heap_bytes', 'Memory: the whole wasm heap', memory.heapBytes),
  ];
}

/**
 * Loads the trades file at `url`, read in full first so only parsing is
 * timed: { loadMs, setTradesMs }, as natively. The file stays in memory, so
 * how fast it arrives doesn't count; decompressing it does, as natively.
 */
async function loadTrades(viewer: Viewer, url: string): Promise<{ loadMs: number; setTradesMs: number }> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Can't fetch ${url}: ${response.status} ${response.statusText}`);
  }
  const bytes = await response.blob();

  const loadStart = performance.now();
  let stream = bytes.stream();
  if (new URL(url, location.href).pathname.endsWith('.gz')) {
    stream = stream.pipeThrough(new DecompressionStream('gzip'));
  }
  // Its name gives the assets when the file doesn't, as Binance's don't.
  viewer.beginLoad(new URL(url, location.href).pathname.split('/').pop() ?? '');
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) {
      break;
    }
    const error = viewer.appendChunk(value);
    if (error) {
      throw new Error(error);
    }
  }
  const loadMs = performance.now() - loadStart;

  // Finishing the parse, then the derived data and the upload.
  const setStart = performance.now();
  const error = viewer.finishLoad();
  if (error) {
    throw new Error(error);
  }
  return { loadMs, setTradesMs: performance.now() - setStart };
}

/** How long showing a view takes on the CPU, each time: finding its trades, the axes, any summaries. */
function measureUpdates(viewer: Viewer, benchmark: Case): number[] {
  const times: number[] = [];
  for (let update = 0; update < WarmUpUpdates + MeasuredUpdates; update++) {
    const start = performance.now();
    benchmark.show(viewer);
    if (update >= WarmUpUpdates) {
      times.push(performance.now() - start);
    }
  }
  return times;
}

/** 12:00–13:00, zoomed by `factor` around the middle. */
function zoomFromHour(viewer: Viewer, factor: number) {
  viewer.resetView();
  viewer.zoomAt(WidthPx / 2, factor);
}

async function run(): Promise<BenchmarkResult> {
  const viewer = await createViewer('#benchmark', 'default');
  viewer.resize(WidthPx, HeightPx);
  viewer.setPixelRatio(1);
  const info = viewer.info();
  // Browsers often hide the device's name; the vendor and architecture are left, such as "google swiftshader".
  const device = info.description || info.device || [info.vendor, info.architecture].filter(Boolean).join(' ');
  const gpu = [device, info.backend, info.adapter].filter(Boolean).join(' · ');

  // The synthetic day is on show from the start.
  const metrics = measureMemory(viewer);

  const trades = new URLSearchParams(location.search).get('trades');
  if (trades) {
    const { loadMs, setTradesMs } = await loadTrades(viewer, trades);
    metrics.push({ key: 'load_ms', label: 'Reading the trades', unit: 'ms', value: loadMs, flaky: false });
    metrics.push({ key: 'set_trades_ms', label: 'Showing them: derived data and upload', unit: 'ms', value: setTradesMs, flaky: false });
  }

  // Each view says whether crowded views may be summarized, so none depends on the one before.
  const show =
    (summaries: boolean, view: (viewer: Viewer) => void) =>
    (viewer: Viewer) => {
      viewer.setSummariesAllowed(summaries);
      view(viewer);
    };
  const cases: Case[] = [
    { name: 'Whole day, every trade', key: 'day_every_trade', show: show(false, (v) => v.showDay()) },
    { name: 'Whole day', key: 'day', show: show(true, (v) => v.showDay()) },
    { name: '12:00–13:00', key: 'hour', show: show(true, (v) => v.resetView()) },
    { name: '1 minute', key: 'minute', show: show(true, (v) => zoomFromHour(v, 1 / 60)) },
    { name: '1 second', key: 'second', show: show(true, (v) => zoomFromHour(v, 1 / 3600)) },
  ];
  const rows = [
    `GPU: ${gpu}. ${WidthPx} × ${HeightPx} pixels; ${MeasuredUpdates} updates per view after ${WarmUpUpdates} to warm up, ` +
      'nothing drawn. Milliseconds: median (95th percentile).',
    '',
    '| View | Trades in view | View update (CPU) | Frame (CPU) | Frame (GPU) |',
    '|---|--:|--:|--:|--:|',
  ];
  for (const benchmark of cases) {
    const times = measureUpdates(viewer, benchmark);
    const median = getPercentile(times, 0.5);
    metrics.push({ key: `${benchmark.key}_update_ms`, label: `${benchmark.name}: view update (CPU)`, unit: 'ms', value: median, flaky: false });
    const update = `${median.toFixed(3)} (${getPercentile(times, 0.95).toFixed(3)})`;
    rows.push(`| ${benchmark.name} | ${viewer.viewStats().count} | ${update} | ${NotMeasured} | ${NotMeasured} |`);
  }
  rows.push(
    '',
    "Frames aren't drawn: CI's Chromium draws on the CPU (SwiftShader), where one frame of the whole day with every trade " +
      "took 12 s. A frame's CPU time is the same C++ as natively, a fraction of a millisecond, and the native benchmark " +
      'measures it. Commit 3f8e4be measured frames here too.',
  );

  return { platform: 'web', gpu, trades: viewer.tradeStats().count, metrics, table: `${rows.join('\n')}\n` };
}

run()
  .then((result) => {
    window.benchmarkResult = result;
    const memory = result.metrics
      .filter((metric) => metric.unit === 'bytes')
      .map((metric) => `${metric.label}: ${metric.value.toLocaleString('en-US')} B`);
    document.getElementById('result')!.textContent = [result.table, ...memory].join('\n');
    document.body.dataset.status = 'done';
  })
  .catch((error: unknown) => {
    window.benchmarkError = error instanceof Error ? error.message : String(error);
    document.getElementById('result')!.textContent = window.benchmarkError;
    document.body.dataset.status = 'error';
  });
