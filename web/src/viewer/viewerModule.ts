import createViewerModule, { type MainModule, type Viewer } from '../wasm/Viewer.js';

export type { Viewer };

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
