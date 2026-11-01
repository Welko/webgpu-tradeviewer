import { expect, test, type Page } from '@playwright/test';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import type { SceneResult } from '../src/scene';

/**
 * Draws every image-test scene (scene.html) and saves the engine's copy of
 * each as <renderings>/<browser>/<scene>.png, for tools/compare-renderings.mjs
 * to compare with the other platforms'. TRADEVIEWER_RENDERINGS_DIR sets
 * <renderings>; by default it's renderings/ at the repository's root.
 */
const renderingsDir =
  process.env.TRADEVIEWER_RENDERINGS_DIR ?? fileURLToPath(new URL('../../renderings', import.meta.url));

/** Waits for the scene page to finish, failing with its error otherwise. */
async function waitUntilDrawn(page: Page) {
  const body = page.locator('body');
  await expect(body).not.toHaveAttribute('data-status', 'loading', { timeout: 30_000 });
  const error = await page.evaluate(() => window.sceneError);
  expect(error, error).toBeUndefined();
}

// A freshly launched browser may have no adapter for a moment (see smoke.spec.ts).
test.beforeAll(async ({ browser }) => {
  const page = await browser.newPage();
  await page.goto(`${test.info().project.use.baseURL}/scene.html`);
  await expect
    .poll(() => page.evaluate(async () => (await navigator.gpu?.requestAdapter()) != null), { timeout: 15_000 })
    .toBe(true);
  await page.close();
});

test('every scene draws, and the canvas shows exactly what the engine drew', async ({ page, browserName, browser }) => {
  await page.goto('/scene.html');
  await waitUntilDrawn(page);
  const scenes = (await page.evaluate(() => window.scenes))!;
  const names = scenes.map((scene) => scene.name);
  expect(names.length).toBeGreaterThan(0);

  const dir = join(renderingsDir, browserName);
  mkdirSync(dir, { recursive: true });
  // The scenes' known pixels, for the comparison to check every platform's renderings with.
  writeFileSync(join(renderingsDir, 'scenes.json'), `${JSON.stringify(scenes, null, 2)}\n`);
  let gpu = '';
  for (const name of names) {
    await test.step(name, async () => {
      await page.goto(`/scene.html?scene=${name}`);
      await waitUntilDrawn(page);
      const result = (await page.evaluate(() => window.sceneResult)) as SceneResult;
      gpu = result.gpu;
      writeFileSync(join(dir, `${name}.png`), Buffer.from(result.engine.split(',')[1], 'base64'));

      expect.soft(result.canvasDifference, `${name}: canvas vs the engine's copy`).toEqual({ pixels: 0, largest: 0 });
    });
  }
  // What the report says about this platform, as tools/render-native.sh writes it natively.
  const about = { platform: browserName, version: `${browserName} ${browser.version()}`, gpu, scenes: names };
  writeFileSync(join(dir, 'about.json'), JSON.stringify(about, null, 2));
});
