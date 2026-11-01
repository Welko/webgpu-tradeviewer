#!/usr/bin/env node
/**
 * Compares the image-test scenes as each platform drew them, and with the
 * reference renderings kept in tools/reference-renderings/, and checks each
 * rendering's known pixels.
 *
 * Reads <renderings>/<platform>/<scene>.png and about.json, and
 * <renderings>/scenes.json (the scenes' known pixels), as
 * tools/render-native.sh and web/tests/scenes.spec.ts write them, and writes
 * <renderings>/report/:
 *   rendering-comparison.html  each scene's known pixels on every platform, and
 *                              every pair of platforms side by side, with their difference
 *   comparison.json            the numbers
 *   summary.md                 the numbers as tables, for a CI job summary
 * Exits with 1 if a known pixel is off, a pair differs by more than
 * Thresholds allow, or a rendering is missing.
 *
 * Usage: node tools/compare-renderings.mjs [renderings directory]
 *        node tools/compare-renderings.mjs --update-reference [renderings directory]
 *          copies the native renderings over the reference ones
 */
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync, inflateSync } from 'node:zlib';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const referenceDir = join(root, 'tools', 'reference-renderings');

/**
 * How far two renderings of a scene may differ. GPUs round blends and smooth
 * edges slightly differently, so many pixels may differ by a little, and a
 * few by a lot where an edge falls exactly between pixels; a marker missing or
 * out of place changes hundreds by a lot.
 */
const Thresholds = {
  /** A channel differing by at most this much is rounding, not a difference. */
  roundingLevels: 2,
  /** Pixels differing by more than rounding, as a share of the image. */
  maxBeyondRoundingShare: 0.02,
  /** A channel differing by more than this much is a difference anyone would see. */
  visibleLevels: 32,
  /** Pixels differing visibly, as a share of the image: 259 of 960 × 540, 40 of 400 × 200. */
  maxVisibleShare: 0.0005,
};

/** The order of the report's rows and columns: the user's, then any others. */
const PlatformOrder = ['native', 'firefox', 'chromium', 'reference'];

// PNG, 8 bits per channel, RGB or RGBA, without interlacing: what the
// platforms write. Rows are top first.

const PngSignature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function paeth(left, up, upLeft) {
  const estimate = left + up - upLeft;
  const toLeft = Math.abs(estimate - left);
  const toUp = Math.abs(estimate - up);
  const toUpLeft = Math.abs(estimate - upLeft);
  return toLeft <= toUp && toLeft <= toUpLeft ? left : toUp <= toUpLeft ? up : upLeft;
}

/** { width, height, rgba } from a PNG file's bytes. */
function decodePng(bytes, path) {
  if (!bytes.subarray(0, 8).equals(PngSignature)) {
    throw new Error(`${path}: not a PNG`);
  }
  let width = 0;
  let height = 0;
  let channels = 0;
  const data = [];
  for (let offset = 8; offset < bytes.length; ) {
    const length = bytes.readUInt32BE(offset);
    const type = bytes.toString('latin1', offset + 4, offset + 8);
    const chunk = bytes.subarray(offset + 8, offset + 8 + length);
    if (type === 'IHDR') {
      width = chunk.readUInt32BE(0);
      height = chunk.readUInt32BE(4);
      const [depth, colorType, , , interlace] = chunk.subarray(8, 13);
      channels = colorType === 6 ? 4 : colorType === 2 ? 3 : 0;
      if (depth !== 8 || channels === 0 || interlace !== 0) {
        throw new Error(`${path}: only 8-bit RGB or RGBA without interlacing is supported`);
      }
    } else if (type === 'IDAT') {
      data.push(chunk);
    }
    offset += 12 + length;
  }

  const filtered = inflateSync(Buffer.concat(data));
  const stride = width * channels;
  const rows = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = filtered[y * (stride + 1)];
    const line = filtered.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const i = y * stride + x;
      const left = x >= channels ? rows[i - channels] : 0;
      const up = y > 0 ? rows[i - stride] : 0;
      const upLeft = x >= channels && y > 0 ? rows[i - stride - channels] : 0;
      const predictor = [0, left, up, (left + up) >> 1, paeth(left, up, upLeft)][filter];
      rows[i] = (line[x] + predictor) & 0xff;
    }
  }

  const rgba = Buffer.alloc(width * height * 4, 255);
  for (let pixel = 0; pixel < width * height; pixel++) {
    rows.copy(rgba, pixel * 4, pixel * channels, pixel * channels + Math.min(channels, 4));
  }
  return { width, height, rgba };
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let k = 0; k < 8; k++) {
      crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const header = Buffer.alloc(8);
  header.writeUInt32BE(data.length, 0);
  header.write(type, 4, 'latin1');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([header.subarray(4), data])));
  return Buffer.concat([header, data, crc]);
}

