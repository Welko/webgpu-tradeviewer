import { utc } from '../utc';
import type { TimeRange, Viewer } from './viewerModule';

interface ViewControlsProps {
  viewer: Viewer;
  view: TimeRange;
  /** Call after changing the viewer, to draw it again. */
  onChange: () => void;
}

/** The time range on screen, and buttons to jump to the usual ones. */
export function ViewControls({ viewer, view, onChange }: ViewControlsProps) {
  const act = (action: () => void) => () => {
    action();
    onChange();
  };

  return (
    <div className="view-controls">
      <span data-testid="view-range">
        {utc(view.startMs)} – {utc(view.endMs)}
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
