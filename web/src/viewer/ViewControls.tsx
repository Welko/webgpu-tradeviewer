import { utc } from '../utc';
import type { TimeRange, Viewer, Visible } from './viewerModule';

interface ViewControlsProps {
  viewer: Viewer;
  view: TimeRange;
  visible: Visible;
  /** Call after changing the viewer, to draw it again. */
  onChange: () => void;
}

/** The time range on screen and what it holds, and buttons to jump to the usual ranges. */
export function ViewControls({ viewer, view, visible, onChange }: ViewControlsProps) {
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
        <span data-testid="visible-count">{visible.count.toLocaleString('en-US')}</span> trades,{' '}
        <span data-testid="visible-amount">{visible.amount.toLocaleString('en-US', { maximumFractionDigits: 8 })}</span> BTC
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
    </div>
  );
}
