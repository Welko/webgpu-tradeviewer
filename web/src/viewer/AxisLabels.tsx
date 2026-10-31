import type { Axes } from './viewerModule';

/** Room for the labels beside and below the plot, in CSS pixels. */
export const AxisMarginsCssPx = { right: 64, bottom: 22 };

// Time labels closer than this to an end of the axis align with that end, so they aren't cut off.
const EdgeDistanceCssPx = 40;

/** How to place a time label `xCssPx` along an axis `widthCssPx` long. */
function alignAt(xCssPx: number, widthCssPx: number): string {
  if (xCssPx < EdgeDistanceCssPx) {
    return 'translateX(0)';
  }
  return xCssPx > widthCssPx - EdgeDistanceCssPx ? 'translateX(-100%)' : 'translateX(-50%)';
}

interface AxisLabelsProps {
  axes: Axes;
  /** The canvas's size in CSS pixels, margins included. */
  canvasWidthCssPx: number;
  canvasHeightCssPx: number;
}

/**
 * The axes' labels, in the margins the plot leaves free. The viewer draws the
 * matching grid lines, from the same ticks.
 */
export function AxisLabels({ axes, canvasWidthCssPx, canvasHeightCssPx }: AxisLabelsProps) {
  const plotWidthCssPx = Math.max(0, canvasWidthCssPx - AxisMarginsCssPx.right);
  const plotHeightCssPx = Math.max(0, canvasHeightCssPx - AxisMarginsCssPx.bottom);
  return (
    <div className="axis-labels" aria-hidden="true">
      <div className="plot-frame" style={{ width: plotWidthCssPx, height: plotHeightCssPx }} />
      {axes.times.map((tick) => (
        <span
          key={`time ${tick.label}`}
          className="time-label"
          data-testid="time-label"
          style={{
            left: tick.fraction * plotWidthCssPx,
            top: plotHeightCssPx + 4,
            transform: alignAt(tick.fraction * plotWidthCssPx, plotWidthCssPx),
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
          style={{ left: plotWidthCssPx + 6, top: (1 - tick.fraction) * plotHeightCssPx }}
        >
          {tick.label}
        </span>
      ))}
    </div>
  );
}
