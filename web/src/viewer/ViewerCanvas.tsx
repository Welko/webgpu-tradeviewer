import { useId, useRef } from 'react';
import { AxisLabels } from './AxisLabels';
import { ColumnTooltip, TradeTooltip } from './Tooltip';
import { useCanvasInput } from './useCanvasInput';
import { useCanvasSize } from './useCanvasSize';
import { useViewer, type OnStatus } from './useViewer';
import { useViewerSnapshot } from './useViewerSnapshot';
import { ViewControls } from './ViewControls';
import type { PowerPreference } from './viewerModule';

export type { ViewerStatus } from './useViewer';

interface ViewerCanvasProps {
  /** Changing it recreates the viewer on a newly requested adapter. */
  powerPreference: PowerPreference;
  onStatus?: OnStatus;
}

/**
 * Hosts the C++ viewer. React owns the frame loop and the canvas size, and
 * forwards input (see the hooks this uses).
 */
export function ViewerCanvas({ powerPreference, onStatus }: ViewerCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const canvasId = `viewer-${useId().replace(/:/g, '')}`;
  const onStatusRef = useRef(onStatus);
  onStatusRef.current = onStatus;

  const live = useViewer(canvasRef, canvasId, powerPreference, onStatusRef);
  const { snapshot: shown, refresh } = useViewerSnapshot(live, onStatusRef);
  useCanvasInput(live, refresh);
  useCanvasSize(live, refresh);

  return (
    <div className="viewer">
      {shown && (
        <ViewControls
          viewer={shown.viewer}
          view={shown.view}
          viewStats={shown.viewStats}
          instrument={shown.instrument}
          legend={shown.legend}
          summaries={shown.summaries}
          onChange={shown.refresh}
        />
      )}
      <div className="plot">
        <canvas id={canvasId} ref={canvasRef} />
        {shown && <AxisLabels axes={shown.axes} canvasWidthCssPx={shown.sizeCssPx.width} canvasHeightCssPx={shown.sizeCssPx.height} />}
        {shown?.picked && (
          <TradeTooltip
            trade={shown.picked}
            instrument={shown.instrument}
            cssPixelsPerPixel={shown.cssPixelsPerPixel}
            canvasWidthCssPx={shown.sizeCssPx.width}
            canvasHeightCssPx={shown.sizeCssPx.height}
          />
        )}
        {shown?.pickedColumn && (
          <ColumnTooltip
            column={shown.pickedColumn}
            instrument={shown.instrument}
            cssPixelsPerPixel={shown.cssPixelsPerPixel}
            canvasWidthCssPx={shown.sizeCssPx.width}
            canvasHeightCssPx={shown.sizeCssPx.height}
          />
        )}
      </div>
    </div>
  );
}
