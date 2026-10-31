import type { ReactNode } from 'react';
import { utc, utcMicroseconds } from '../utc';
import type { PickedColumn, PickedTrade } from './viewerModule';

// From the anchor to the tooltip's nearest corner, in CSS pixels.
const Offset = 14;
// Room a tooltip needs, roughly, before it flips to the other side of its anchor.
const FlipWidth = 260;
const FlipHeight = 90;

interface Placement {
  cssPixelsPerPixel: number;
  /** In CSS pixels. */
  canvasWidth: number;
  canvasHeight: number;
}

function number(value: number): string {
  return value.toLocaleString('en-US', { maximumFractionDigits: 8 });
}

/**
 * A box beside the point (`anchorX`, `anchorY`), in physical pixels, below
 * and to the right unless that leaves the canvas.
 */
function Tooltip({
  anchorX,
  anchorY,
  cssPixelsPerPixel,
  canvasWidth,
  canvasHeight,
  testId,
  children,
}: Placement & { anchorX: number; anchorY: number; testId: string; children: ReactNode }) {
  const left = anchorX * cssPixelsPerPixel;
  const top = anchorY * cssPixelsPerPixel;
  const across = left + Offset + FlipWidth > canvasWidth ? `calc(-100% - ${Offset}px)` : `${Offset}px`;
  const down = top + Offset + FlipHeight > canvasHeight ? `calc(-100% - ${Offset}px)` : `${Offset}px`;
  return (
    <div className="plot-tooltip" data-testid={testId} style={{ left, top, transform: `translate(${across}, ${down})` }}>
      {children}
    </div>
  );
}

/** The picked trade's time, price, amount and side, beside its marker. */
export function TradeTooltip({ trade, ...placement }: Placement & { trade: PickedTrade }) {
  return (
    <Tooltip anchorX={trade.centerX} anchorY={trade.centerY} testId="trade-tooltip" {...placement}>
      <div className={`trade-side ${trade.side}`} data-testid="trade-side">
        {trade.side === 'buy' ? 'Buy' : 'Sell'}
      </div>
      <div data-testid="trade-time">{utcMicroseconds(trade.timeUs)}</div>
      <div>
        <span data-testid="trade-price">{number(trade.price)}</span> USDT ·{' '}
        <span data-testid="trade-amount">{number(trade.amount)}</span> BTC
      </div>
    </Tooltip>
  );
}

/** What the picked column summary holds, beside the top of its bar. */
export function ColumnTooltip({ column, ...placement }: Placement & { column: PickedColumn }) {
  const sells = column.count - column.buyCount;
  return (
    <Tooltip anchorX={column.centerX} anchorY={column.top} testId="column-tooltip" {...placement}>
      <div data-testid="column-time">
        {utc(column.startUs / 1000)} – {utc(column.endUs / 1000)}
      </div>
      <div data-testid="column-count">
        {number(column.count)} trades: {number(column.buyCount)} buys, {number(sells)} sells
      </div>
      <div data-testid="column-price">
        {number(column.priceLow)} – {number(column.priceHigh)} USDT
      </div>
      <div data-testid="column-amounts">
        <span className="trade-side buy">{number(column.buyAmount)}</span> BTC bought ·{' '}
        <span className="trade-side sell">{number(column.sellAmount)}</span> BTC sold
      </div>
    </Tooltip>
  );
}
