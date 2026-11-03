/**
 * Measures what showing trades costs in this browser, as BenchmarkApp does
 * natively (see cpp/apps/BenchmarkApp/src/Main.cpp): benchmark.html, or
 * benchmark.html?trades=<url> for a trades file (.csv or .csv.gz) instead of
 * the synthetic day. Once done, the body's data-status is "done" or "error",
 * and window.benchmarkResult holds what web/scripts/benchmark.mjs writes to
 * benchmark.json, with the table.
 */
import { createViewer, type FrameStats, type Viewer } from './viewer/viewerModule';

// Task 3's viewport, at a pixel ratio of 1, as natively.
const WidthPx = 1600;
const HeightPx = 900;
// Fewer frames than natively's 10 and 60: in CI, Chromium draws on the CPU
// (SwiftShader), where 12:00–13:00 takes half a second a frame.
const WarmUpFrames = 5;
const MeasuredFrames = 30;
// How long a frame's stats may take to arrive.
const StatsTimeoutMs = 10_000;

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
  /** The frame times as a Markdown table. */
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
  /** Starts its metrics' keys. */
  key: string;
  show: (viewer: Viewer) => void;
}

/** One view's measured frames, in milliseconds. */
interface Samples {
  tradesDrawn: number;
  viewUpdate: number[];
  /** Acquire, encode and submit. */
  cpuFrame: number[];
  acquire: number[];
  encode: number[];
  submit: number[];
  /** By pass name, in drawing order of first appearance. */
  passes: Map<string, number[]>;
  gpuTotal: number[];
  /** Why frames had no GPU times, if any didn't. */
  untimed: Set<string>;
}

/** The value `share` of the way up `values`, by the nearest-rank method. */
function getPercentile(values: number[], share: number): number {
  if (values.length === 0) {
    return 0;
  }
  const sorted = [...values].sort((a, b) => a - b);
  const rank = Math.ceil(share * sorted.length);
  return sorted[Math.min(Math.max(rank, 1), sorted.length) - 1];
}

/** "median (95th percentile)". */
function describe(values: number[] | undefined): string {
  if (!values || values.length === 0) {
    return '–';
  }
  return `${getPercentile(values, 0.5).toFixed(3)} (${getPercentile(values, 0.95).toFixed(3)})`;
}

/** 12:00–13:00, zoomed by `factor` around the middle. */
function zoomFromHour(viewer: Viewer, factor: number) {
  viewer.resetView();
  viewer.zoomAt(WidthPx / 2, factor);
}

