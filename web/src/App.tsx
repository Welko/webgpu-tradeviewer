import { useState } from 'react';
import { DataPanel } from './data/DataPanel';
import { describeGpu } from './viewer/gpuInfo';
import { ViewerCanvas, type ViewerStatus } from './viewer/ViewerCanvas';
import type { PowerPreference } from './viewer/viewerModule';

function describe(status: ViewerStatus): string {
  switch (status.kind) {
    case 'loading':
      return 'Starting WebGPU…';
    case 'ready':
      return describeGpu(status.gpu);
    case 'error':
      return status.message;
  }
}

export function App() {
  const [status, setStatus] = useState<ViewerStatus>({ kind: 'loading' });
  const [powerPreference, setPowerPreference] = useState<PowerPreference>('default');

  return (
    <div className="app">
      <header className="app-header">
        <h1>BTC/USDT trades</h1>
        <span className={status.kind === 'error' ? 'status error' : 'status'} data-status={status.kind}>
          {describe(status)}
          {status.kind === 'ready' && powerPreference !== 'default' && ` · ${powerPreference} requested`}
        </span>
        <label className="gpu-select" title="A hint to the browser; it may ignore it.">
          GPU
          <select value={powerPreference} onChange={(event) => setPowerPreference(event.target.value as PowerPreference)}>
            <option value="default">Default</option>
            <option value="low-power">Low power (integrated)</option>
            <option value="high-performance">High performance (discrete)</option>
          </select>
        </label>
      </header>
      <main className="app-main">
        <ViewerCanvas powerPreference={powerPreference} onStatus={setStatus} />
        {status.kind === 'ready' && <DataPanel viewer={status.viewer} onTradesChanged={status.refresh} />}
      </main>
    </div>
  );
}
