import { useEffect, useState, type DragEvent } from 'react';
import type { TradeStats, Viewer } from '../viewer/viewerModule';
import { utc } from '../utc';
import { BinanceSampleUrl, loadBinanceSample, loadFile, type OnProgress } from './loadTrades';

const TardisUrl = 'https://datasets.tardis.dev/v1/binance/trades/2019/12/01/BTCUSDT.csv.gz';
const Synthetic = 'Synthetic demo data: made-up trades, not real ones';

type LoadState =
  | { kind: 'idle' }
  | { kind: 'loading'; what: string; loaded: number; total: number | null }
  | { kind: 'error'; message: string };

function megabytes(bytes: number): string {
  return `${(bytes / 1e6).toFixed(1)} MB`;
}

/** What's loaded, ways to load real data, and why the site doesn't host it. */
export function DataPanel({ viewer, onTradesChanged }: { viewer: Viewer; onTradesChanged: () => void }) {
  const [source, setSource] = useState(Synthetic);
  const [stats, setStats] = useState<TradeStats>(() => viewer.tradeStats());
  const [load, setLoad] = useState<LoadState>({ kind: 'idle' });

  // A new viewer, for another GPU, starts again from synthetic data.
  useEffect(() => {
    setSource(Synthetic);
    setStats(viewer.tradeStats());
    setLoad({ kind: 'idle' });
  }, [viewer]);

  const run = async (what: string, loading: (onProgress: OnProgress) => Promise<void>) => {
    setLoad({ kind: 'loading', what, loaded: 0, total: null });
    try {
      await loading((loaded, total) => setLoad({ kind: 'loading', what, loaded, total }));
      setSource(what);
      setStats(viewer.tradeStats());
      onTradesChanged();
      setLoad({ kind: 'idle' });
    } catch (error) {
      setLoad({ kind: 'error', message: `${what}: ${error instanceof Error ? error.message : String(error)}` });
    }
  };
  const open = (file: File | undefined) => {
    if (file) {
      void run(file.name, (onProgress) => loadFile(viewer, file, onProgress));
    }
  };
  const drop = (event: DragEvent) => {
    event.preventDefault();
    open(event.dataTransfer.files[0]);
  };
  const loading = load.kind === 'loading';

  return (
    <aside className="data-panel">
      <h2>Data</h2>
      <p className="source" data-testid="source">
        {source}
      </p>
      <dl>
        <dt>Trades</dt>
        <dd data-testid="trade-count">{stats.count.toLocaleString('en-US')}</dd>
        <dt>From</dt>
        <dd>{utc(stats.firstMs)}</dd>
        <dt>To</dt>
        <dd>{utc(stats.lastMs)}</dd>
        <dt>Traded</dt>
        <dd>{stats.totalAmount.toLocaleString('en-US', { maximumFractionDigits: 6 })} BTC</dd>
      </dl>

      {load.kind === 'loading' && (
        <div className="progress">
          <progress value={load.loaded} max={load.total ?? undefined} />
          <span>
            {load.what}: {megabytes(load.loaded)}
            {load.total !== null && ` of ${megabytes(load.total)}`}
          </span>
        </div>
      )}
      {load.kind === 'error' && (
        <p className="error" role="alert">
          {load.message}
        </p>
      )}

      <div className="drop-zone" onDragOver={(event) => event.preventDefault()} onDrop={drop}>
        <label className={loading ? 'button disabled' : 'button'}>
          Open file…
          <input
            type="file"
            accept=".csv,.gz,.zip"
            disabled={loading}
            hidden
            onChange={(event) => {
              open(event.target.files?.[0]);
              event.target.value = '';
            }}
          />
        </label>
        <span>or drop a trades file here</span>
      </div>
      <button
        type="button"
        className="button"
        disabled={loading}
        onClick={() => void run('Binance sample, 2019-12-01', (onProgress) => loadBinanceSample(viewer, onProgress))}
      >
        Load Binance sample (7.7 MB)
      </button>

      <details>
        <summary>Using real data</summary>
        <ol>
          <li>
            Download the brief's trades file from Tardis: <a href={TardisUrl}>BTCUSDT.csv.gz</a> (6.7 MB, Binance
            BTC/USDT, 1 December 2019). Other days and symbols work too.
          </li>
          <li>Drop it here or open it. It's read in your browser and never uploaded.</li>
        </ol>
        <p>
          <strong>Why it isn't hosted here:</strong> Tardis's{' '}
          <a href="https://docs.tardis.dev/legal/terms-of-service">terms of service</a> don't allow making their data
          available to others (clause 9.2), so this site can't serve it.
        </p>
        <p>
          <strong>The Binance sample</strong> is the same day from{' '}
          <a href="https://data.binance.vision">Binance's public data</a>, which your browser downloads straight from{' '}
          <a href={BinanceSampleUrl}>data.binance.vision</a>. It's provided by Binance under{' '}
          <a href="https://github.com/binance/binance-public-data/blob/master/TERMS_AND_CONDITIONS.md">
            non-commercial terms
          </a>
          .
        </p>
      </details>
    </aside>
  );
}
