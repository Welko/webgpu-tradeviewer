import { useEffect, useId, useRef, useState } from 'react';
import type { GpuInfo } from './gpuInfo';
import { AxisLabels, AxisMarginsCssPx } from './AxisLabels';
import { readTheme } from './theme';
import { ColumnTooltip, TradeTooltip } from './Tooltip';
import { ViewControls } from './ViewControls';
import {
  createViewer,
  loadModule,
  onModuleAbort,
  type MainModule,
  type Axes,
  type GpuHealth,
  type Instrument,
  type Legend,
  type PickedColumn,
  type PickedTrade,
  type PowerPreference,
  type Summaries, type TimeRange, type TradeStats, type Viewer } from './viewerModule';

export type ViewerStatus =
  | { kind: 'loading' }
  /**
   * `refresh()` draws again; call it after changing the viewer, such as
   * loading trades. `problem` says what went wrong, if anything: the GPU
   * restarting or failing, WebGPU errors, or the viewer having had to start
   * over.
   */
  | { kind: 'ready'; gpu: GpuInfo; viewer: Viewer; instrument: Instrument; refresh: () => void; problem: string | null }
  | { kind: 'error'; message: string };

declare global {
  interface Window {
    /** The viewer on the page, for tests and the console. */
    viewer?: Viewer;
    /** Faults to inject into it, for the browser tests (see Testing in WebMain.cpp). */
    viewerTesting?: MainModule['Testing'];
  }
}

/** What the page should say after the wasm module stopped for `reason`, and started over. */
function describeAbort(reason: string): string {
  if (reason.startsWith('Out of memory')) {
    return `This data is too large for the page. ${reason}. The viewer started over with the demo data.`;
  }
  // A bug: the details are for developers.
  return 'Something went wrong, and the viewer started over with the demo data. The details are in the browser’s console.';
}

/** What the page should say about `health`, if anything. */
function describeHealth(health: GpuHealth | null): string | null {
  if (!health) {
    return null;
  }
  if (health.failure) {
    return `The GPU stopped working: ${health.failure}`;
  }
  if (health.restarting) {
    return 'The GPU was lost; restarting it…';
  }
  if (health.errorCount > 0) {
    return `WebGPU reported ${health.errorCount} error${health.errorCount === 1 ? '' : 's'}: ${health.errors[0]}`;
  }
  return null;
}

// Zooming per CSS pixel of wheel scrolling: a usual 100-pixel step zooms by about 1.22.
const ZoomPerWheelCssPx = 0.002;

/** A wheel event's vertical scroll in CSS pixels, whatever unit it came in. */
function wheelCssPx(event: WheelEvent, pageHeightCssPx: number): number {
  switch (event.deltaMode) {
    case WheelEvent.DOM_DELTA_LINE:
      return event.deltaY * 16;
    case WheelEvent.DOM_DELTA_PAGE:
      return event.deltaY * pageHeightCssPx;
    default:
      return event.deltaY;
  }
}

interface ViewerCanvasProps {
  /** Changing it recreates the viewer on a newly requested adapter. */
  powerPreference: PowerPreference;
  onStatus?: (status: ViewerStatus) => void;
}

/**
 * Hosts the C++ viewer. React owns the frame loop and the canvas size, and
 * forwards input: the wheel zooms around the pointer, dragging pans, and the
 * pointer picks the trade, or column summary, it shows details of.
 * Frames are requested only while the viewer needs them, so an idle page
 * schedules nothing.
 */
