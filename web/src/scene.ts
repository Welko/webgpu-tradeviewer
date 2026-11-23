/**
 * Draws one image-test scene, scene.html?scene=<name>, for tests that compare
 * renderings across platforms (tests/scenes.spec.ts). Without a name, lists
 * the scenes. Once drawn, the body's data-status is "done" or "error" and
 * window.sceneResult holds what the tests read.
 */
import { getElement } from './dom';
import { describeGpu, type GpuInfo } from './viewer/gpuInfo';
import { createViewer, loadModule, type SceneInfo, type ScenePatch } from './viewer/viewerModule';

/** What the tests read once the scene is drawn. */
export interface SceneResult {
  name: string;
  description: string;
  widthPx: number;
  heightPx: number;
  /** Its known pixels, for tools/compare-renderings.mjs to check the engine's copy with. */
  patches: ScenePatch[];
  /** The GPU, as the viewer describes it. */
  gpu: string;
  /** The engine's own copy of the frame, as a PNG data URL. */
  engine: string;
  /** What the canvas shows, read back with drawImage, as a PNG data URL. */
  canvas: string;
  /** How the canvas differs from the engine's copy: pixels with any channel off, and the largest difference. */
  canvasDifference: { pixels: number; largest: number };
}

declare global {
  interface Window {
    scenes?: SceneInfo[];
    sceneResult?: SceneResult;
    sceneError?: string;
  }
}

/** RGBA rows as a PNG data URL. Lossless: every pixel is opaque. */
/** A canvas's 2D context, which every browser that runs WebGPU has. */
function get2dContext(canvas: HTMLCanvasElement): CanvasRenderingContext2D;
function get2dContext(canvas: OffscreenCanvas): OffscreenCanvasRenderingContext2D;
function get2dContext(canvas: HTMLCanvasElement | OffscreenCanvas) {
  const context = canvas.getContext('2d');
  if (!context) {
    throw new Error("This browser can't draw on a 2D canvas");
  }
  return context;
}

function toPng(rgba: Uint8ClampedArray<ArrayBuffer>, widthPx: number, heightPx: number): string {
  const canvas = document.createElement('canvas');
  canvas.width = widthPx;
  canvas.height = heightPx;
  get2dContext(canvas).putImageData(new ImageData(rgba, widthPx, heightPx), 0, 0);
  return canvas.toDataURL('image/png');
}

/** The canvas's current frame. Only readable in the task that drew it: afterwards it's presented and gone. */
function readCanvas(canvas: HTMLCanvasElement): Uint8ClampedArray<ArrayBuffer> {
  const copy = new OffscreenCanvas(canvas.width, canvas.height);
  const context = get2dContext(copy);
  context.drawImage(canvas, 0, 0);
  return context.getImageData(0, 0, canvas.width, canvas.height).data;
}

function compare(a: Uint8ClampedArray, b: Uint8ClampedArray): { pixels: number; largest: number } {
  let pixels = 0;
  let largest = 0;
  for (let i = 0; i < a.length; i += 4) {
    let differs = false;
    for (let channel = 0; channel < 4; channel++) {
      const difference = Math.abs(a[i + channel] - b[i + channel]);
      largest = Math.max(largest, difference);
      differs ||= difference > 0;
    }
    pixels += differs ? 1 : 0;
  }
  return { pixels, largest };
}

async function draw(scene: SceneInfo): Promise<SceneResult> {
  const canvas = document.getElementById('scene') as HTMLCanvasElement;
  canvas.width = scene.widthPx;
  canvas.height = scene.heightPx;
  // One canvas pixel per screen pixel, so the page shows the scene unscaled.
  canvas.style.width = `${scene.widthPx / devicePixelRatio}px`;
  canvas.style.height = `${scene.heightPx / devicePixelRatio}px`;

  // Kept, not deleted: deleting it would clear the canvas.
  const viewer = await createViewer('#scene', 'default');
  const patches = viewer.showScene(scene.name) as ScenePatch[];
  const engine = new Promise<Uint8Array>((resolve, reject) => {
    viewer.readPixels((pixels: Uint8Array | null, error: string | null) =>
      pixels ? resolve(pixels) : reject(new Error(error ?? 'No pixels')),
    );
  });
  viewer.render();
  const shown = readCanvas(canvas);
  const copied = new Uint8ClampedArray((await engine).buffer as ArrayBuffer);
  return {
    name: scene.name,
    description: scene.description,
    widthPx: scene.widthPx,
    heightPx: scene.heightPx,
    patches,
    gpu: describeGpu(viewer.info() as GpuInfo),
    engine: toPng(copied, scene.widthPx, scene.heightPx),
    canvas: toPng(shown, scene.widthPx, scene.heightPx),
    canvasDifference: compare(copied, shown),
  };
}

async function main() {
  const module = await loadModule();
  const scenes = module.scenes() as SceneInfo[];
  window.scenes = scenes;
  const list = getElement('scenes');
  for (const scene of scenes) {
    const item = document.createElement('li');
    item.innerHTML = `<a href="?scene=${scene.name}">${scene.name}</a>: ${scene.description}`;
    list.append(item);
  }

  const name = new URLSearchParams(location.search).get('scene');
  if (!name) {
    document.body.dataset.status = 'done';
    return;
  }
  const scene = scenes.find((candidate) => candidate.name === name);
  if (!scene) {
    throw new Error(`No scene called "${name}"`);
  }
  const result = await draw(scene);
  window.sceneResult = result;
  getElement('result').textContent = [
    `${scene.name}: ${scene.description}`,
    `GPU: ${result.gpu}`,
    `Canvas vs the engine's copy: ${result.canvasDifference.pixels} pixels differ, by up to ${result.canvasDifference.largest}`,
  ].join('\n');
  document.body.dataset.status = 'done';
}

main().catch((error: unknown) => {
  window.sceneError = error instanceof Error ? error.message : String(error);
  getElement('result').textContent = window.sceneError;
  document.body.dataset.status = 'error';
});
