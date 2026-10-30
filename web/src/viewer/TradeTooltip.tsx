import { utcMicroseconds } from '../utc';
import type { PickedTrade } from './viewerModule';

// From the marker's centre to the tooltip's nearest corner, in CSS pixels.
const Offset = 14;
// Room the tooltip needs, roughly, before it flips to the other side of the marker.
const FlipWidth = 260;
const FlipHeight = 80;

interface TradeTooltipProps {
  trade: PickedTrade;
  /** CSS pixels per physical pixel of the canvas. */
  scale: number;
  /** The canvas's size in CSS pixels. */
  width: number;
  height: number;
}

/** The picked trade's time, price, amount and side, beside its marker. */
export function TradeTooltip({ trade, scale, width, height }: TradeTooltipProps) {
  const x = trade.x * scale;
  const y = trade.y * scale;
  // Below and to the right of the marker, unless that would leave the canvas.
  const across = x + Offset + FlipWidth > width ? `calc(-100% - ${Offset}px)` : `${Offset}px`;
  const down = y + Offset + FlipHeight > height ? `calc(-100% - ${Offset}px)` : `${Offset}px`;
  const number = (value: number) => value.toLocaleString('en-US', { maximumFractionDigits: 8 });
  return (
    <div className="trade-tooltip" data-testid="trade-tooltip" style={{ left: x, top: y, transform: `translate(${across}, ${down})` }}>
      <div className={`trade-side ${trade.side}`} data-testid="trade-side">
        {trade.side === 'buy' ? 'Buy' : 'Sell'}
      </div>
      <div data-testid="trade-time">{utcMicroseconds(trade.timeUs)}</div>
      <div>
        <span data-testid="trade-price">{number(trade.price)}</span> USDT ·{' '}
        <span data-testid="trade-amount">{number(trade.amount)}</span> BTC
      </div>
    </div>
  );
}
