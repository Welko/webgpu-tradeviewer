# Gpu module architecture (proposal, rev. 3)

Status: draft for review. Implemented so far: `Context`, `Profiler`, `Market::Engine` and `TradesPass`.

Changes since rev. 2: the module is renamed `Graphics` → `Gpu` (it does compute as well as rendering), the domain namespace `Trading` → `Market`, and the facade is `Gpu::Market::Engine`.

Changes in rev. 2: generic engine split from market-specific code, native windowed UI with Dear ImGui, a shared input path, decisions on resource tracking and pass names, and where profiling data goes.

## Goals

1. **WebGPU stays inside Gpu.** Code outside the module speaks in plain types: time ranges, spans of trade data, stats structs. No `WGPU*` type leaves the module.
2. **Generic engine, domain on top.** The engine knows nothing about trades. Market-specific passes, buffers and the public facade live in `Gpu::Market` and depend on the engine, never the other way round.
3. **Native is a first-class app, not just a test target.** It gets a window, the same interactions as the web, and its own UI.
4. **Render on demand.** Draw only when something changed or an animation runs.
5. **Profiling is part of the design.** GPU timestamps, CPU timings and resource counters flow through one path. When off, it costs about one branch per frame, and it can be compiled out.
6. **Bounded resources.** Every GPU allocation is counted. Steady-state frames allocate nothing.
7. **Testable without a browser or GPU.** The same code renders offscreen natively on a software Vulkan driver in CI.

## Layers

```
apps/ViewerApp
  App                      platform-independent: view state, input → actions, animation policy
  NativeMain               GLFW window + Dear ImGui panels (native UI)
  WebMain                  embind API for React (web UI)
     │
     ▼  public API (include/Gpu/)
Gpu::Market::Engine       facade (pimpl): the only public class with behavior
Gpu::Type                 plain data: Info, Target, PowerPreference, Options, FrameStats …
Gpu::Market::Type         plain data: View, TradeColumns (spans), Legend …
Gpu::Market::Style        marker sizes, summary bars, and the legend built from them (no GPU)
     │
     ▼  internal (src/include/Gpu/)
Gpu::Market               TradesPass, AggregatePass (compute), axes/overlay passes later
Gpu (engine)              Handle<T>, Resource::Stats, Profiler
```

Rules:

- `Gpu::*` engine headers never include `Gpu::Market` headers.
- `Gpu::Market` may depend on `Core::Data` (the GPU-free market data model). The engine may not.
- Nothing outside the module includes `src/include`.

### Naming

- **`Gpu`** (module and engine namespace): WebGPU rendering *and* compute.
- **`Market`** (domain namespace): trades now, order books in Task 2.
- **`Gpu::Market::Engine`** (facade). "Trading engine" was avoided because in finance it means the system that executes or matches orders.

## Public API (sketch)

```cpp
namespace Gpu::Market {

class Engine {
public:
    static void create(const Gpu::Type::Target::Any&, const Type::Options&, Callback onDone);

    // State changes. Each marks the engine dirty; none draws.
    void resize(std::uint32_t width, std::uint32_t height);
    void setView(const Market::Type::View& view);              // time/price window
    void setTrades(const Market::Type::TradeColumns& trades);  // uploads once

    [[nodiscard]] bool needsRender() const;   // dirty or animating
    void render();                           // draws only if needsRender()

    // Native UI: when enabled, the engine draws ImGui's draw data as its last pass.
    void setUiEnabled(bool enabled);

    void setProfiling(bool enabled);
    [[nodiscard]] const Gpu::Type::FrameStats& getStats() const;
    void setStatsListener(std::function<void()>);

    [[nodiscard]] const Gpu::Type::Info& getInfo() const;
    void processEvents();                    // native: pumps WebGPU callbacks
};

} // namespace Gpu::Market
```

`FrameStats` is plain data. The web wrapper converts it to a JS object for React (embind `value_object`):

```cpp
struct PassTiming { std::string name; double gpuMs; };
struct FrameStats {
    std::uint64_t frame;              // frame the GPU numbers belong to (lags 1–3 frames)
    double cpuEncodeMs, cpuSubmitMs;
    std::vector<PassTiming> passes;   // empty when unsupported or off
    double gpuTotalMs;
    std::uint32_t drawCalls, instances;
    std::uint64_t bytesUploadedThisFrame;
    std::uint32_t buffers, textures;
    std::uint64_t gpuBytesAllocated;  // our own bookkeeping; WebGPU has no query
    std::uint32_t timingsDropped;     // readback ring was full
    bool gpuTimingSupported;
};
```

## Frame flow

