import createViewerModule, { type MainModule, type TimeRange, type TradeSummary, type Viewer } from '../wasm/ViewerApp.js';

export type { TimeRange, TradeSummary, Viewer };

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

/** A colour, each channel from 0 to 1. */
export interface Color {
  red: number;
  green: number;
  blue: number;
}

/** The colours the viewer draws with. */
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

/** What Viewer.pickedTrade() returns: the trade under the pointer. */
export interface PickedTrade {
  /** Microseconds since 1970, UTC. */
  timeUs: number;
  price: number;
  /** In BTC. */
  amount: number;
  side: 'buy' | 'sell';
  /** Its marker's centre, in physical pixels from the canvas's top left corner. */
  x: number;
  y: number;
}

/** One marker size in a legend. Where the scale stops growing, a size also stands for all amounts past it. */
export interface LegendSize {
  /** In BTC. */
  amount: number;
  /** In CSS pixels. */
  radius: number;
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
