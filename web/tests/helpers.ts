import { expect, type Page } from '@playwright/test';
import { deflateRawSync } from 'node:zlib';

/** Collects console errors and uncaught exceptions from the page. */
export function collectErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on('console', (message) => {
    if (message.type() === 'error') {
      errors.push(message.text());
    }
  });
  page.on('pageerror', (error) => errors.push(error.message));
  return errors;
}

/** Waits until the viewer reports that WebGPU is ready, failing with its error otherwise. */
export async function waitUntilReady(page: Page) {
  const status = page.locator('[data-status]');
  await expect(status).not.toHaveAttribute('data-status', 'loading', { timeout: 30_000 });
  await expect(status, await status.textContent() ?? '').toHaveAttribute('data-status', 'ready');
}

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let k = 0; k < 8; k++) {
      crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/** A zip archive holding one deflated file, as Binance serves its trades. */
export function zip(name: string, text: string): Buffer {
  const data = Buffer.from(text);
  const compressed = deflateRawSync(data);
  const fileName = Buffer.from(name);
  const common = (header: Buffer, offset: number) => {
    header.writeUInt16LE(20, offset); // version needed
    header.writeUInt16LE(0, offset + 2); // flags
    header.writeUInt16LE(8, offset + 4); // deflate
    header.writeUInt32LE(0, offset + 6); // time and date
    header.writeUInt32LE(crc32(data), offset + 10);
    header.writeUInt32LE(compressed.length, offset + 14);
    header.writeUInt32LE(data.length, offset + 18);
    header.writeUInt16LE(fileName.length, offset + 22);
  };
  const local = Buffer.alloc(30);
  local.writeUInt32LE(0x04034b50, 0);
  common(local, 4);
  const central = Buffer.alloc(46);
  central.writeUInt32LE(0x02014b50, 0);
  central.writeUInt16LE(20, 4); // version made by
  common(central, 6);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(1, 8);
  end.writeUInt16LE(1, 10);
  end.writeUInt32LE(central.length + fileName.length, 12);
  end.writeUInt32LE(local.length + fileName.length + compressed.length, 16);
  return Buffer.concat([local, fileName, compressed, central, fileName, end]);
}