```
Engine::render()
  if !needsRender(): return
  Frame frame{context, timer}                    // acquires surface texture
  profiler->beginFrame(timed)                    // untimed if no slot is free
  for pass in passes: pass.prepare(frameCtx)     // uploads only what changed (counted)
  for pass in passes:
      desc.timestampWrites = profiler->measurePass(pass.name()) // nullptr when untimed
      pass.encode(encoder, desc)
  if uiEnabled: uiPass.encode(...)               // ImGui draw data, on top
  profiler->endPasses(encoder)                   // records copying the timestamps out
  frame.submit()                                 // submit, present (native)
  profiler->endFrame(stats)                      // reads the times; the frame is reported once they arrive
  dirty = false
```

CPU timing uses `Core::Profiling::Scope`, a steady-clock RAII scope. It is GPU-free, so it lives in `Core` and `App::update` can use it too.

## Native UI and input

- **Dear ImGui** for the native app. Its WebGPU backend supports wgpu-native, Dawn and Emscripten's `emdawnwebgpu` (4.0.10+). Its changelog targets wgpu-native 29, which we use (pinned in `modules/Gpu/CMakeLists.txt`, over WebGPU-distribution's default of 24).
- **Split:** the app builds the panels with ImGui's API (no WebGPU types involved). `Gpu::UiPass` renders `ImGui::GetDrawData()`, so the WebGPU part stays inside Gpu.
- **Web keeps React** (the brief asks for React). ImGui could still be switched on in the web build as a developer overlay, but it isn't required.
- **Showing stats without a feedback loop:** stats arrive after their frame, so showing them means drawing again. While the panel is visible, idle redraws are capped (about 4 Hz). ImGui is timed as its own pass (`imgui`), so totals can leave it out. A key hides the panel; hidden, it costs nothing. No cached scene texture is needed: ImGui only draws its own windows' triangles.
- **Bring back `Engine::hasPendingWork()` with the panel** (native only): whether frames still wait for their GPU times. The native loop then waits with a short timeout (`glfwWaitEventsTimeout`) instead of `glfwWaitEvents()`, or the last frame's times only appear after the next input.
- **One interaction model:** `App` owns view state and handles abstract input (`onPointerMove`, `onPointerButton`, `onWheel`, `onKey`). GLFW callbacks and DOM events both forward into it, so zoom, pan, reset and picking behave the same on both platforms. React and ImGui only call `App` actions and display `App` state.

## Resources and handles

- `Handle<T>` is a move-only owner (like `std::unique_ptr`) that releases its WebGPU object when destroyed. It also holds a pointer to a `Resource::Stats` block. Creating a handle adds its bytes and count; destroying it subtracts them.
- **Ownership:** passes own their handles, and there's exactly one owner per object. No `shared_ptr`. `Resource::Stats` only counts.
- **Lifetime rule:** `Resource::Stats` lives in the `Context`, which is destroyed after all passes.
- Easy to change later: call sites only see `Handle<T>`.

## Profiling

`Gpu::Profiler` (`Gpu/Profiler.hpp`) is the one class the engine talks to, with plain calls in a fixed order: `beginFrame`, `measurePass` per pass, `endPasses` (before submit), `endFrame(stats)` (after submit). Drawn frames wait in a `Core::RingQueue` of `{ready, stats}`; a readback marks its frame ready, and ready frames are popped from the front, so each frame is reported once, in order. When the ring is full, the oldest is reported as `TooLate`. The engine holds it in a `shared_ptr`; readbacks hold a `weak_ptr` and are dropped if it is gone. Step 16's stats history reuses `Core::RingQueue`; if it, CSV recording and trace export grow large, they get a folder then.

Owned by the engine and passed by reference through `FrameContext`. No globals.

- **Features.** `Context` requests `timestamp-query` only if the adapter has it. Without it the engine still reports CPU times and counters, and `gpuTimingSupported = false`.
- **Slots:** one query set (`2 × MaxPasses` entries, `MaxPasses` = 8) and one resolve buffer, shared by all frames: the GPU runs them in order. Only the mapped readback buffers need one per frame in flight: K = 3 slots, all allocated once.
- **The pass is the unit of GPU timing.** WebGPU only allows timestamps at pass boundaries. To time something separately, make it its own pass.
- **Readback** is asynchronous. When a slot's map completes, its timestamps (nanoseconds) become `PassTiming` entries for that frame, and the stats listener fires. Nothing waits on the GPU.
- **Back-pressure:** if no slot is free, the frame renders untimed and `timingsDropped` increments.
- **Off:** `setProfiling(false)` releases the query sets and buffers.
- **Compiled out:** CMake option `TRADEVIEWER_PROFILING` (default ON) replaces it, and `Core::Profiling::Scope`, with inline stubs. Call sites don't change, and a test builds the OFF configuration so it can't rot.
- **Tunables** (K, `maxPasses`, history length) are named constants in one header, so changing them later is a one-line edit.
- **Pass names** are `std::string` for now. Only `Profiler` and `FrameStats` touch them, so switching to ids later stays local.
- **Precision:** Chrome rounds timestamps to 100 µs unless started with WebGPU developer features. The debug panel shows this.

