import { useEffect } from 'react';
import type { LiveViewer } from './useViewer';

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

/**
 * Forwards the canvas's input to the viewer, then `refresh`es: the wheel
 * zooms around the pointer, dragging pans, and the pointer picks the trade,
 * or column summary, it shows details of.
 */
export function useCanvasInput(live: LiveViewer | null, refresh: (() => void) | null) {
  useEffect(() => {
    if (!live || !refresh) {
      return;
    }
    const { viewer, canvas } = live;
    const removeInput = new AbortController();
    // Removed as soon as the viewer is gone, before React gets to clean up.
    const listen = { signal: AbortSignal.any([live.signal, removeInput.signal]) };
    // CSS pixels to the canvas's physical pixels, which the viewer works in.
    const toPx = (cssPx: number) => (canvas.clientWidth > 0 ? (cssPx * canvas.width) / canvas.clientWidth : cssPx);

    canvas.addEventListener(
      'wheel',
      (event) => {
        event.preventDefault();
        viewer.zoomAt(toPx(event.offsetX), Math.exp(wheelCssPx(event, canvas.clientHeight) * ZoomPerWheelCssPx));
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
          viewer.panBy(toPx(event.clientX - dragXCssPx));
          dragXCssPx = event.clientX;
        }
        viewer.setPointer(toPx(event.offsetX), toPx(event.offsetY));
        refresh();
      },
      listen,
    );
    canvas.addEventListener(
      'pointerleave',
      () => {
        viewer.clearPointer();
        refresh();
      },
      listen,
    );
    const endDrag = () => {
      dragXCssPx = null;
    };
    canvas.addEventListener('pointerup', endDrag, listen);
    canvas.addEventListener('pointercancel', endDrag, listen);

    return () => {
      removeInput.abort();
    };
  }, [live, refresh]);
}
