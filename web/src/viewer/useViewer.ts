import { useEffect, useState, type RefObject } from 'react';
import { reportError, setNotices } from '../notices';
import { attachViewer, reportErrorEvent } from '../telemetry';
import type { GpuInfo } from './gpuInfo';
import { readTheme } from './theme';
import {
  createViewer,
  loadModule,
  onModuleAbort,
  type Instrument,
  type MainModule,
  type PowerPreference,
  type Viewer,
} from './viewerModule';

export type ViewerStatus =
  | { kind: 'loading' }
  /** `refresh()` draws again; call it after changing the viewer, such as loading trades. */
  | { kind: 'ready'; gpu: GpuInfo; viewer: Viewer; instrument: Instrument; refresh: () => void }
  /** Says why through the page's notices. */
  | { kind: 'error' };

export type OnStatus = (status: ViewerStatus) => void;

declare global {
  interface Window {
    /** The viewer on the page, for tests and the console. */
    viewer?: Viewer;
    /** Faults to inject into it, for the browser tests (see cpp/apps/ViewerApp/src/Web/Testing.cpp). */
    viewerTesting?: MainModule['Testing'];
  }
}

/** A viewer that's running, on its canvas. */
export interface LiveViewer {
  viewer: Viewer;
  canvas: HTMLCanvasElement;
  /** Aborted once the viewer is gone, deleted or its module stopped: from then on, every call into it would throw. */
  signal: AbortSignal;
}

/** What the page should say after the wasm module stopped for `reason`, and started over. */
function describeAbort(reason: string): string {
  if (reason.startsWith('Out of memory')) {
    return `This data is too large for the page. ${reason}. The viewer started over with the demo data.`;
  }
  // A bug: the details are for developers.
  return 'Something went wrong, and the viewer started over with the demo data. The details are in the browser’s console.';
}

/**
 * Creates the viewer on the canvas `canvasId`, and a new one whenever
 * `powerPreference` changes or the wasm module stops. Null until it's running.
 */
export function useViewer(
  canvasRef: RefObject<HTMLCanvasElement | null>,
  canvasId: string,
  powerPreference: PowerPreference,
  onStatusRef: RefObject<OnStatus | undefined>,
): LiveViewer | null {
  // Bumped when the wasm module aborts, to start over with a new one.
  const [generation, setGeneration] = useState(0);
  const [live, setLive] = useState<LiveViewer | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }
    const lifetime = new AbortController();
    let disposed = false;
    let dead = false;
    let viewer: Viewer | null = null;
    let detachTelemetry: (() => void) | null = null;
    const stopListening = onModuleAbort((reason) => {
      // Every call into the module would throw from now on.
      dead = true;
      lifetime.abort();
      console.error(`The viewer stopped: ${reason}`);
      setNotices('module', [{ severity: 'error', message: describeAbort(reason) }]);
      reportErrorEvent('crash', reason);
      setGeneration((current) => current + 1);
    });

    onStatusRef.current?.({ kind: 'loading' });
    setNotices('viewer', []);

    const onViewerError = (message: string) => {
      reportError(message);
      reportErrorEvent('viewer', message);
    };

    createViewer(`#${canvasId}`, powerPreference, onViewerError)
      .then((created) => {
        if (disposed) {
          created.delete();
          return;
        }
        viewer = created;
        window.viewer = created;
        detachTelemetry = attachViewer(created);
        // The module the viewer is in, already loaded.
        void loadModule().then((module) => {
          window.viewerTesting = module.Testing;
        });
        created.setTheme(readTheme(canvas.parentElement ?? canvas));
        setLive({ viewer: created, canvas, signal: lifetime.signal });
      })
      .catch((error: unknown) => {
        if (!disposed) {
          setNotices('viewer', [{ severity: 'error', message: error instanceof Error ? error.message : String(error), needsReload: true }]);
          onStatusRef.current?.({ kind: 'error' });
        }
      });

    return () => {
      disposed = true;
      lifetime.abort();
      stopListening();
      detachTelemetry?.();
      setLive(null);
      if (window.viewer === viewer) {
        delete window.viewer;
      }
      if (!dead) {
        viewer?.delete();
      }
    };
  }, [canvasRef, canvasId, powerPreference, onStatusRef, generation]);

  return live;
}
