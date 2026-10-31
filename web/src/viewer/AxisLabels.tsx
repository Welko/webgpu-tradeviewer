import type { Axes } from './viewerModule';

/** Room for the labels beside and below the plot, in CSS pixels. */
export const AxisMargins = { right: 64, bottom: 22 };

// Time labels closer than this to an end of the axis align with that end, so they aren't cut off.
const EdgeDistance = 40;

/** How to place a time label `x` pixels along an axis `width` pixels long. */
function alignAt(x: number, width: number): string {
  if (x < EdgeDistance) {
    return 'translateX(0)';
  }
  return x > width - EdgeDistance ? 'translateX(-100%)' : 'translateX(-50%)';
}

interface AxisLabelsProps {
  axes: Axes;
  /** The canvas's size in CSS pixels, margins included. */
  canvasWidth: number;
  canvasHeight: number;
}

/**
 * The axes' labels, in the margins the plot leaves free. The viewer draws the
 * matching grid lines, from the same ticks.
 */
export function AxisLabels({ axes, canvasWidth, canvasHeight }: AxisLabelsProps) {
  const plotWidth = Math.max(0, canvasWidth - AxisMargins.right);
  const plotHeight = Math.max(0, canvasHeight - AxisMargins.bottom);
  return (
    <div className="axis-labels" aria-hidden="true">
      <div className="plot-frame" style={{ width: plotWidth, height: plotHeight }} />
      {axes.times.map((tick) => (
        <span
          key={`time ${tick.label}`}
          className="time-label"
          data-testid="time-label"
          style={{
            left: tick.fraction * plotWidth,
            top: plotHeight + 4,
            transform: alignAt(tick.fraction * plotWidth, plotWidth),
          }}
        >
          {tick.label}
        </span>
      ))}
      {axes.prices.map((tick) => (
        <span
          key={`price ${tick.label}`}
          className="price-label"
          data-testid="price-label"
          style={{ left: plotWidth + 6, top: (1 - tick.fraction) * plotHeight }}
        >
          {tick.label}
        </span>
      ))}
    </div>
  );
}