## Profiling in production

Decided 2026-10-27: profiling is compiled into release builds too, so users, CI benchmark runs on release builds, and telemetry can all use it.

- **CPU timing is always on.** It costs a few `steady_clock` reads per frame.
- **GPU timing is sampled.** Time a random frame now and then (never every Nth: it aliases with periodic passes), or every frame while the debug panel or a recording is active. Each timed frame costs timestamp writes and one small readback.
- **Each frame says why it has no GPU times** (`FrameStats::Untimed`): off, unsupported, no free slot, readback failed, too late, or not resolved. The last four mean timings were lost; they're warned about at most once a second, and telemetry should count them.
- **Precision travels with the times.** Chrome rounds timestamps to 100 µs unless started with WebGPU developer features, so short passes read as 0 or 100 µs there.
- **Telemetry** (with consent only) sends CPU times, sampled GPU times with their precision, and counts of untimed frames by reason.

## Where profiling data goes

- **In memory:** a fixed-size ring of the last 600 frame records (~16 passes each, a few hundred KB at most). It can't grow, so it can't blow up. Median and p95 are computed from it.
- **Recording to disk** is an explicit mode (start/stop in the debug panel, or a fixed-length benchmark run). Records stream through a shared `Core::Csv::RotatingWriter`:
  - a new file every N rows (default 10,000);
  - at most M files per folder (default 1,000). When the cap is reached it stops and reports it, instead of deleting anything;
  - a new file whenever the column set changes, so old files never need padding.
  - Natively this writes to disk. On the web there's no disk, so the panel offers "Download CSV" from the in-memory ring or a recorded session.
- **Trace export:** the same records can be written as Chrome Trace Event JSON, which opens in [Perfetto](https://ui.perfetto.dev) and `chrome://tracing`. That gives one timeline viewer for web and native.
- **On the web**, CPU scopes are also reported with `performance.measure()`, so they show up in Chrome DevTools' Performance timeline next to everything else.

## Render on demand

- The engine keeps a `dirty` flag. `resize`, `setView` and `setTrades` set it; `render()` clears it.
- `App::update(now)` returns whether an animation is running (e.g. Task 2 replay).
- **Web:** React requests one animation frame when something changes and keeps requesting only while `needsRender()` or animating.
- **Native:** `glfwWaitEvents()` when idle, `glfwPollEvents()` while animating. `processEvents()` pumps WebGPU callbacks each iteration.
- **Benchmarks:** a "continuous" toggle forces a frame every vsync (Task 3).

## Testing

- **Headless target:** `Type::Target::Offscreen{width, height}`. In CI, a device on Mesa's software Vulkan driver (lavapipe).
- **Counter tests:** unchanged frames upload 0 bytes and create nothing; zoom/pan/resize loops keep `buffers` and `gpuBytesAllocated` flat.
- **Image tests** (later): compare offscreen pixels against a CPU reference.
- **Timestamp queries are required in CI.** GPU tests fail (not skip) when the device lacks `timestamp-query` or when no timing data comes back. Locally, lavapipe (LLVM 20) offers `timestamp-query`; whether real timing values come back is checked at that step.
- **Timing values** aren't compared against thresholds in CI; they go to benchmark runs.

## Decisions log

| # | Question | Decision |
|---|---|---|
| 1 | Virtual `Pass` interface vs. concrete passes | Concrete members (2026-10-29): `Engine::Impl` holds `std::optional<TradesPass>` and `render()` calls each pass in a fixed order. Each pass has its own inputs (`setView`, `append`, heatmap data later), which a shared interface can't express. Revisit if passes need adding or removing at runtime |
| 2 | Who owns GPU objects | Passes own `Handle<T>`; `Resource::Stats` counts via a pointer in the handle |
| 3 | Ring size, history length | Guesses, kept as named constants |
| 4 | Pass names | `std::string` for now, contained in `Profiler`/`FrameStats` |
| 5 | Names | Module `Gpu`, domain `Gpu::Market`, facade `Gpu::Market::Engine` |
| 6 | Engine structure | One `Market::Engine` implementation (device, output, passes as functions). The Context/Surface/Frame/Pass layers were removed as too heavy for this project; split the file when it grows |
| 7 | Marker shape | One equilateral triangle per trade (2026-10-31). BenchmarkApp, Intel HD 520, whole day with every trade: 6-vertex square 36.6 ms, 4-vertex strip 36.6 ms, triangle 21.9 ms. Triangle count, not vertex count, limits tiny markers. Other zooms: within noise |
| 8 | Crowded views | Column summaries past 32 trades per CSS pixel column (2026-10-31): one bar per column, min to max price, color by buy share, opacity by amount. Computed on the CPU from a price segment tree and running totals by side, O(columns · log n). Drawn by `ColumnsPass` instead of `TradesPass` |
