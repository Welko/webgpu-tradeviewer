import { useEffect } from 'react';
import { AxisMarginsCssPx } from './AxisLabels';
import type { LiveViewer } from './useViewer';

/**
 * Keeps the canvas at its CSS size in physical pixels, and the viewer's size,
 * pixel ratio and plot insets with it, then `refresh`es.
 */
export function useCanvasSize(live: LiveViewer | null, refresh: (() => void) | null) {
  useEffect(() => {
    if (!live || !refresh) {
      return;
    }
    const { viewer, canvas, signal } = live;
    const observer = new ResizeObserver(([entry]) => {
      if (signal.aborted) {
        return;
      }
      // Safari doesn't report device pixels.
      const box = (entry.devicePixelContentBoxSize as readonly ResizeObserverSize[] | undefined)?.[0];
      const widthPx = box ? box.inlineSize : Math.round(entry.contentRect.width * devicePixelRatio);
      const heightPx = box ? box.blockSize : Math.round(entry.contentRect.height * devicePixelRatio);
      // Assigning the size clears the canvas, so only do it on a real change.
      if (canvas.width !== widthPx || canvas.height !== heightPx) {
        canvas.width = widthPx;
        canvas.height = heightPx;
      }
      // Also changes when the page is zoomed or moved to another screen, which resizes the canvas in pixels.
      viewer.setPixelRatio(devicePixelRatio);
      const pixelsPerCssPixel = widthPx / Math.max(1, entry.contentRect.width);
      viewer.setPlotInsets(
        0,
        0,
        Math.round(AxisMarginsCssPx.right * pixelsPerCssPixel),
        Math.round(AxisMarginsCssPx.bottom * pixelsPerCssPixel),
      );
      viewer.resize(widthPx, heightPx);
      refresh();
    });
    observer.observe(canvas);
    return () => {
      observer.disconnect();
    };
  }, [live, refresh]);
}
