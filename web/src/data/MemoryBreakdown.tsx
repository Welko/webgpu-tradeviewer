import type { MemoryStats } from '../viewer/viewerModule';

/** Memory sizes, which come in powers of two. */
export function mebibytes(bytes: number): string {
  return `${(bytes / 1048576).toFixed(1)} MiB`;
}

/** Bytes per trade, with a decimal while it's small; a dash with no trades. */
function perTrade(bytes: number, count: number): string {
  if (count === 0) {
    return '–';
  }
  const each = bytes / count;
  return `${each < 10 ? each.toFixed(1) : each.toFixed(0)} B`;
}

/** What's worked out from the trades when they're loaded, and why. */
const Derived: { key: keyof MemoryStats; name: string; purpose: string }[] = [
  {
    key: 'totalsBytes',
    name: 'Running totals',
    purpose: 'amounts and buy counts summed up to each trade, so any range’s totals take two lookups',
  },
  {
    key: 'priceTreeBytes',
    name: 'Price tree',
    purpose: 'lowest and highest prices of spans of trades, to fit the price axis to any view quickly',
  },
  {
    key: 'summariesBytes',
    name: 'Column summaries',
    purpose: 'one per pixel column, only while zoomed out',
  },
];

/**
 * What the trades take in memory, what's derived from them, and the GPU's
 * share, in a collapsible table. `onOpen` should read them again: the
 * column summaries change with the view.
 */
export function MemoryBreakdown({ memory, count, onOpen }: { memory: MemoryStats; count: number; onOpen: () => void }) {
  const derived = Derived.reduce((sum, { key }) => sum + memory[key], 0);
  const total = memory.tradesBytes + derived;
  return (
    <details className="memory" data-testid="memory" onToggle={(event) => event.currentTarget.open && onOpen()}>
      <summary>
        Memory: {mebibytes(total)}, and {mebibytes(memory.gpuBytes)} on the GPU
      </summary>
      <table>
        <thead>
          <tr>
            <th />
            <th>Size</th>
            <th>Per trade</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <th>
              Trades
              <small>time, price, amount and side</small>
            </th>
            <td>{mebibytes(memory.tradesBytes)}</td>
            <td>{perTrade(memory.tradesBytes, count)}</td>
          </tr>
          {Derived.map(({ key, name, purpose }) => (
            <tr key={key} className="derived">
              <th>
                {name}
                <small>{purpose}</small>
              </th>
              <td>{mebibytes(memory[key])}</td>
              <td>{perTrade(memory[key], count)}</td>
            </tr>
          ))}
          <tr className="total">
            <th>Total</th>
            <td>{mebibytes(total)}</td>
            <td>{perTrade(total, count)}</td>
          </tr>
          <tr>
            <th>
              On the GPU
              <small>the trades again, for drawing, and the column summaries</small>
            </th>
            <td>{mebibytes(memory.gpuBytes)}</td>
            <td>{perTrade(memory.gpuBytes, count)}</td>
          </tr>
        </tbody>
      </table>
      <p data-testid="memory-derived">
        {memory.tradesBytes > 0
          ? `The derived data adds ${Math.round((derived / memory.tradesBytes) * 100)}% to the trades.`
          : 'No trades.'}{' '}
        The page’s memory is {mebibytes(memory.heapBytes)} in all, and never shrinks: it holds everything, up to 4 GiB.
      </p>
    </details>
  );
}
