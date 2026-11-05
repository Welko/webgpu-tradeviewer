import type { Instrument } from './viewerModule';

/** An amount with the decimals the file gives amounts with, and no more. */
export function formatAmount(value: number, instrument: Instrument): string {
  return value.toLocaleString('en-US', { maximumFractionDigits: instrument.amountDecimals });
}

/** A price with the decimals the file gives prices with, and no more. */
export function formatPrice(value: number, instrument: Instrument): string {
  return value.toLocaleString('en-US', { maximumFractionDigits: instrument.priceDecimals });
}
