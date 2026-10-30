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
 * (the text colour if none), at the markers' opacity.
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
 * What the markers mean: colour by side, and size by amount at the chosen
 * scale. The viewer supplies its own colours and sizes, so this matches what
 * it draws.
 */
export function Legend({ legend }: { legend: LegendData }) {
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
      <span className="legend-sizes" title="Marker size by traded amount; sizes stop growing past the ends">
        {legend.sizes.map((size) => (
          <span key={`${size.bound} ${size.amount}`} className="legend-entry" data-testid="legend-size">
            {/* Sizes are for either side, so they take the text's grey. */}
            <Disc radius={size.radius} opacity={legend.opacity} />
            {formatAmount(size)}
          </span>
        ))}
        BTC
      </span>
    </div>
  );
}
