# Task 2: what only a GPU makes possible

Replaces Tasks 2 and 3 of the original brief (the order-book heatmap and the
performance investigation), which are dropped. Task 1 could nearly all have
run on a CPU. This task adds things that couldn't.

**The rule for what belongs here:** a GPU wins when millions of pixels are each
touched many times, or when everything is redrawn every frame. So each feature
should either redraw every trade each frame, or change what all of them look
like through one small value sent to a shader, with nothing re-uploaded.

| Whole day, every trade (about 430,000 markers) | Time per frame |
|---|--:|
| Intel HD 520, a 2015 laptop chip | 22 ms |
| SwiftShader, running on the CPU | 12,200 ms |

(Not a fair limit: a hand-written CPU renderer would beat SwiftShader, and the
two were measured with different marker sizes.)

## 1. Foundations: a dynamic viewer

The engine draws one fixed picture today, so making it dynamic comes first.

- **Data apart from views.** One `Dataset`: the trades and what's worked out
  from them, on the CPU, and one copy on the GPU. Any number of views read it.
  Today each `App` owns its own, and the GPU buffer is private to `TradesPass`.
  This takes in option C of the old App/Engine brainstorm (the `Chart` class).
- **Views.** Each has its own time and price range, its own kind of drawing, and
  its own canvas.
- **Layers.** A view is a list of layers, drawn in order, instead of the fixed
  grid, then trades or columns, in `Engine::Impl::render`.
- **Per-view shader values.** Passes write their uniforms just before encoding,
  into one buffer. All queue writes land before the submit, so drawing a pass
  twice in a frame shows the last view's values twice. Each view needs its own.
- **Separate canvases.** Any number of them, anywhere on the page, with other
  elements in between: the frontend team decides the layout, not the viewer.
  Natively, one window each.
- **One GPU device, shared.** A device can draw to many canvases, and everything
  on it (buffers, pipelines) can be used for any of them. Today `Engine::create`
  makes its own device; that moves out into something the views share, with
  `GpuSession`'s restarts. `RenderTarget` is already separate, so each canvas gets
  one. Open points: pipelines are built for one canvas's format, so a second
  format needs its own; natively, the adapter is chosen to suit the first window,
  and another window it can't draw to must fail with a clear message.
- **Drawn together.** Every canvas is drawn in the same frame callback, so linked
  animations stay in step.
- **A shared selection.** The hovered trade, and the chosen ranges of time, price
  and size. Trade indices mean the same in every view of a dataset, so linking
  is sharing this one thing.
- **An animation clock.** Mostly there: `Engine::update(nowMs)` already stores
  the time, and the frame loops already keep drawing while `needsRender()` says
  so. An idle page must still schedule nothing.

## 2. New views

- **Density.** At wide zoom, instead of bars: every trade as a soft blob, added
  together into a floating-point texture, buys and sells in separate channels.
  A shader turns that into colour per pixel. Shows where within the price range
  the trading happened, and who dominated there. Brightness and contrast change
  instantly, without redrawing the blobs; blob radius redraws them. Replaces the
  density grid planned in step 18b.
- **Size view.** Time across, trade size up (log scale). Same GPU data as the
  markers, a different vertex shader. Shows when large trades happened.
- **Volume by price.** A side panel: how much traded at each price in the view.
- **Volume over time.** Bars under the main view: how much traded per interval.
- **Buy pressure.** A line of total bought minus total sold over time, from the
  running totals we already keep.

## 3. Linking

- **Hover:** point at a trade in one view, and it's ringed in all of them.
- **Time cursor:** a vertical line at the same time in every view.
- **Brushing:** drag out a range in any view, and everything outside it dims in
  all views, instantly: a shader compares each trade with the selection. Size
  and price ranges work the same way as time.
- **Size filter:** a slider, "only trades above 1 BTC". Same mechanism.
- **Follow or not:** each view either follows another's zoom and pan, or stays
  independent.
- **Click to focus:** click a bar in one view to zoom another to its trades.

## 4. Animations

Each trade's motion is worked out in the vertex shader from one or two values
per frame, so 430,000 trades animate with nothing uploaded.

- **Smooth zoom and pan:** the buttons glide to their range, the wheel eases.
  Today they jump.
- **Morph between views:** every trade glides from its place in one view to its
  place in another, such as price to size.
- **Bars to markers:** crossing the crowding threshold, trades spread out of their
  bars instead of today's hard switch, and gather back when zooming out.
- **Replay:** a clock runs through the day at chosen speeds, with pause and seek.
  Each trade pops in when its time comes and fades with age.
- **Selection:** dimming fades instead of snapping; the ring eases in.
- **Reveal on load:** a new file's trades sweep in from left to right.

Respect the browser's reduced-motion setting: jump instead of animating.

## 5. Order

1. Foundations, showing today's picture on one canvas, pixel for pixel.
2. Tests with 4 viewers on one page, and more (see below).
3. Two canvases with hover linked: markers and the size view.
4. Brushing and the size filter.
5. Smooth zoom and pan, then morphing and bars to markers.
6. Density.
7. Replay.
8. Volume by price, volume over time, buy pressure.

## Many viewers on one page

Tests with up to 4 viewers on the same page, and more, to find where it breaks:

- Each draws, resizes and responds to its own input, with other page elements
  between the canvases.
- Removing one leaves the others working; adding one later works.
- Sharing data: memory stays about flat as viewers are added.
- Losing the GPU brings them all back.
- Loading other trades shows in every viewer of that data.
- Past the browser's limits, a clear message rather than a blank canvas.

Today's page has things that assume one viewer: `window.viewer` and
`window.viewerTesting` hold just one.

## Limits to keep in mind

- **Not all at 60 Hz on this laptop.** The whole day as markers already takes
  22 ms a frame on the HD 520, so morphing it runs near 45 Hz. Density may cost
  more, since blobs cover more pixels than markers. Nothing above is measured.
- **Blending precision.** `rgba16float` can be blended everywhere in WebGPU, but
  half floats lose precision when many blobs add up. Measure before going further.
- **Tests.** CI draws on a CPU, so image tests use small scenes with a fixed
  clock. Animations are tested at chosen moments, not by watching them play.
- **Memory.** Each extra view should cost its shader values, not another copy of
  the trades. Two separate `App`s today would cost about 30 MB per viewer.
