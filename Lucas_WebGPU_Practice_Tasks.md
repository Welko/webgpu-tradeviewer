# Lucas — WebGPU market-data practice brief

Three progressive take-home exercises using one exchange, one instrument, and one day.
These are original practice assignments, not reported or official Jump Trading questions.

## Chosen dataset

**Tardis / Binance Spot / BTCUSDT / 1 December 2019 (UTC)**

Download these two complementary files:

- [Trades — CSV.gz](https://datasets.tardis.dev/v1/binance/trades/2019/12/01/BTCUSDT.csv.gz)
- [Incremental L2 order book — CSV.gz](https://datasets.tardis.dev/v1/binance/incremental_book_L2/2019/12/01/BTCUSDT.csv.gz)

They are linked in [Tardis's official Binance documentation](https://docs.tardis.dev/historical-data-details/binance). This date is a documented public sample. The recorded Binance depth feed uses 100 ms updates, so the exercise concerns received market data, not reconstruction of every individual exchange order event.

Read the [CSV schemas](https://docs.tardis.dev/downloadable-csv-files/data-types) and [order-book reconstruction rules](https://docs.tardis.dev/faq/order-books). The download links and format documentation were checked on 21 October 2026; the files have not been downloaded or counted for this brief. Report their actual row counts, sizes, and timestamp coverage yourself.

## How to use the exercises

Work through the tasks in order and reuse the previous code. Suggested timeboxes are **4–6 hours**, **6–10 additional hours**, and **3–5 additional hours**. These are planning estimates for practice, not predictions of the employer's time allowance. Stop at the timebox and explain unfinished work.

Use TypeScript, React, WebGPU, and WGSL. React can manage controls and layout; implement the market-data drawing yourself. Decompression, parsing, and UI utility libraries are allowed. A preprocessing script and precomputed data chunks are allowed if their code and regeneration instructions are included.

Use UTC throughout. Keep source timestamps in a representation that preserves their integer precision; calculate times relative to a chosen origin before converting values to GPU floats. A missing WebGPU adapter should produce a useful error message.

---

## Task 1 — Interactive trade viewer

**Brief:** Build a browser application that lets a trader inspect all recorded BTC/USDT executions over a selected time range.

### Required behavior

1. Load the trades file and show progress, actual trade count, and recorded time range.
2. Draw trades with WebGPU: time on the horizontal axis and price on the vertical axis.
3. Distinguish the trade `side` values by color and encode quantity with marker size. Include a legend.
4. Provide time zoom, horizontal pan, reset, and a cursor that shows the nearest trade's time, price, quantity, and side. Use a documented distance rule for picking.
5. Show the visible trade count and total traded quantity for the chosen time range.
6. Initially show 12:00–13:00 UTC, with a control to expand to the complete recorded day.

### Acceptance criteria

- At a sufficiently close zoom, every trade in the selected interval is represented. At wider zoom, any aggregation or sampling is explicit in the UI.
- Boundary handling is deterministic: use a half-open time interval, start inclusive and end exclusive.
- Compare the displayed count and quantity for three fixed ranges with an independent CPU reference calculation; document the ranges and results.
- Resizing and repeated zoom/pan operations produce no visual corruption or steadily growing allocation.
- Report actual data volume; do not silently duplicate trades to claim a larger dataset.

### Submit

Working application, setup instructions, and a short note explaining GPU buffer layout, picking strategy, and any level-of-detail choices.

**Discussion prompts:** What limits performance first? How would you handle ten times as many trades? Which operations belong on the CPU, and which benefit from the GPU?

---

## Task 2 — Liquidity heatmap and market replay

**Brief:** Extend the viewer to show how available liquidity evolves, with executions overlaid on the book history.

### Required behavior

1. Reconstruct separate bid and ask books from the incremental L2 file.
2. Add a WebGPU heatmap: time horizontally, fixed absolute price vertically, and resting quantity as intensity. Provide a clear bid/ask legend and an intensity-scale control.
3. Overlay trades from Task 1, plus best bid and ask.
4. Add play/pause, seek, and 1×, 10×, and 100× replay controls.
5. Display the current top ten bid and ask levels and the spread.
6. Seeking to any time must recover the same book as replaying from the beginning. Checkpoints are allowed.

### Data correctness requirements

Follow the provider's format rather than treating each CSV row as an independent complete book:

- Skip rows before the first snapshot.
- Clear the book once when a new snapshot batch begins, then consume its rows.
- A nonzero `amount` replaces the quantity at that side and price; zero deletes that level.
- Apply all rows of a message group before publishing a consistent book state. The provider documents grouping by `local_timestamp`.
- Preserve file order. Use local receive time for replay and document a deterministic tie rule when combining the book and trades.
- Replay and seeking must include preceding book state, not just updates inside the visible time window.
- Surface missing or anomalous states, including crossed books, and document any correction policy.

These rules derive from the provider's reconstruction documentation linked above.

### Define the heatmap precisely

For this exercise, each time column displays the book state at that column's right edge, after complete groups available at that time. Each price cell sums quantities at prices inside its half-open price interval. Carry state forward between updates; leave genuinely unknown state marked as unknown.

Price boundaries stay fixed for a given view. Rebin when the user changes the price range. Do not shift each historical column independently around its own mid-price.

Start with a 1,024 × 512 time/price grid. Preserve real timestamps at accelerated playback speeds; acceleration changes the wall-clock replay rate.

### Acceptance criteria

- Small hand-authored fixtures cover quantity replacement, deletion, a second snapshot, and a multi-row message group.
- For ten fixed timestamps, compare top-ten books and spreads from direct seeking against a sequential reference replay. Results must agree.
- For a small chosen heatmap region, verify the binned values against a straightforward CPU reference.
- Pausing freezes the simulation clock and produces a consistent book, heatmap, and trade overlay.
- A seek does not reuse stale state from the previous position.

### Submit

Updated application and a brief explanation of reconstruction, checkpointing, heatmap semantics, and synchronization.

**Discussion prompts:** How do you seek quickly? Why does batching matter for correctness? What changes if incoming data outpaces display refresh?

---

## Task 3 — Performance investigation under load

**Brief:** Make the application remain usable while replay accelerates, and show evidence for your optimization choices.

### Reproducible workload

Use the actual dataset from Tasks 1 and 2. Log source rows, message-group counts, visible trades, heatmap dimensions, and the selected interval.

Benchmark all three configurations on the same machine:

| Configuration | History window | Heatmap | Replay |
| --- | --- | --- | --- |
| Small | 1 minute | 512 × 256 | 1× |
| Medium | 1 hour | 1,024 × 512 | 10× |
| Large | Full available day | 2,048 × 1,024 | 100× |

Use a 1,600 × 900 CSS-pixel viewport, device-pixel ratio 1, and the same rendering settings across before/after comparisons. Let each run warm up for five seconds, then measure 30 seconds, looping the replay if it reaches the end.

The one-minute and one-hour windows start at 12:00 UTC. Record the day-wide range actually present. Count what is drawn; do not imply that all source rows are simultaneously visible.

### Required work

1. Capture a baseline and identify the dominant bottleneck using profiling.
2. Make at least one justified change and repeat the same measurements.
3. Avoid reparsing the complete source or rebuilding all historical GPU resources on every frame.
4. Keep queues and retained history bounded, and explain the chosen limits.
5. Process book updates in order. Coalescing display frames is allowed; silently skipping unapplied book updates is not.
6. Make overload visible. If playback cannot keep up, expose lag and slow the simulation clock or use backpressure with a documented policy.

### Measurements to report

- Load/preprocessing time, separately from warmed replay.
- Median and p95 displayed-frame intervals measured with `requestAnimationFrame`.
- CPU processing and submission time, separately from displayed-frame intervals.
- Estimated GPU-buffer/texture allocations and available browser memory measurements, stating what is excluded.
- Pause responsiveness and replay-clock lag.
- Machine, GPU, browser version, build mode, and exact reproduction steps.

Use GPU timestamps only if supported. CPU time spent submitting GPU commands is not GPU execution time.

### Performance goal

On your documented machine, aim for **p95 displayed-frame intervals of 20 ms or less** in the medium configuration, and pause-to-frozen-frame latency under **100 ms**. For the large configuration, find and explain the saturation point.

These are practice targets, not a promise that every machine or implementation can meet them. Correctness and honest measurements matter more than forcing a target. Report regressions and any quality/performance trade-off; do not manufacture a deliberately poor baseline.

### Submit

Before/after results, one profiler capture or equivalent trace summary, and a one-page explanation of the change, evidence, and remaining bottleneck.

**Discussion prompts:** Did WebGPU improve the measured bottleneck? What is lost through visual aggregation? How would the design change for a live feed?

---

## Suggested self-review

This is a practice rubric, not Jump Trading's:

| Area | Weight | Evidence |
| --- | --- | --- |
| Correctness | 35% | Faithful state reconstruction, reproducible reference comparisons, precise aggregation semantics |
| Performance reasoning | 30% | Controlled measurements, clear bottleneck diagnosis, justified trade-offs |
| Architecture | 20% | Separation of parsing/replay/rendering, bounded state, resource cleanup |
| Usability and communication | 15% | Responsive controls, legible display, concise setup and design notes |

A strong submission is easy to run and explains its limits. Prioritize working behavior and measurements over decorative interface work.

## Final handoff checklist

- Source and dependency lockfile.
- Commands to obtain and prepare the two data files.
- Exact command to run the app.
- Browser and hardware requirements.
- Actual input counts and time coverage.
- Correctness checks and benchmark reproduction steps.
- Short design note and known limitations.
- A two-minute recorded or live walkthrough is optional.

If time is limited, complete Task 2 with a minimal trade overlay and a few honest performance measurements. It combines the most relevant graphics, real-time data, and correctness work in this set.

