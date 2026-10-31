import createViewerModule, { type MainModule, type TimeRange, type TradeStats, type Viewer } from '../wasm/ViewerApp.js';

export type { TimeRange, TradeStats, Viewer };

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

/** What Viewer.pickedColumn() returns: the column summary under the pointer. */
export interface PickedColumn {
  /** The column's time range in microseconds since 1970, UTC, start inclusive and end exclusive. */
  startUs: number;
  endUs: number;
  count: number;
  buyCount: number;
  /** In BTC. */
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
  /** In BTC. */
  amount: number;
  side: 'buy' | 'sell';
  /** Its marker's center, from the canvas's top left corner. */
  centerXPx: number;
  centerYPx: number;
}

/** One marker size in a legend. Where the scale stops growing, a size also stands for all amounts past it. */
export interface LegendSize {
  /** In BTC. */
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

// One wasm instance for the page; each component creates its own Viewer in it.
let modulePromise: Promise<MainModule> | null = null;

function loadModule(): Promise<MainModule> {
  modulePromise ??= createViewerModule();
  return modulePromise;
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
