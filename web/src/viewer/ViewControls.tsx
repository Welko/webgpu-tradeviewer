import { utc } from '../utc';
import { useState } from 'react';
import { Legend } from './Legend';
import type { Legend as LegendData, Summaries, TimeRange, TradeStats, Viewer } from './viewerModule';

interface ViewControlsProps {
  viewer: Viewer;
  view: TimeRange;
  /** The trades in `view`. */
  viewStats: TradeStats;
  /** What the markers mean at the chosen size scale. */
  legend: LegendData;
  /** Set while the view is crowded enough to show column summaries instead of trades. */
  summaries: Summaries | null;
  /** Call after changing the viewer, to draw it again. */
  onChange: () => void;
}

/**
 * The time range on screen and what it holds, buttons to jump to the usual
 * ranges, the markers' size scale, whether crowded views are summarized, and
 * the legend.
 */
export function ViewControls({ viewer, view, viewStats, legend, summaries, onChange }: ViewControlsProps) {
  const [everyTrade, setEveryTrade] = useState(false);
  const act = (action: () => void) => () => {
    action();
    onChange();
  };

  return (
    <div className="view-controls">
      <span data-testid="view-range">
        {utc(view.startMs)} – {utc(view.endMs)}
      </span>
      <span title="Trades from the start time up to, but not including, the end time">
        <span data-testid="view-count">{viewStats.count.toLocaleString('en-US')}</span> trades,{' '}
        <span data-testid="view-amount">{viewStats.totalAmount.toLocaleString('en-US', { maximumFractionDigits: 8 })}</span> BTC
      </span>
      <button type="button" className="button" onClick={act(() => viewer.resetView())}>
        12:00–13:00
      </button>
      <button type="button" className="button" onClick={act(() => viewer.showDay())}>
        Whole day
      </button>
      <label title="How marker size shows a trade's amount">
        Size
        <select defaultValue="area" onChange={(event) => act(() => viewer.setMarkerScale(event.target.value))()}>
          <option value="area">Area ∝ amount</option>
          <option value="log">Log of amount</option>
        </select>
      </label>
      <label title="Draw every trade even when the view is crowded. Slow when zoomed out.">
        <input
          type="checkbox"
          checked={everyTrade}
          onChange={(event) => {
            const checked = event.target.checked;
            setEveryTrade(checked);
            act(() => viewer.setSummariesAllowed(!checked))();
          }}
        />
        Every trade
      </label>
      {summaries && (
        <span
          className="summaries"
          data-testid="summaries"
          title="Too many trades to draw each one: each bar sums up one pixel column. Zoom in, or tick Every trade, to see every trade."
        >
          Summarized: {summaries.withTrades.toLocaleString('en-US')} bars, ~
          {Math.round(viewStats.count / Math.max(1, summaries.withTrades)).toLocaleString('en-US')} trades each
        </span>
      )}
      <Legend legend={legend} summarized={summaries !== null} />
    </div>
  );
}
