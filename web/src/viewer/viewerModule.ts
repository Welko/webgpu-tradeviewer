import createViewerModule, { type MainModule, type TimeRange, type TradeStats, type Viewer } from '../wasm/ViewerApp.js';

export type { MainModule, TimeRange, TradeStats, Viewer };

/** A tick on an axis: where along it (0 at the left or bottom, 1 at the other end), and its label. */
export interface AxisTick {
  fraction: number;
  label: string;
}

/** What Viewer.axes() returns. */
export interface Axes {
  times: AxisTick[];
  prices: AxisTick[];
}

/** A color, each channel from 0 to 1. */
export interface Color {
  red: number;
  green: number;
  blue: number;
}

/** The colors the viewer draws with. */
export interface Theme {
  background: Color;
  /** Of the grid lines, which are opaque. */
  grid: Color;
  buy: Color;
  sell: Color;
  /** Of every marker, over the background. */
  markerOpacity: number;
  /** Of the ring around the trade under the pointer. */
  cursor: Color;
}

/** What Viewer.instrument() returns: what the trades are in, read from their file. */
export interface Instrument {
  /** The asset bought and sold, such as "BTC"; "base" if the file doesn't say. */
  base: string;
  /** The asset prices are in, such as "USDT"; "quote" if the file doesn't say. */
  quote: string;
  /** The most decimals the file gives amounts and prices with. */
  amountDecimals: number;
  priceDecimals: number;
}

/** What Viewer.pickedColumn() returns: the column summary under the pointer. */
export interface PickedColumn {
  /** The column's time range in microseconds since 1970, UTC, start inclusive and end exclusive. */
  startUs: number;
  endUs: number;
  count: number;
  buyCount: number;
  /** In the base asset. */
  buyAmount: number;
  sellAmount: number;
  priceLow: number;
  priceHigh: number;
  /** Its bar's center across, top and bottom, from the canvas's top left corner. */
  centerXPx: number;
  topPx: number;
  bottomPx: number;
}

/** What Viewer.summaries() returns while the view is crowded enough to show column summaries. */
export interface Summaries {
  /** Columns across the plot, one per CSS pixel. */
  across: number;
  /** Those with trades: the bars drawn. */
  withTrades: number;
}

/** What Viewer.pickedTrade() returns: the trade under the pointer. */
export interface PickedTrade {
  /** Microseconds since 1970, UTC. */
  timeUs: number;
  price: number;
  /** In the base asset. */
  amount: number;
  side: 'buy' | 'sell';
  /** Its marker's center, from the canvas's top left corner. */
  centerXPx: number;
  centerYPx: number;
}

/** One marker size in a legend. Where the scale stops growing, a size also stands for all amounts past it. */
export interface LegendSize {
  /** In the base asset. */
  amount: number;
  radiusCssPx: number;
  bound: 'exact' | 'atMost' | 'atLeast';
}

/** What Viewer.legend() returns: what the markers look like at the current marker scale. */
export interface Legend {
  buy: Color;
  sell: Color;
  /** Of every marker, over the background. */
  opacity: number;
  /** Smallest first. */
  sizes: LegendSize[];
}

/** Same values as WebGPU's GPUPowerPreference, plus 'default' for no hint. */
export type PowerPreference = 'default' | 'low-power' | 'high-performance';

/** What scenes() returns: a picture for image tests (see cpp/apps/ViewerApp/include/Viewer/Scene.hpp). */
export interface SceneInfo {
  name: string;
  description: string;
  widthPx: number;
  heightPx: number;
}

/** What Viewer.showScene() returns: a pixel, or an area whose every pixel, has a color known in advance. */
export interface ScenePatch {
  what: string;
  /** Its top left corner, from the canvas's top left corner. */
  xPx: number;
  yPx: number;
  widthPx: number;
  heightPx: number;
  rgba: [number, number, number, number];
  /** How far each channel may be off. */
  tolerance: number;
}

/** What Viewer.gpuHealth() returns: how the GPU is doing (see Viewer::App::GpuHealth). */
export interface GpuHealth {
  /** Lost, and a new device is on its way. */
  restarting: boolean;
  /** How many times it was lost and came back. */
  restarts: number;
  /** Why it couldn't come back; nothing draws. */
  failure: string | null;
  /** The first WebGPU errors nothing caught, such as validation errors, which mean a bug. */
  errors: string[];
  errorCount: number;
}

/** What Viewer.memoryStats() returns: what the viewer's large parts hold, room to grow included. */
export interface MemoryStats {
  /** The trades themselves: 25 bytes each. */
  tradesBytes: number;
  /** Derived: amounts and buy counts summed up to each trade, 20 bytes per trade. */
  totalsBytes: number;
  /** Derived: lowest and highest prices of spans of trades, about 2 bytes per trade. */
  priceTreeBytes: number;
  /** Derived: the column summaries on show, while zoomed out. */
  summariesBytes: number;
  /** A file being read, until it's done. */
  loadingBytes: number;
  /** Its GPU buffers and textures, the canvas's own excepted. */
  gpuBytes: number;
  /** The whole wasm heap, which never shrinks; at most 4 GiB. */
  heapBytes: number;
}

/** What Viewer.setStatsCallback() reports for each drawn frame. */
export interface FrameStats {
  /** Which frame this is, counting from 1. */
  frame: number;
  /** Where the frame's CPU time went, while profiling is on. */
  cpu: { acquireMs: number; encodeMs: number; submitMs: number } | null;
  /** How long the frame took on the GPU, from timestamp queries; null when `untimed`. */
  gpu: { totalMs: number; passes: { name: string; gpuMs: number }[] } | null;
  /** Why there are no GPU times, such as 'unsupported' without timestamp-query; null with them. */
  untimed: string | null;
  /** Live GPU objects, and the memory they take. */
  buffers: number;
  textures: number;
  gpuBytes: number;
}

// One wasm instance for the page; each component creates its own Viewer in it.
let modulePromise: Promise<MainModule> | null = null;
const abortListeners = new Set<(reason: string) => void>();

/**
 * The wasm module, loaded on first use. After it aborts (out of memory, or a
 * bug), every Viewer in it is dead, and the next call loads a new one.
 */
export function loadModule(): Promise<MainModule> {
  modulePromise ??= createViewerModule({
    onAbort: (what: unknown) => {
      modulePromise = null;
      const reason = String(what ?? 'unknown reason').replace(/^Aborted\((.*)\)$/s, '$1') || 'unknown reason';
      for (const listener of abortListeners) {
        listener(reason);
      }
    },
  });
  return modulePromise;
}

/** Calls `listener` with why, whenever the wasm module aborts. Returns a function that stops it. */
export function onModuleAbort(listener: (reason: string) => void): () => void {
  abortListeners.add(listener);
  return () => abortListeners.delete(listener);
}

/**
 * Creates a Viewer drawing into the canvas matched by `selector`.
 * Rejects with a readable message if WebGPU is unavailable.
 * The caller owns the result and must call `delete()` on it.
 */
export async function createViewer(selector: string, powerPreference: PowerPreference): Promise<Viewer> {
  if (!('gpu' in navigator)) {
    throw new Error('WebGPU is not available in this browser. Try a recent Chrome, Edge or Firefox.');
  }

  const module = await loadModule();
  const viewer = new module.Viewer();

  try {
    await new Promise<void>((resolve, reject) => {
      viewer.init(selector, powerPreference, (error: string | null) => (error ? reject(new Error(error)) : resolve()));
    });
  } catch (error) {
    viewer.delete();
    throw error;
  }

  return viewer;
}
