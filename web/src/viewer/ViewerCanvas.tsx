import { useEffect, useId, useRef } from 'react';
import type { GpuInfo } from './gpuInfo';
import { createViewer, type PowerPreference, type Viewer } from './viewerModule';

export type ViewerStatus =
  | { kind: 'loading' }
  | { kind: 'ready'; gpu: GpuInfo }
  | { kind: 'error'; message: string };

interface ViewerCanvasProps {
  /** Changing it recreates the viewer on a newly requested adapter. */
  powerPreference: PowerPreference;
  onStatus?: (status: ViewerStatus) => void;
}

/**
 * Hosts the C++ viewer. React owns the frame loop and the canvas size.
 * Frames are requested only while the viewer needs them, so an idle page
 * schedules nothing.
 */
export function ViewerCanvas({ powerPreference, onStatus }: ViewerCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const canvasId = `viewer-${useId().replace(/:/g, '')}`;
  const onStatusRef = useRef(onStatus);
  onStatusRef.current = onStatus;

  useEffect(() => {
    const canvas = canvasRef.current!;
    let disposed = false;
    let viewer: Viewer | null = null;
    let frame = 0;
    let scheduled = false;
    let observer: ResizeObserver | null = null;

    onStatusRef.current?.({ kind: 'loading' });

    createViewer(`#${canvasId}`, powerPreference)
      .then((created) => {
        if (disposed) {
          created.delete();
          return;
        }
        viewer = created;

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

        observer = new ResizeObserver(([entry]) => {
          const box = entry.devicePixelContentBoxSize?.[0];
          const width = box ? box.inlineSize : Math.round(entry.contentRect.width * devicePixelRatio);
          const height = box ? box.blockSize : Math.round(entry.contentRect.height * devicePixelRatio);
          // Assigning the size clears the canvas, so only do it on a real change.
          if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width;
            canvas.height = height;
          }
          created.resize(width, height);
          requestFrame();
        });
        observer.observe(canvas);
        requestFrame();

        onStatusRef.current?.({ kind: 'ready', gpu: created.info() as GpuInfo });
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
      viewer?.delete();
    };
  }, [canvasId, powerPreference]);

  return (
    <div className="viewer">
      <canvas id={canvasId} ref={canvasRef} />
    </div>
  );
}