/** A compressed PNG of RGBA rows. */
function encodePng({ width, height, rgba }) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header.set([8, 6, 0, 0, 0], 8);
  const rows = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) {
    rgba.copy(rows, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
  }
  return Buffer.concat([PngSignature, chunk('IHDR', header), chunk('IDAT', deflateSync(rows, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

const dataUrl = (png) => `data:image/png;base64,${png.toString('base64')}`;

// Comparing

/**
 * How `a` and `b` differ, and a picture of it: each pixel's largest channel
 * difference, scaled so the largest in the image is white.
 */
function compare(a, b) {
  if (a.width !== b.width || a.height !== b.height) {
    return { error: `${a.width} × ${a.height} vs ${b.width} × ${b.height} pixels` };
  }
  const pixels = a.width * a.height;
  const differences = new Uint8Array(pixels);
  let largest = 0;
  let total = 0;
  let differing = 0;
  let beyondRounding = 0;
  let visible = 0;
  for (let pixel = 0; pixel < pixels; pixel++) {
    let difference = 0;
    for (let channel = 0; channel < 4; channel++) {
      const i = pixel * 4 + channel;
      difference = Math.max(difference, Math.abs(a.rgba[i] - b.rgba[i]));
      total += channel < 3 ? Math.abs(a.rgba[i] - b.rgba[i]) : 0;
    }
    differences[pixel] = difference;
    largest = Math.max(largest, difference);
    differing += difference > 0 ? 1 : 0;
    beyondRounding += difference > Thresholds.roundingLevels ? 1 : 0;
    visible += difference > Thresholds.visibleLevels ? 1 : 0;
  }

  const picture = Buffer.alloc(pixels * 4, 255);
  for (let pixel = 0; pixel < pixels && largest > 0; pixel++) {
    const level = Math.round((differences[pixel] * 255) / largest);
    picture.fill(level, pixel * 4, pixel * 4 + 3);
  }
  return {
    largest,
    meanDifference: total / (pixels * 3),
    differingShare: differing / pixels,
    beyondRoundingShare: beyondRounding / pixels,
    visibleShare: visible / pixels,
    passed: beyondRounding / pixels <= Thresholds.maxBeyondRoundingShare && visible / pixels <= Thresholds.maxVisibleShare,
    picture: { width: a.width, height: a.height, rgba: picture },
  };
}

/** Each platform's about.json, with `dir`, ordered as the report shows them. */
function findPlatforms(renderingsDir) {
  const platforms = [];
  for (const name of readdirSync(renderingsDir)) {
    const about = join(renderingsDir, name, 'about.json');
    if (existsSync(about)) {
      platforms.push({ ...JSON.parse(readFileSync(about, 'utf8')), dir: join(renderingsDir, name) });
    }
  }
  if (existsSync(join(referenceDir, 'about.json'))) {
    platforms.push({ ...JSON.parse(readFileSync(join(referenceDir, 'about.json'), 'utf8')), platform: 'reference', dir: referenceDir });
  }
  const rank = (platform) => {
    const index = PlatformOrder.indexOf(platform.platform);
    return index < 0 ? PlatformOrder.length : index;
  };
  return platforms.sort((a, b) => rank(a) - rank(b) || a.platform.localeCompare(b.platform));
}

/** The scenes and their known pixels: from this run, else as the references were drawn. */
function readScenes(renderingsDir) {
  for (const dir of [renderingsDir, referenceDir]) {
    if (existsSync(join(dir, 'scenes.json'))) {
      return JSON.parse(readFileSync(join(dir, 'scenes.json'), 'utf8'));
    }
  }
  return [];
}

/** Each of `patches` as `image` has it: { ...patch, actual, passed }. */
function checkPatches(patches, image) {
  return patches.map((patch) => {
    const i = (patch.yPx * image.width + patch.xPx) * 4;
    const actual = [...image.rgba.subarray(i, i + 4)];
    const passed = patch.xPx < image.width && patch.yPx < image.height
      && actual.every((value, channel) => Math.abs(value - patch.rgba[channel]) <= patch.tolerance);
    return { ...patch, actual, passed };
  });
}

// The report

const escape = (text) => String(text).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const percent = (share) => `${(share * 100).toFixed(share > 0 && share < 0.001 ? 3 : 2)}%`;
const swatch = (rgba) => `<span class="swatch" style="background: rgb(${rgba.slice(0, 3).join(' ')})"></span>`;

function describe(result) {
  if (result.error) {
    return `<span class="failed">${escape(result.error)}</span>`;
  }
  if (result.largest === 0) {
    return '<span class="passed">identical</span>';
  }
  return [
    `<span class="${result.passed ? 'passed' : 'failed'}">${percent(result.visibleShare)} visibly</span>`,
    `<span class="${result.passed ? 'passed' : 'failed'}">${percent(result.beyondRoundingShare)} beyond rounding</span>`,
    `${percent(result.differingShare)} at all`,
    `largest ${result.largest}, mean ${result.meanDifference.toFixed(3)}`,
    `<span class="muted">white = ${result.largest}/255</span>`,
  ].join('<br>');
}

/** The scene's known pixels: what each platform drew there. */
function renderChecks(scene, platforms, checks) {
  if (!scene.patches?.length) {
    return '';
  }
  const header = platforms.map((platform) => `<th>${escape(platform.platform)}</th>`).join('');
  const rows = scene.patches.map((patch, index) => {
    const cells = platforms.map((platform) => {
      const check = checks.get(platform.platform)?.[index];
      if (!check) {
        return '<td class="failed">missing</td>';
      }
      return `<td class="${check.passed ? 'passed' : 'failed'}">${swatch(check.actual)}${check.actual.join(', ')}</td>`;
    });
    const tolerance = patch.tolerance ? ` ±${patch.tolerance}` : ' exactly';
    return `<tr><td>${escape(patch.what)}</td><td>${patch.xPx}, ${patch.yPx}</td><td>${swatch(patch.rgba)}${patch.rgba.join(', ')}${tolerance}</td>${cells.join('')}</tr>`;
  });
  return `<h3>Known pixels</h3>
<table class="checks">
<thead><tr><th>pixel</th><th>at</th><th>expected (RGBA)</th>${header}</tr></thead>
<tbody>${rows.join('\n')}</tbody>
</table>`;
}

function renderComparison(platforms, images, results) {
  const header = platforms.map((platform) => `<th>vs ${escape(platform.platform)}</th>`).join('');
  const rows = platforms.map((platform, row) => {
    const image = images.get(platform.platform);
    const first = [
      `<th rowspan="2">${escape(platform.platform)}</th>`,
      `<td rowspan="2">${image ? `<img src="${image.url}" alt="${escape(platform.platform)}">` : '<span class="failed">missing</span>'}</td>`,
    ];
    const second = [];
    platforms.forEach((other, column) => {
      const result = results.get([platform.platform, other.platform].sort().join(' '));
      if (column === row) {
        first.push('<td rowspan="2" class="diagonal"></td>');
      } else if (!result) {
        first.push('<td rowspan="2"></td>');
      } else if (column > row) {
        // Above the diagonal, the picture; below, the same numbers alone.
        first.push(`<td>${result.picture ? `<img src="${result.picture}" alt="difference">` : ''}</td>`);
        second.push(`<td class="numbers">${describe(result)}</td>`);
      } else {
        first.push(`<td rowspan="2" class="numbers">${describe(result)}</td>`);
      }
    });
    return `<tr>${first.join('')}</tr><tr>${second.join('')}</tr>`;
  });
  return `<h3>Renderings and differences</h3>
<table class="grid">
<colgroup><col class="platform">${'<col>'.repeat(platforms.length + 1)}</colgroup>
<thead><tr><th>platform</th><th>rendering</th>${header}</tr></thead>
<tbody>${rows.join('\n')}</tbody>
</table>`;
}

function renderReport(scenes, platforms, results, passed) {
  const about = platforms
    .map((platform) => `<li><b>${escape(platform.platform)}</b>: ${escape(platform.version ?? '')}, ${escape(platform.gpu || 'unknown GPU')}</li>`)
    .join('');
  const sections = scenes.map((scene) => {
    const result = results.get(scene.name);
    return `<section>
<h2>${escape(scene.name)}</h2>
<p class="muted">${escape(scene.description ?? '')}</p>
${renderChecks(scene, platforms, result.checks)}
${renderComparison(platforms, result.images, result.pairs)}
</section>`;
  });
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Rendering comparison</title>
<style>
:root { color-scheme: light dark; --background: #fff; --text: #1c1e24; --muted: #666b78; --line: #d5d8de; --passed: #17803d; --failed: #c62828; --overlay: rgb(0 0 0 / 0.85); }
@media (prefers-color-scheme: dark) { :root { --background: #12141a; --text: #d8dbe2; --muted: #8b90a0; --line: #2c303a; --passed: #4ccf7a; --failed: #ff6b6b; } }
body { margin: 16px; font: 14px/1.4 system-ui, sans-serif; background: var(--background); color: var(--text); }
h3 { margin: 16px 0 8px; font-size: 15px; }
table { border-collapse: collapse; margin-bottom: 16px; }
th, td { border: 1px solid var(--line); padding: 4px 6px; vertical-align: top; text-align: left; }
/* Fits the page: every column but the first shares the width. */
table.grid { width: 100%; table-layout: fixed; }
table.grid col.platform { width: 80px; }
table.grid td { padding: 3px; }
table.grid img { display: block; width: 100%; height: auto; cursor: zoom-in; }
table.checks td { font-variant-numeric: tabular-nums; white-space: nowrap; }
td.diagonal { background: repeating-linear-gradient(45deg, transparent 0 6px, var(--line) 6px 7px); }
td.numbers { font-size: 12px; overflow-wrap: anywhere; }
.swatch { display: inline-block; width: 12px; height: 12px; margin-right: 6px; border: 1px solid var(--line); vertical-align: -2px; }
.muted { color: var(--muted); }
.passed { color: var(--passed); font-weight: 600; }
.failed { color: var(--failed); font-weight: 600; }
#overlay { position: fixed; inset: 0; display: none; overflow: auto; background: var(--overlay); cursor: zoom-out; }
#overlay.shown { display: grid; place-items: center; }
#overlay img { image-rendering: pixelated; }
</style>
</head>
<body>
<h1>Rendering comparison: <span class="${passed ? 'passed' : 'failed'}">${passed ? 'passed' : 'failed'}</span></h1>
<p>Each scene drawn on every platform. Known pixels: colors expected exactly, or within the tolerance for blended ones.
Differences: each pixel's largest channel difference, scaled so the largest in the image is white. A pair fails if more
than ${percent(Thresholds.maxVisibleShare)} of its pixels differ visibly, by more than ${Thresholds.visibleLevels}/255,
or more than ${percent(Thresholds.maxBeyondRoundingShare)} beyond rounding, by more than ${Thresholds.roundingLevels}/255.
Click an image for its full size.</p>
<ul>${about}</ul>
${sections.join('\n')}
<div id="overlay"><img alt=""></div>
<script>
const overlay = document.getElementById('overlay');
for (const image of document.querySelectorAll('table img')) {
  image.addEventListener('click', () => {
    overlay.firstElementChild.src = image.src;
    overlay.classList.add('shown');
  });
}
overlay.addEventListener('click', () => overlay.classList.remove('shown'));
document.addEventListener('keydown', (event) => event.key === 'Escape' && overlay.classList.remove('shown'));
</script>
</body>
</html>
`;
}

function renderSummary(scenes, platforms, results, passed) {
  const lines = [`## Rendering comparison: ${passed ? 'passed' : 'failed'}`, ''];
  lines.push('Known pixels as each platform drew them: right, or how many are off.', '');
  lines.push(`| scene | ${platforms.map((platform) => platform.platform).join(' | ')} |`);
  lines.push(`|---|${platforms.map(() => '---').join('|')}|`);
  for (const scene of scenes) {
    const cells = platforms.map((platform) => {
      const checks = results.get(scene.name).checks.get(platform.platform);
      if (!checks) return 'missing';
      const off = checks.filter((check) => !check.passed).length;
      return off ? `❌ ${off} of ${checks.length} off` : `✅ ${checks.length} right`;
    });
    lines.push(`| ${scene.name} | ${cells.join(' | ')} |`);
  }
  lines.push(
    '',
    `Pixels differing visibly (by more than ${Thresholds.visibleLevels}/255), then beyond rounding (by more than ${Thresholds.roundingLevels}/255).`,
    `A pair fails above ${percent(Thresholds.maxVisibleShare)} or ${percent(Thresholds.maxBeyondRoundingShare)}.`,
    '',
  );
  const pairs = [];
  platforms.forEach((a, i) => platforms.slice(i + 1).forEach((b) => pairs.push([a.platform, b.platform])));
  lines.push(`| scene | ${pairs.map((pair) => pair.join(' vs ')).join(' | ')} |`);
  lines.push(`|---|${pairs.map(() => '---').join('|')}|`);
  for (const scene of scenes) {
    const cells = pairs.map((pair) => {
      const result = results.get(scene.name).pairs.get([...pair].sort().join(' '));
      if (!result) return 'missing';
      if (result.error) return `❌ ${result.error}`;
      return `${result.passed ? '✅' : '❌'} ${percent(result.visibleShare)}, ${percent(result.beyondRoundingShare)}`;
    });
    lines.push(`| ${scene.name} | ${cells.join(' | ')} |`);
  }
  return `${lines.join('\n')}\n`;
}

function updateReference(renderingsDir) {
  const native = join(renderingsDir, 'native');
  const about = JSON.parse(readFileSync(join(native, 'about.json'), 'utf8'));
  mkdirSync(referenceDir, { recursive: true });
  for (const scene of about.scenes) {
    const image = decodePng(readFileSync(join(native, `${scene}.png`)), scene);
    writeFileSync(join(referenceDir, `${scene}.png`), encodePng(image));
  }
  writeFileSync(join(referenceDir, 'about.json'), `${JSON.stringify({ ...about, platform: 'reference' }, null, 2)}\n`);
  writeFileSync(join(referenceDir, 'scenes.json'), readFileSync(join(renderingsDir, 'scenes.json')));
  console.log(`Copied ${about.scenes.length} native renderings to ${referenceDir}`);
}

function main() {
  const args = process.argv.slice(2);
  const update = args.includes('--update-reference');
  const renderingsDir = resolve(args.find((arg) => !arg.startsWith('--')) ?? process.env.TRADEVIEWER_RENDERINGS_DIR ?? join(root, 'renderings'));
  if (update) {
    updateReference(renderingsDir);
    return;
  }

  const platforms = findPlatforms(renderingsDir);
  if (platforms.length < 2) {
    console.error(`Fewer than two platforms' renderings in ${renderingsDir}`);
    process.exit(1);
  }
  // Every scene described, then any other a platform drew.
  const described = readScenes(renderingsDir);
  const names = [...new Set([...described.map((scene) => scene.name), ...platforms.flatMap((platform) => platform.scenes ?? [])])];
  const scenes = names.map((name) => described.find((scene) => scene.name === name) ?? { name, patches: [] });

  let passed = true;
  const results = new Map();
  const numbers = {};
  for (const scene of scenes) {
    const images = new Map();
    const checks = new Map();
    numbers[scene.name] = { knownPixels: {}, pairs: {} };
    for (const platform of platforms) {
      const path = join(platform.dir, `${scene.name}.png`);
      if (!existsSync(path)) {
        console.error(`${platform.platform}: no rendering of ${scene.name}`);
        passed = false;
        continue;
      }
      const image = decodePng(readFileSync(path), path);
      images.set(platform.platform, { ...image, url: dataUrl(encodePng(image)) });
      const checked = checkPatches(scene.patches, image);
      checks.set(platform.platform, checked);
      numbers[scene.name].knownPixels[platform.platform] = checked;
      for (const check of checked.filter((check) => !check.passed)) {
        passed = false;
        console.error(`${scene.name}, ${platform.platform}: ${check.what} at (${check.xPx}, ${check.yPx}) is ${check.actual.join(', ')}, not ${check.rgba.join(', ')} ±${check.tolerance}`);
      }
    }

    const pairs = new Map();
    platforms.forEach((a, i) => {
      for (const b of platforms.slice(i + 1)) {
        const imageA = images.get(a.platform);
        const imageB = images.get(b.platform);
        if (!imageA || !imageB) continue;
        const { picture, ...result } = compare(imageA, imageB);
        const ok = !result.error && result.passed;
        pairs.set([a.platform, b.platform].sort().join(' '), { ...result, passed: ok, picture: picture && result.largest > 0 ? dataUrl(encodePng(picture)) : null });
        numbers[scene.name].pairs[`${a.platform} vs ${b.platform}`] = result;
        if (!ok) {
          passed = false;
          const share = `${percent(result.visibleShare)} of pixels visibly, ${percent(result.beyondRoundingShare)} beyond rounding`;
          console.error(`${scene.name}, ${a.platform} vs ${b.platform}: ${result.error ?? share}`);
        }
      }
    });
    results.set(scene.name, { images, checks, pairs });
  }

  const reportDir = join(renderingsDir, 'report');
  mkdirSync(reportDir, { recursive: true });
  const report = join(reportDir, 'rendering-comparison.html');
  writeFileSync(report, renderReport(scenes, platforms, results, passed));
  writeFileSync(join(reportDir, 'comparison.json'), `${JSON.stringify({ thresholds: Thresholds, platforms: platforms.map(({ dir, ...about }) => about), scenes: numbers }, null, 2)}\n`);
  const summary = renderSummary(scenes, platforms, results, passed);
  writeFileSync(join(reportDir, 'summary.md'), summary);
  console.log(summary);
  console.log(`Report: ${report}`);
  process.exit(passed ? 0 : 1);
}

main();
