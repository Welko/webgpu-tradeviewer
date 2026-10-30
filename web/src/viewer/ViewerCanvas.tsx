import { useEffect, useId, useRef, useState } from 'react';
import type { GpuInfo } from './gpuInfo';
import { AxisLabels, AxisMargins } from './AxisLabels';
import { readTheme } from './theme';
import { TradeTooltip } from './TradeTooltip';
import { ViewControls } from './ViewControls';
import { createViewer, type Axes, type Legend, type PickedTrade, type PowerPreference, type TimeRange, type TradeSummary, type Viewer } from './viewerModule';

export type ViewerStatus =
  | { kind: 'loading' }
  /** `refresh()` draws again; call it after changing the viewer, such as loading trades. */
  | { kind: 'ready'; gpu: GpuInfo; viewer: Viewer; refresh: () => void }
  | { kind: 'error'; message: string };

// Zooming per pixel of wheel scrolling: a usual 100-pixel step zooms by about 1.22.
const ZoomPerWheelPixel = 0.002;

/** A wheel event's vertical scroll in pixels, whatever unit it came in. */
function wheelPixels(event: WheelEvent, pageHeight: number): number {
  switch (event.deltaMode) {
    case WheelEvent.DOM_DELTA_LINE:
      return event.deltaY * 16;
    case WheelEvent.DOM_DELTA_PAGE:
      return event.deltaY * pageHeight;
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
 * pointer picks the trade it shows details of.
 * Frames are requested only while the viewer needs them, so an idle page
 * schedules nothing.
 */
export function ViewerCanvas({ powerPreference, onStatus }: ViewerCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const canvasId = `viewer-${useId().replace(/:/g, '')}`;
  const onStatusRef = useRef(onStatus);
  onStatusRef.current = onStatus;
  const [shown, setShown] = useState<{
    viewer: Viewer;
    view: TimeRange;
    viewSummary: TradeSummary;
    axes: Axes;
    legend: Legend;
    picked: PickedTrade | null;
    /** The canvas in CSS pixels. */
    size: { width: number; height: number };
    /** CSS pixels per physical pixel. */
    scale: number;
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

    onStatusRef.current?.({ kind: 'loading' });

    createViewer(`#${canvasId}`, powerPreference)
      .then((created) => {
        if (disposed) {
          created.delete();
          return;
        }
        viewer = created;
        created.setTheme(readTheme(canvas.parentElement!));

        const tick = (now: number) => {
          scheduled = false;
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
        let size = { width: canvas.clientWidth, height: canvas.clientHeight };
        const refresh = () => {
          requestFrame();
          setShown({
            viewer: created,
            view: created.view(),
            viewSummary: created.viewSummary(),
            axes: created.axes() as Axes,
            legend: created.legend() as Legend,
            picked: created.pickedTrade() as PickedTrade | null,
            size,
            scale: canvas.width > 0 ? canvas.clientWidth / canvas.width : 1,
            refresh,
          });
        };

        // CSS pixels to the canvas's physical pixels, which the viewer works in.
        const toPixels = (x: number) => (canvas.clientWidth > 0 ? (x * canvas.width) / canvas.clientWidth : x);
        const listen = { signal: removeInput.signal };
        canvas.addEventListener(
          'wheel',
          (event) => {
            event.preventDefault();
            created.zoomAt(toPixels(event.offsetX), Math.exp(wheelPixels(event, canvas.clientHeight) * ZoomPerWheelPixel));
            refresh();
          },
          { ...listen, passive: false },
        );
        let dragX: number | null = null;
        canvas.addEventListener(
          'pointerdown',
          (event) => {
            if (event.button === 0) {
              canvas.setPointerCapture(event.pointerId);
              dragX = event.clientX;
            }
          },
          listen,
        );
        canvas.addEventListener(
          'pointermove',
          (event) => {
            if (dragX !== null) {
              created.panBy(toPixels(event.clientX - dragX));
              dragX = event.clientX;
            }
            created.setPointer(toPixels(event.offsetX), toPixels(event.offsetY));
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
          dragX = null;
        };
        canvas.addEventListener('pointerup', endDrag, listen);
        canvas.addEventListener('pointercancel', endDrag, listen);

        observer = new ResizeObserver(([entry]) => {
          const box = entry.devicePixelContentBoxSize?.[0];
          const width = box ? box.inlineSize : Math.round(entry.contentRect.width * devicePixelRatio);
          const height = box ? box.blockSize : Math.round(entry.contentRect.height * devicePixelRatio);
          // Assigning the size clears the canvas, so only do it on a real change.
          if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width;
            canvas.height = height;
          }
          // Also changes when the page is zoomed or moved to another screen, which resizes the canvas in pixels.
          created.setPixelRatio(devicePixelRatio);
          const scale = width / Math.max(1, entry.contentRect.width);
          created.setPlotInsets(0, 0, Math.round(AxisMargins.right * scale), Math.round(AxisMargins.bottom * scale));
          created.resize(width, height);
          size = { width: entry.contentRect.width, height: entry.contentRect.height };
          refresh();
        });
        observer.observe(canvas);
        refresh();

        onStatusRef.current?.({ kind: 'ready', gpu: created.info() as GpuInfo, viewer: created, refresh });
      })
      .catch((error: unknown) => {
        if (!disposed) {
          onStatusRef.current?.({ kind: 'error', message: error instanceof Error ? error.message : String(error) });
        }
      });

    return () => {
      disposed = true;
      cancelAnimationFrame(frame);
      observer?.disconnect();
      removeInput.abort();
      setShown(null);
      viewer?.delete();
    };
  }, [canvasId, powerPreference]);

  return (
    <div className="viewer">
      {shown && (
        <ViewControls
          viewer={shown.viewer}
          view={shown.view}
          viewSummary={shown.viewSummary}
          legend={shown.legend}
          onChange={shown.refresh}
        />
      )}
      <div className="plot">
        <canvas id={canvasId} ref={canvasRef} />
        {shown && <AxisLabels axes={shown.axes} width={shown.size.width} height={shown.size.height} />}
        {shown?.picked && (
          <TradeTooltip trade={shown.picked} scale={shown.scale} width={shown.size.width} height={shown.size.height} />
        )}
      </div>
    </div>
  );
}