function nextAnimationFrame(): Promise<void> {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
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
 * Whether performance.now() counts in microseconds: only on a cross-origin
 * isolated page, which vite preview makes this one (see vite.config.ts).
 * Otherwise Chromium rounds to 0.1 ms.
 */
function describeTimer(): string {
  return crossOriginIsolated ? 'Timer: microseconds.' : 'Timer: 0.1 ms steps (the page isn\'t cross-origin isolated).';
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
  viewer.beginLoad();
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

/** Collects every frame's stats, and waits for them to arrive. */
class StatsCollector {
  readonly reported: FrameStats[] = [];
  private wake: (() => void) | null = null;

  constructor(viewer: Viewer) {
    viewer.setStatsCallback((stats: FrameStats) => {
      this.reported.push(stats);
      this.wake?.();
    });
  }

  /** Waits until `count` frames have been reported. */
  async waitFor(count: number): Promise<void> {
    const deadline = performance.now() + StatsTimeoutMs;
    while (this.reported.length < count) {
      if (performance.now() > deadline) {
        throw new Error(`A frame's stats never arrived: ${this.reported.length} reported, waiting for ${count}`);
      }
      await new Promise<void>((resolve) => {
        this.wake = resolve;
        setTimeout(resolve, 100);
      });
    }
  }
}

/** One view's frames: each in its own animation frame, as the viewer draws them, and measured alone. */
async function measure(viewer: Viewer, benchmark: Case, collector: StatsCollector): Promise<Samples> {
  const samples: Samples = {
    tradesDrawn: 0,
    viewUpdate: [],
    cpuFrame: [],
    acquire: [],
    encode: [],
    submit: [],
    passes: new Map(),
    gpuTotal: [],
    untimed: new Set(),
  };
  const firstMeasured = collector.reported.length + WarmUpFrames;
  for (let frame = 0; frame < WarmUpFrames + MeasuredFrames; frame++) {
    await nextAnimationFrame();
    const start = performance.now();
    benchmark.show(viewer);
    const updateMs = performance.now() - start;
    if (frame >= WarmUpFrames) {
      samples.viewUpdate.push(updateMs);
    }
    viewer.render();
    await collector.waitFor(collector.reported.length + 1);
  }
  samples.tradesDrawn = viewer.viewStats().count;
  for (const stats of collector.reported.slice(firstMeasured, firstMeasured + MeasuredFrames)) {
    if (!stats.cpu) {
      throw new Error('A measured frame has no CPU times: profiling is compiled out');
    }
    samples.cpuFrame.push(stats.cpu.acquireMs + stats.cpu.encodeMs + stats.cpu.submitMs);
    samples.acquire.push(stats.cpu.acquireMs);
    samples.encode.push(stats.cpu.encodeMs);
    samples.submit.push(stats.cpu.submitMs);
    // Browsers may have no timestamp-query; then only the CPU is timed.
    if (!stats.gpu) {
      samples.untimed.add(stats.untimed ?? 'unknown');
      continue;
    }
    samples.gpuTotal.push(stats.gpu.totalMs);
    for (const pass of stats.gpu.passes) {
      const times = samples.passes.get(pass.name) ?? [];
      times.push(pass.gpuMs);
      samples.passes.set(pass.name, times);
    }
  }
  return samples;
}

/** The frame times as Markdown, as BenchmarkApp writes them. */
function writeTable(gpu: string, cases: Case[], results: Samples[]): string {
  // Views can draw different passes: crowded ones summarize trades in columns.
  const passNames = [...new Set(results.flatMap((samples) => [...samples.passes.keys()]))];
  const untimed = [...new Set(results.flatMap((samples) => [...samples.untimed]))];
  const lines = [
    `GPU: ${gpu}. ${WidthPx} × ${HeightPx} pixels; ${MeasuredFrames} frames per view after ${WarmUpFrames} to warm up. ` +
      `Milliseconds: median (95th percentile). ${describeTimer()}${untimed.length > 0 ? ` Some frames have no GPU times: ${untimed.join(', ')}.` : ''}`,
    '',
    `| View | Trades drawn | View update (CPU) | Acquire (CPU) | Encode (CPU) | Submit (CPU) |${passNames.map((pass) => ` GPU: ${pass} |`).join('')} GPU total |`,
    `|---|--:|--:|--:|--:|--:|${passNames.map(() => '--:|').join('')}--:|`,
  ];
  cases.forEach((benchmark, i) => {
    const samples = results[i];
    const cells = [
      benchmark.name,
      String(samples.tradesDrawn),
      describe(samples.viewUpdate),
      describe(samples.acquire),
      describe(samples.encode),
      describe(samples.submit),
      ...passNames.map((pass) => describe(samples.passes.get(pass))),
      describe(samples.gpuTotal),
    ];
    lines.push(`| ${cells.join(' | ')} |`);
  });
  return `${lines.join('\n')}\n`;
}

async function run(): Promise<BenchmarkResult> {
  // Kept, not deleted: the page shows the last frame.
  const viewer = await createViewer('#benchmark', 'default');
  viewer.resize(WidthPx, HeightPx);
  viewer.setPixelRatio(1);
  const info = viewer.info();
  // Browsers often hide the device's name; the vendor and architecture are left, such as "google swiftshader".
  const device = info.description || info.device || [info.vendor, info.architecture].filter(Boolean).join(' ');
  const gpu = [device, info.backend, info.adapter].filter(Boolean).join(' · ');

  // The synthetic day is on show from the start.
  const memory = measureMemory(viewer);

  const metrics: Metric[] = [];
  const trades = new URLSearchParams(location.search).get('trades');
  if (trades) {
    const { loadMs, setTradesMs } = await loadTrades(viewer, trades);
    metrics.push({ key: 'load_ms', label: 'Reading the trades', unit: 'ms', value: loadMs, flaky: false });
    metrics.push({ key: 'set_trades_ms', label: 'Showing them: derived data and upload', unit: 'ms', value: setTradesMs, flaky: false });
  }

  const collector = new StatsCollector(viewer);
  viewer.setProfiling(true);

  // Each view says whether crowded views may be summarized, so none depends on the one before.
  const show =
    (summaries: boolean, view: (viewer: Viewer) => void) =>
    (viewer: Viewer) => {
      viewer.setSummariesAllowed(summaries);
      view(viewer);
    };
  // Not the whole day with every trade, as natively: on SwiftShader, each
  // of its frames takes 12 seconds.
  const cases: Case[] = [
    { name: 'Whole day', key: 'day', show: show(true, (v) => v.showDay()) },
    { name: '12:00–13:00', key: 'hour', show: show(true, (v) => v.resetView()) },
    { name: '1 minute', key: 'minute', show: show(true, (v) => zoomFromHour(v, 1 / 60)) },
    { name: '1 second', key: 'second', show: show(true, (v) => zoomFromHour(v, 1 / 3600)) },
  ];
  const results: Samples[] = [];
  for (const benchmark of cases) {
    results.push(await measure(viewer, benchmark, collector));
  }
  viewer.setStatsCallback(null);

  cases.forEach((benchmark, i) => {
    const samples = results[i];
    const time = (suffix: string, label: string, values: number[], flaky: boolean) =>
      metrics.push({ key: `${benchmark.key}_${suffix}`, label: `${benchmark.name}: ${label}`, unit: 'ms', value: getPercentile(values, 0.5), flaky });
    time('update_ms', 'view update (CPU)', samples.viewUpdate, false);
    time('frame_cpu_ms', 'frame (CPU)', samples.cpuFrame, false);
    // A browser's GPU times vary too much between runs, Chromium's software renderer's above all.
    if (samples.gpuTotal.length > 0) {
      time('frame_gpu_ms', 'frame (GPU)', samples.gpuTotal, true);
    }
  });

  return {
    platform: 'web',
    gpu,
    trades: viewer.tradeStats().count,
    metrics: [...memory, ...metrics],
    table: writeTable(gpu, cases, results),
  };
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
