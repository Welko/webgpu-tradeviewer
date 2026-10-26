import { useEffect, useId, useRef } from 'react';
import { createViewer, type PowerPreference, type Viewer } from './viewerModule';

export type ViewerStatus =
  | { kind: 'loading' }
  | { kind: 'ready'; adapter: string }
  | { kind: 'error'; message: string };

interface ViewerCanvasProps {
  /** Changing it recreates the viewer on a newly requested adapter. */
  powerPreference: PowerPreference;
  onStatus?: (status: ViewerStatus) => void;
}

/**
 * Hosts the C++ viewer. React owns the frame loop and the canvas size;
 * the viewer only reacts to resize/update/render calls.
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
    let observer: ResizeObserver | null = null;

    onStatusRef.current?.({ kind: 'loading' });

    createViewer(`#${canvasId}`, powerPreference)
      .then((created) => {
        if (disposed) {
          created.delete();
          return;
        }
        viewer = created;

        observer = new ResizeObserver(([entry]) => {
          const box = entry.devicePixelContentBoxSize?.[0];
          const width = box ? box.inlineSize : Math.round(entry.contentRect.width * devicePixelRatio);
          const height = box ? box.blockSize : Math.round(entry.contentRect.height * devicePixelRatio);
          canvas.width = width;
          canvas.height = height;
          created.resize(width, height);
        });
        observer.observe(canvas);

        const tick = (now: number) => {
          created.update(now);
          created.render();
          frame = requestAnimationFrame(tick);
        };
        frame = requestAnimationFrame(tick);

        onStatusRef.current?.({ kind: 'ready', adapter: created.describe() });
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
