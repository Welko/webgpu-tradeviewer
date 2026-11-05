import type { ReactNode } from 'react';
import { utc, utcMicroseconds } from '../utc';
import { formatAmount, formatPrice } from './format';
import type { Instrument, PickedColumn, PickedTrade } from './viewerModule';

// From the anchor to the tooltip's nearest corner, in CSS pixels.
const OffsetCssPx = 14;
// Room a tooltip needs, roughly, before it flips to the other side of its anchor.
const FlipWidthCssPx = 260;
const FlipHeightCssPx = 90;

interface Placement {
  /** What the trades are in, for units and decimals. */
  instrument: Instrument;
  cssPixelsPerPixel: number;
  canvasWidthCssPx: number;
  canvasHeightCssPx: number;
}

/** A count. */
function number(value: number): string {
  return value.toLocaleString('en-US');
}

/**
 * A box beside the point (`anchorXPx`, `anchorYPx`), below and to the right
 * unless that leaves the canvas.
 */
function Tooltip({
  anchorXPx,
  anchorYPx,
  cssPixelsPerPixel,
  canvasWidthCssPx,
  canvasHeightCssPx,
  testId,
  children,
}: Omit<Placement, 'instrument'> & { anchorXPx: number; anchorYPx: number; testId: string; children: ReactNode }) {
  const leftCssPx = anchorXPx * cssPixelsPerPixel;
  const topCssPx = anchorYPx * cssPixelsPerPixel;
  const across = leftCssPx + OffsetCssPx + FlipWidthCssPx > canvasWidthCssPx ? `calc(-100% - ${OffsetCssPx}px)` : `${OffsetCssPx}px`;
  const down = topCssPx + OffsetCssPx + FlipHeightCssPx > canvasHeightCssPx ? `calc(-100% - ${OffsetCssPx}px)` : `${OffsetCssPx}px`;
  return (
    <div className="plot-tooltip" data-testid={testId} style={{ left: leftCssPx, top: topCssPx, transform: `translate(${across}, ${down})` }}>
      {children}
    </div>
  );
}

/** The picked trade's time, price, amount and side, beside its marker. */
export function TradeTooltip({ trade, instrument, ...placement }: Placement & { trade: PickedTrade }) {
  return (
    <Tooltip anchorXPx={trade.centerXPx} anchorYPx={trade.centerYPx} testId="trade-tooltip" {...placement}>
      <div className={`trade-side ${trade.side}`} data-testid="trade-side">
        {trade.side === 'buy' ? 'Buy' : 'Sell'}
      </div>
      <div data-testid="trade-time">{utcMicroseconds(trade.timeUs)}</div>
      <div>
        <span data-testid="trade-price">{formatPrice(trade.price, instrument)}</span> {instrument.quote} ·{' '}
        <span data-testid="trade-amount">{formatAmount(trade.amount, instrument)}</span> {instrument.base}
      </div>
    </Tooltip>
  );
}

/** What the picked column summary holds, beside the top of its bar. */
export function ColumnTooltip({ column, instrument, ...placement }: Placement & { column: PickedColumn }) {
  const sells = column.count - column.buyCount;
  return (
    <Tooltip anchorXPx={column.centerXPx} anchorYPx={column.topPx} testId="column-tooltip" {...placement}>
      <div data-testid="column-time">
        {utc(column.startUs / 1000)} – {utc(column.endUs / 1000)}
      </div>
      <div data-testid="column-count">
        {number(column.count)} trades: {number(column.buyCount)} buys, {number(sells)} sells
      </div>
      <div data-testid="column-price">
        {formatPrice(column.priceLow, instrument)} – {formatPrice(column.priceHigh, instrument)} {instrument.quote}
      </div>
      <div data-testid="column-amounts">
        <span className="trade-side buy">{formatAmount(column.buyAmount, instrument)}</span> {instrument.base} bought ·{' '}
        <span className="trade-side sell">{formatAmount(column.sellAmount, instrument)}</span> {instrument.base} sold
      </div>
    </Tooltip>
  );
}