export function ViewerCanvas({ powerPreference, onStatus }: ViewerCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const canvasId = `viewer-${useId().replace(/:/g, '')}`;
  const onStatusRef = useRef(onStatus);
  onStatusRef.current = onStatus;
  // Bumped when the wasm module aborts, to start over with a new one.
  const [generation, setGeneration] = useState(0);
  const abortedRef = useRef<string | null>(null);
  const [shown, setShown] = useState<{
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
  } | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current!;
    let disposed = false;
    let viewer: Viewer | null = null;
    let frame = 0;
    let scheduled = false;
    let observer: ResizeObserver | null = null;
    const removeInput = new AbortController();
    let dead = false;
    const stopListening = onModuleAbort((reason) => {
      // Every call into the module would throw from now on.
      dead = true;
      cancelAnimationFrame(frame);
      removeInput.abort();
      console.error(`The viewer stopped: ${reason}`);
      abortedRef.current = describeAbort(reason);
      setGeneration((current) => current + 1);
    });

    onStatusRef.current?.({ kind: 'loading' });

    createViewer(`#${canvasId}`, powerPreference)
      .then((created) => {
        if (disposed) {
          created.delete();
          return;
        }
        viewer = created;
        window.viewer = created;
        // The module the viewer is in, already loaded.
        void loadModule().then((module) => {
          window.viewerTesting = module.Testing;
        });
        created.setTheme(readTheme(canvas.parentElement!));

        const tick = (now: number) => {
          scheduled = false;
          if (dead) {
            return;
          }
          created.update(now);
          if (created.needsRender()) {
            created.render();
          }
          // Still needed after drawing: an animation is running, or the
          // surface skipped this frame.
          if (created.needsRender()) {
            requestFrame();
          }
        };
        const requestFrame = () => {
          if (!scheduled) {
            scheduled = true;
            frame = requestAnimationFrame(tick);
          }
        };
        let sizeCssPx = { width: canvas.clientWidth, height: canvas.clientHeight };
        let problem: string | null = null;
        let instrument = created.instrument() as Instrument;
        const refresh = () => {
          if (dead) {
            return;
          }
          requestFrame();
          const now = describeHealth(created.gpuHealth() as GpuHealth | null) ?? abortedRef.current;
          // Other trades may be in another instrument.
          const current = created.instrument() as Instrument;
          const sameInstrument = JSON.stringify(current) === JSON.stringify(instrument);
          if (now !== problem || !sameInstrument) {
            problem = now;
            instrument = current;
            onStatusRef.current?.({ kind: 'ready', gpu: created.info() as GpuInfo, viewer: created, instrument, refresh, problem });
          }
          setShown({
            viewer: created,
            view: created.view(),
            viewStats: created.viewStats(),
            instrument,
            axes: created.axes() as Axes,
            legend: created.legend() as Legend,
            picked: created.pickedTrade() as PickedTrade | null,
            pickedColumn: created.pickedColumn() as PickedColumn | null,
            summaries: created.summaries() as Summaries | null,
            sizeCssPx,
            cssPixelsPerPixel: canvas.width > 0 ? canvas.clientWidth / canvas.width : 1,
            refresh,
          });
        };

        // CSS pixels to the canvas's physical pixels, which the viewer works in.
        const toPx = (cssPx: number) => (canvas.clientWidth > 0 ? (cssPx * canvas.width) / canvas.clientWidth : cssPx);
        const listen = { signal: removeInput.signal };
        canvas.addEventListener(
          'wheel',
          (event) => {
            event.preventDefault();
            created.zoomAt(toPx(event.offsetX), Math.exp(wheelCssPx(event, canvas.clientHeight) * ZoomPerWheelCssPx));
            refresh();
          },
          { ...listen, passive: false },
        );
        let dragXCssPx: number | null = null;
        canvas.addEventListener(
          'pointerdown',
          (event) => {
            if (event.button === 0) {
              canvas.setPointerCapture(event.pointerId);
              dragXCssPx = event.clientX;
            }
          },
          listen,
        );
        canvas.addEventListener(
          'pointermove',
          (event) => {
            if (dragXCssPx !== null) {
              created.panBy(toPx(event.clientX - dragXCssPx));
              dragXCssPx = event.clientX;
            }
            created.setPointer(toPx(event.offsetX), toPx(event.offsetY));
            refresh();
          },
          listen,
        );
        canvas.addEventListener(
          'pointerleave',
          () => {
            created.clearPointer();
            refresh();
          },
          listen,
        );
        const endDrag = () => {
          dragXCssPx = null;
        };
        canvas.addEventListener('pointerup', endDrag, listen);
        canvas.addEventListener('pointercancel', endDrag, listen);

        observer = new ResizeObserver(([entry]) => {
          const box = entry.devicePixelContentBoxSize?.[0];
          const widthPx = box ? box.inlineSize : Math.round(entry.contentRect.width * devicePixelRatio);
          const heightPx = box ? box.blockSize : Math.round(entry.contentRect.height * devicePixelRatio);
          // Assigning the size clears the canvas, so only do it on a real change.
          if (canvas.width !== widthPx || canvas.height !== heightPx) {
            canvas.width = widthPx;
            canvas.height = heightPx;
          }
          // Also changes when the page is zoomed or moved to another screen, which resizes the canvas in pixels.
          created.setPixelRatio(devicePixelRatio);
          const pixelsPerCssPixel = widthPx / Math.max(1, entry.contentRect.width);
          created.setPlotInsets(
            0,
            0,
            Math.round(AxisMarginsCssPx.right * pixelsPerCssPixel),
            Math.round(AxisMarginsCssPx.bottom * pixelsPerCssPixel),
          );
          created.resize(widthPx, heightPx);
          sizeCssPx = { width: entry.contentRect.width, height: entry.contentRect.height };
          refresh();
        });
        observer.observe(canvas);
        // When the GPU is lost, and once it's back.
        created.setWakeCallback(refresh);
        refresh();

        onStatusRef.current?.({ kind: 'ready', gpu: created.info() as GpuInfo, viewer: created, instrument, refresh, problem });
      })
      .catch((error: unknown) => {
        if (!disposed) {
          onStatusRef.current?.({ kind: 'error', message: error instanceof Error ? error.message : String(error) });
        }
      });

    return () => {
      disposed = true;
      stopListening();
      cancelAnimationFrame(frame);
      observer?.disconnect();
      removeInput.abort();
      setShown(null);
      if (window.viewer === viewer) {
        delete window.viewer;
      }
      if (!dead) {
        viewer?.delete();
      }
    };
  }, [canvasId, powerPreference, generation]);

  return (
    <div className="viewer">
      {shown && (
        <ViewControls
          viewer={shown.viewer}
          view={shown.view}
          viewStats={shown.viewStats}
          instrument={shown.instrument}
          legend={shown.legend}
          summaries={shown.summaries}
          onChange={shown.refresh}
        />
      )}
      <div className="plot">
        <canvas id={canvasId} ref={canvasRef} />
        {shown && <AxisLabels axes={shown.axes} canvasWidthCssPx={shown.sizeCssPx.width} canvasHeightCssPx={shown.sizeCssPx.height} />}
        {shown?.picked && (
          <TradeTooltip
            trade={shown.picked}
            instrument={shown.instrument}
            cssPixelsPerPixel={shown.cssPixelsPerPixel}
            canvasWidthCssPx={shown.sizeCssPx.width}
            canvasHeightCssPx={shown.sizeCssPx.height}
          />
        )}
        {shown?.pickedColumn && (
          <ColumnTooltip
            column={shown.pickedColumn}
            instrument={shown.instrument}
            cssPixelsPerPixel={shown.cssPixelsPerPixel}
            canvasWidthCssPx={shown.sizeCssPx.width}
            canvasHeightCssPx={shown.sizeCssPx.height}
          />
        )}
      </div>
    </div>
  );
}
