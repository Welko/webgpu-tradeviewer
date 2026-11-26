import { useEffect, useMemo, useState, type RefObject } from 'react';
import { setNotices } from '../notices';
import type { GpuInfo } from './gpuInfo';
import type { LiveViewer, OnStatus } from './useViewer';
import type {
  Axes,
  GpuHealth,
  Instrument,
  Legend,
  PickedColumn,
  PickedTrade,
  Summaries,
  TimeRange,
  TradeStats,
  Viewer,
} from './viewerModule';

/** What the page around the canvas shows of the viewer, as of its last refresh. */
export interface ViewerSnapshot {
  viewer: Viewer;
  view: TimeRange;
  viewStats: TradeStats;
  instrument: Instrument;
  axes: Axes;
  legend: Legend;
  picked: PickedTrade | null;
  pickedColumn: PickedColumn | null;
  summaries: Summaries | null;
  /** The canvas in CSS pixels. */
  sizeCssPx: { width: number; height: number };
  cssPixelsPerPixel: number;
  refresh: () => void;
}

/**
 * Draws `viewer` when it needs it, one frame at a time: requesting one only
 * while it needs drawing, so an idle page schedules nothing.
 */
function createFrameLoop({ viewer, signal }: LiveViewer): () => void {
  let scheduled = false;
  const tick = (now: number) => {
    scheduled = false;
    if (signal.aborted) {
      return;
    }
    viewer.update(now);
    if (viewer.needsRender()) {
      viewer.render();
    }
    // Still needed after drawing: an animation is running, or the surface
    // skipped this frame.
    if (viewer.needsRender()) {
      requestFrame();
    }
  };
  const requestFrame = () => {
    if (!scheduled) {
      scheduled = true;
      requestAnimationFrame(tick);
    }
  };
  return requestFrame;
}

/** Asks for a frame and takes a new snapshot; says the viewer is ready the first time, and whenever its instrument changes. */
function createRefresh(live: LiveViewer, setSnapshot: (snapshot: ViewerSnapshot) => void, onStatusRef: RefObject<OnStatus | undefined>) {
  const { viewer, canvas, signal } = live;
  const requestFrame = createFrameLoop(live);
  let instrument: Instrument | null = null;
  const refresh = () => {
    if (signal.aborted) {
      return;
    }
    requestFrame();
    // If it fails instead, the viewer logs why, which reportError() hears.
    const health = viewer.gpuHealth() as GpuHealth | null;
    setNotices('gpu', health?.restarting ? [{ severity: 'warning', message: 'The GPU was lost; restarting it…' }] : []);
    // Other trades may be in another instrument.
    const current = viewer.instrument() as Instrument;
    if (JSON.stringify(current) !== JSON.stringify(instrument)) {
      instrument = current;
      onStatusRef.current?.({ kind: 'ready', gpu: viewer.info() as GpuInfo, viewer, instrument, refresh });
    }
    const box = canvas.getBoundingClientRect();
    setSnapshot({
      viewer,
      view: viewer.view(),
      viewStats: viewer.viewStats(),
      instrument: current,
      axes: viewer.axes() as Axes,
      legend: viewer.legend() as Legend,
      picked: viewer.pickedTrade() as PickedTrade | null,
      pickedColumn: viewer.pickedColumn() as PickedColumn | null,
      summaries: viewer.summaries() as Summaries | null,
      sizeCssPx: { width: box.width, height: box.height },
      cssPixelsPerPixel: canvas.width > 0 ? canvas.clientWidth / canvas.width : 1,
      refresh,
    });
  };
  return refresh;
}

/**
 * The viewer's state for the page, and `refresh`, which draws it again and
 * updates that state: call it after any change to the viewer. Null while
 * there's no viewer.
 */
export function useViewerSnapshot(
  live: LiveViewer | null,
  onStatusRef: RefObject<OnStatus | undefined>,
): { snapshot: ViewerSnapshot | null; refresh: (() => void) | null } {
  const [snapshot, setSnapshot] = useState<ViewerSnapshot | null>(null);
  const refresh = useMemo(() => (live ? createRefresh(live, setSnapshot, onStatusRef) : null), [live, onStatusRef]);

  useEffect(() => {
    if (!live || !refresh) {
      return;
    }
    // When the GPU is lost, and once it's back.
    live.viewer.setWakeCallback(refresh);
    refresh();
    return () => {
      setSnapshot(null);
    };
  }, [live, refresh]);

  return { snapshot, refresh };
}
