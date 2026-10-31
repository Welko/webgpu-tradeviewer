import { useId } from 'react';
import type { Color, Legend as LegendData, LegendSize } from './viewerModule';

const SwatchRadius = 5;

function toCss(color: Color): string {
  const channel = (value: number) => Math.round(value * 255);
  return `rgb(${channel(color.red)} ${channel(color.green)} ${channel(color.blue)})`;
}

function formatAmount(size: LegendSize): string {
  const amount = size.amount.toLocaleString('en-US', { maximumSignificantDigits: 3 });
  return size.bound === 'atMost' ? `≤ ${amount}` : size.bound === 'atLeast' ? `≥ ${amount}` : amount;
}

/**
 * A marker as the viewer draws it: a disc of `radius` CSS pixels in `color`
 * (the text color if none), at the markers' opacity.
 */
function Disc({ radius, color, opacity }: { radius: number; color?: Color; opacity: number }) {
  const half = Math.ceil(radius) + 1;
  return (
    <svg width={2 * half} height={2 * half} viewBox={`${-half} ${-half} ${2 * half} ${2 * half}`} aria-hidden="true">
      <circle r={radius} fill={color ? toCss(color) : 'currentColor'} fillOpacity={opacity} />
    </svg>
  );
}

/**
 * What the markers mean: color by side, and size by amount at the chosen
 * scale. The viewer supplies its own colors and sizes, so this matches what
 * it draws.
 */
export function Legend({ legend, summarized }: { legend: LegendData; summarized: boolean }) {
  const gradient = `legend-gradient-${useId().replace(/:/g, '')}`;
  return (
    <div className="legend" data-testid="legend">
      <span className="legend-entry">
        <Disc radius={SwatchRadius} color={legend.buy} opacity={legend.opacity} />
        Buy
      </span>
      <span className="legend-entry">
        <Disc radius={SwatchRadius} color={legend.sell} opacity={legend.opacity} />
        Sell
      </span>
      {summarized ? (
        <span
          className="legend-entry"
          data-testid="legend-bars"
          title="Each bar spans the prices traded in one pixel column. Its color is the buys' share of the amount; the fainter, the less was traded."
        >
          <svg width={48} height={10} aria-hidden="true">
            <defs>
              <linearGradient id={gradient}>
                <stop offset="0" stopColor={toCss(legend.sell)} />
                <stop offset="1" stopColor={toCss(legend.buy)} />
              </linearGradient>
            </defs>
            <rect width={48} height={10} fill={`url(#${gradient})`} />
          </svg>
          Bars: price range, sells → buys, fainter = less traded
        </span>
      ) : (
        <span className="legend-sizes" title="Marker size by traded amount; sizes stop growing past the ends">
          {legend.sizes.map((size) => (
            <span key={`${size.bound} ${size.amount}`} className="legend-entry" data-testid="legend-size">
              {/* Sizes are for either side, so they take the text's gray. */}
              <Disc radius={size.radius} opacity={legend.opacity} />
              {formatAmount(size)}
            </span>
          ))}
          BTC
        </span>
      )}
    </div>
  );
}
