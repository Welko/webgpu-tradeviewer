import { reportDataLoaded, reportDataLoadFailed } from '../telemetry';
import type { LoadResult, TradeStats, Viewer } from '../viewer/viewerModule';

/** Binance's public copy of the brief's day, downloaded by the browser straight from Binance. */
export const BinanceSampleUrl = 'https://data.binance.vision/data/spot/daily/trades/BTCUSDT/BTCUSDT-trades-2019-12-01.zip';

type Bytes = Uint8Array<ArrayBuffer>;

/** Bytes read so far, of `total` if known. */
export type OnProgress = (loaded: number, total: number | null) => void;

/**
 * Loads a trades file the user picked or dropped: CSV, gzipped CSV or a zip
 * holding one. What the file holds decides how it's read, not what it's called.
 * Its warnings, if it loads; why not, as an Error, if it doesn't.
 */
export async function loadFile(viewer: Viewer, file: File, onProgress: OnProgress): Promise<string[]> {
  const start = new Uint8Array(await file.slice(0, 4).arrayBuffer());
  if (startsWith(start, [0x50, 0x4b])) {
    const zip = await readAll(file.stream().pipeThrough(counting(file.size, onProgress)));
    return parse(viewer, unzip(zip, 'file'), file.name, 'file');
  }
  let stream = file.stream().pipeThrough(counting(file.size, onProgress));
  if (startsWith(start, [0x1f, 0x8b])) {
    stream = stream.pipeThrough(new DecompressionStream('gzip'));
  }
  return parse(viewer, stream, file.name, 'file');
}

function startsWith(bytes: Uint8Array, start: number[]): boolean {
  return start.every((byte, i) => bytes[i] === byte);
}

/** Downloads and loads the Binance sample. Its warnings, as loadFile() gives them. */
export async function loadBinanceSample(viewer: Viewer, onProgress: OnProgress): Promise<string[]> {
  const response = await fetch(BinanceSampleUrl).catch(() => {
    reportDataLoadFailed('binance', 'fetch');
    throw new Error("Can't reach Binance's server. Check your connection, or open a file instead.");
  });
  if (!response.ok || !response.body) {
    reportDataLoadFailed('binance', 'fetch');
    throw new Error(`Binance's server answered ${response.status} ${response.statusText}`);
  }
  const total = Number(response.headers.get('content-length')) || null;
  const zip = await readAll(response.body.pipeThrough(counting(total, onProgress)));
  return parse(viewer, unzip(zip, 'binance'), BinanceSampleUrl.slice(BinanceSampleUrl.lastIndexOf('/') + 1), 'binance');
}

/**
 * Feeds the CSV text in `stream` to the viewer's parser, chunk by chunk.
 * `fileName` names the assets when the file doesn't, as Binance's don't.
 * `source` says where it came from, for telemetry. The file's warnings; why
 * it failed, as an Error.
 */
async function parse(viewer: Viewer, stream: ReadableStream<Bytes>, fileName: string, source: string): Promise<string[]> {
  const start = performance.now();
  let bytes = 0;
  viewer.beginLoad(fileName);
  const reader = stream.getReader();
  for (;;) {
    // Decompressing fails here, with the browser's own vague message.
    const { done, value } = await reader.read().catch(() => {
      reportDataLoadFailed(source, 'decompress');
      throw new Error('The file is damaged or cut off');
    });
    if (done) {
      break;
    }
    bytes += value.byteLength;
    const { error } = viewer.appendChunk(value) as LoadResult;
    if (error) {
      reportDataLoadFailed(source, 'parse');
      await reader.cancel();
      throw new Error(error);
    }
  }
  // Fails too if the GPU can't hold the trades, which looks the same from here.
  const { error, warnings } = viewer.finishLoad() as LoadResult;
  if (error) {
    reportDataLoadFailed(source, 'parse');
    throw new Error(error);
  }
  const stats = viewer.tradeStats() as TradeStats;
  reportDataLoaded(source, bytes, performance.now() - start, stats.count, stats.lastMs - stats.firstMs);
  return warnings;
}

/** Passes bytes through unchanged, reporting how many went by. */
function counting(total: number | null, onProgress: OnProgress): TransformStream<Bytes, Bytes> {
  let loaded = 0;
  return new TransformStream({
    transform(chunk, controller) {
      loaded += chunk.byteLength;
      onProgress(loaded, total);
      controller.enqueue(chunk);
    },
  });
}

/** unzipFirstFile(), telling telemetry when it fails. */
function unzip(zip: Bytes, source: string): ReadableStream<Bytes> {
  try {
    return unzipFirstFile(zip);
  } catch (error) {
    reportDataLoadFailed(source, 'decompress');
    throw error;
  }
}

async function readAll(stream: ReadableStream<Bytes>): Promise<Bytes> {
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/**
 * The first file in a zip archive, decompressed. Zip keeps a directory of
 * its files at the end, which gives the first file's size and position.
 */
function unzipFirstFile(zip: Bytes): ReadableStream<Bytes> {
  const damaged = 'The zip file is damaged or cut off';
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength);
  let end = zip.byteLength - 22;
  while (end >= 0 && view.getUint32(end, true) !== 0x06054b50) {
    end -= 1;
  }
  if (end < 0) {
    throw new Error(damaged);
  }
  if (view.getUint16(end + 10, true) === 0) {
    throw new Error('The zip file holds no files');
  }
  const entry = view.getUint32(end + 16, true);
  if (entry + 46 > zip.byteLength) {
    throw new Error(damaged);
  }
  const flags = view.getUint16(entry + 8, true);
  const method = view.getUint16(entry + 10, true);
  const compressedSize = view.getUint32(entry + 20, true);
  const header = view.getUint32(entry + 42, true);
  if (header + 30 > zip.byteLength) {
    throw new Error(damaged);
  }
  const start = header + 30 + view.getUint16(header + 26, true) + view.getUint16(header + 28, true);
  if (start + compressedSize > zip.byteLength) {
    throw new Error(damaged);
  }
  if (flags & 1) {
    throw new Error('The zip file is password-protected');
  }

  const data = new Blob([zip.subarray(start, start + compressedSize)]).stream();
  switch (method) {
    case 0:
      return data;
    case 8:
      return data.pipeThrough(new DecompressionStream('deflate-raw'));
    default:
      throw new Error(`The zip file uses compression method ${method}, which can't be read here`);
  }
}
