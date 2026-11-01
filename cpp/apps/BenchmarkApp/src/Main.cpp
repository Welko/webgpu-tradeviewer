// Measures what showing trades costs at several zoom levels, offscreen, and
// writes a Markdown table: the CPU time to update the view, the CPU phases of
// a frame and the GPU time of each pass. CI publishes it on each run's page.
//
// Beside the table, <table>.json holds the numbers tools/check-benchmark.mjs
// compares with the baseline in tools/benchmark-baseline.csv: times to load
// and show the trades, each view's medians, and the memory the synthetic day
// takes, which is the same on every machine.
//
// Usage: BenchmarkApp <table.md> [trades.csv[.gz]] [--commit <sha>]
//   the synthetic day without a file; --commit names the commit measured in
//   benchmark.json, so the file says where it came from once downloaded
// The table goes to a file because log messages go to stdout.

#include "Viewer/App.hpp"

#include "Core/Data/TradeFile.hpp"
#include "Core/Log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

// Task 3's viewport, at a pixel ratio of 1.
constexpr std::uint32_t WidthPx = 1600;
constexpr std::uint32_t HeightPx = 900;
constexpr int WarmUpFrames = 10;
constexpr int MeasuredFrames = 60;

/** A view to measure, and how to get there. */
struct Case
{
    std::string name;
    std::string key;  // starts its metrics' keys
    std::function<void(Viewer::App&)> show;
};

/** One view's measured frames, in milliseconds. */
struct Samples
{
    std::size_t tradesDrawn = 0;
    std::vector<double> viewUpdate;
    std::vector<double> cpuFrame;  // acquire, encode and submit
    std::vector<double> acquire;
    std::vector<double> encode;
    std::vector<double> submit;
    std::map<std::string, std::vector<double>> passes;  // by pass name, in drawing order of first appearance
    std::vector<std::string> passOrder;
    std::vector<double> gpuTotal;
};

/** The value `share` of the way up `values`, by the nearest-rank method. */
double getPercentile(std::vector<double> values, double share)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto rank = static_cast<std::size_t>(share * static_cast<double>(values.size()) + 0.999999);
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

/** "median (95th percentile)". */
std::string describe(const std::vector<double>& values)
{
    if (values.empty()) {
        return "–";
    }
    return std::format("{:.3f} ({:.3f})", getPercentile(values, 0.5), getPercentile(values, 0.95));
}

/** A number compared between runs (see tools/check-benchmark.mjs). */
struct Metric
{
    std::string key;  // a CSV column: letters, digits and underscores
    std::string label;
    std::string unit;  // "bytes", compared exactly; "ms", within a margin
    double value;
    // Too unsteady between runs to fail a build on: recorded and shown, never checked.
    bool flaky = false;
};

/**
 * What the synthetic day takes in memory, with the whole day in view so
 * column summaries are counted: the same on every machine. Nothing, with a
 * message, if no app can be created.
 */
std::optional<std::vector<Metric>> measureMemory()
{
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = Gpu::Type::Target::Offscreen{ .width = WidthPx, .height = HeightPx } },
        [&](Viewer::App::Result result) {
            if (result) {
                app = std::move(*result);
            }
        });
    if (!app) {
        return std::nullopt;
    }
    app->resize(WidthPx, HeightPx);
    app->setPixelRatio(1.0f);
    if (!app->setTrades(Viewer::generateSyntheticDay())) {
        return std::nullopt;
    }
    app->showDay();
    const auto memory = app->getMemoryStats();
    const auto bytes = [](std::size_t value) { return static_cast<double>(value); };
    return std::vector<Metric>{
        { "memory_trades_bytes", "Memory: trades (synthetic day)", "bytes", bytes(memory.tradesBytes) },
        { "memory_totals_bytes", "Memory: running totals", "bytes", bytes(memory.totalsBytes) },
        { "memory_price_tree_bytes", "Memory: price tree", "bytes", bytes(memory.priceTreeBytes) },
        { "memory_summaries_bytes", "Memory: column summaries (whole day)", "bytes", bytes(memory.summariesBytes) },
        { "memory_gpu_bytes", "Memory: GPU buffers and textures", "bytes", bytes(static_cast<std::size_t>(memory.gpuBytes)) },
    };
}

/** Waits until `count` frames have been reported; false if that takes 10 s. */
bool waitForStats(Viewer::App& app, const std::vector<Gpu::Type::FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return reported.size() >= count;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> paths;
    std::optional<std::string> commit;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--commit" && i + 1 < argc) {
            commit = argv[++i];
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty() || paths.size() > 2) {
        std::fputs("Usage: BenchmarkApp <table.md> [trades.csv[.gz]] [--commit <sha>]\n", stderr);
        return 2;
    }
    const std::string& tablePath = paths[0];
    std::vector<Metric> metrics;
    const auto timed = [](auto work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    std::expected<Core::Data::Trades, std::string> trades;
    const double loadMs = timed([&] { trades = paths.size() > 1 ? Core::Data::readTradeFile(paths[1]) : Viewer::generateSyntheticDay(); });
    if (!trades) {
        std::fputs(std::format("{}\n", trades.error()).c_str(), stderr);
        return 1;
    }

    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = Gpu::Type::Target::Offscreen{ .width = WidthPx, .height = HeightPx } },
        [&](Viewer::App::Result result) {
            if (!result) {
                std::fputs(std::format("{}\n", result.error()).c_str(), stderr);
                return;
            }
            app = std::move(*result);
        });
    if (!app) {
        return 1;
    }
    app->resize(WidthPx, HeightPx);
    app->setPixelRatio(1.0f);
    std::expected<void, std::string> set;
    const double setTradesMs = timed([&] { set = app->setTrades(std::move(*trades)); });
    if (!set) {
        Core::Log::error("{}", set.error());
        return 1;
    }
    metrics.push_back({ "load_ms", "Reading the trades", "ms", loadMs });
    metrics.push_back({ "set_trades_ms", "Showing them: derived data and upload", "ms", setTradesMs });

    std::vector<Gpu::Type::FrameStats> reported;
    app->setStatsCallback([&reported](const Gpu::Type::FrameStats& stats) { reported.push_back(stats); });
    app->setProfiling(true);

    // Each view says whether crowded views may be summarized, so none depends on the one before.
    const auto show = [](bool summaries, auto view) {
        return [summaries, view](Viewer::App& viewer) {
            viewer.setSummariesAllowed(summaries);
            view(viewer);
        };
    };
    const std::vector<Case> cases{
        { "Whole day, every trade", "day_every_trade", show(false, [](Viewer::App& viewer) { viewer.showDay(); }) },
        { "Whole day", "day", show(true, [](Viewer::App& viewer) { viewer.showDay(); }) },
        { "12:00–13:00", "hour", show(true, [](Viewer::App& viewer) { viewer.resetView(); }) },
        { "1 minute", "minute", show(true, [](Viewer::App& viewer) { viewer.resetView(); viewer.zoomAt(WidthPx / 2.0, 1.0 / 60.0); }) },
        { "1 second", "second", show(true, [](Viewer::App& viewer) { viewer.resetView(); viewer.zoomAt(WidthPx / 2.0, 1.0 / 3600.0); }) },
    };

    std::vector<Samples> results;
    for (const auto& benchmark : cases) {
        Samples samples;
        const std::size_t firstMeasured = reported.size() + WarmUpFrames;
        for (int frame = 0; frame < WarmUpFrames + MeasuredFrames; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            benchmark.show(*app);
            const std::chrono::duration<double, std::milli> updateMs = std::chrono::steady_clock::now() - start;
            if (frame >= WarmUpFrames) {
                samples.viewUpdate.push_back(updateMs.count());
            }
            app->render();
            // One frame at a time: each is measured alone, and the profiler always has a free slot.
            if (!waitForStats(*app, reported, reported.size() + 1)) {
                std::fputs("A frame's GPU times never arrived\n", stderr);
                return 1;
            }
        }
        samples.tradesDrawn = app->getViewStats().count;
        for (std::size_t i = firstMeasured; i < firstMeasured + MeasuredFrames; ++i) {
            const auto& stats = reported[i];
            if (!stats.cpu || !stats.gpu) {
                std::fputs("A measured frame has no CPU or GPU times; the device needs timestamp-query\n", stderr);
                return 1;
            }
            samples.cpuFrame.push_back(stats.cpu->acquireMs + stats.cpu->encodeMs + stats.cpu->submitMs);
            samples.acquire.push_back(stats.cpu->acquireMs);
            samples.encode.push_back(stats.cpu->encodeMs);
            samples.submit.push_back(stats.cpu->submitMs);
            samples.gpuTotal.push_back(stats.gpu->totalMs);
            for (const auto& pass : stats.gpu->passes) {
                if (!samples.passes.contains(pass.name)) {
                    samples.passOrder.push_back(pass.name);
                }
                samples.passes[pass.name].push_back(pass.gpuMs);
            }
        }
        results.push_back(std::move(samples));
    }

    // Views can draw different passes: crowded ones summarize trades in columns.
    std::vector<std::string> passNames;
    for (const auto& samples : results) {
        for (const auto& pass : samples.passOrder) {
            if (std::ranges::find(passNames, pass) == passNames.end()) {
                passNames.push_back(pass);
            }
        }
    }

    std::ofstream table(tablePath);
    const auto print = [&table](const std::string& text) { table << text; };
    const auto& info = app->getInfo();
    print(std::format("GPU: {} ({}, {}). {} × {} pixels; {} frames per view after {} to warm up. "
                      "Milliseconds: median (95th percentile).\n\n",
                      info.device, info.backend, info.adapter, WidthPx, HeightPx, MeasuredFrames, WarmUpFrames));
    print("| View | Trades drawn | View update (CPU) | Acquire (CPU) | Encode (CPU) | Submit (CPU) |");
    for (const auto& pass : passNames) {
        print(std::format(" GPU: {} |", pass));
    }
    print(" GPU total |\n|---|--:|--:|--:|--:|--:|");
    for (std::size_t i = 0; i < passNames.size(); ++i) {
        print("--:|");
    }
    print("--:|\n");
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& samples = results[i];
        print(std::format("| {} | {} | {} | {} | {} | {} |", cases[i].name, samples.tradesDrawn, describe(samples.viewUpdate),
                          describe(samples.acquire), describe(samples.encode), describe(samples.submit)));
        for (const auto& pass : passNames) {
            const auto found = samples.passes.find(pass);
            print(std::format(" {} |", found != samples.passes.end() ? describe(found->second) : "–"));
        }
        print(std::format(" {} |\n", describe(samples.gpuTotal)));
    }
    if (!table) {
        std::fputs(std::format("Can't write {}\n", tablePath).c_str(), stderr);
        return 1;
    }

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& samples = results[i];
        const auto& name = cases[i].name;
        const auto& key = cases[i].key;
        metrics.push_back({ key + "_update_ms", name + ": view update (CPU)", "ms", getPercentile(samples.viewUpdate, 0.5) });
        metrics.push_back({ key + "_frame_cpu_ms", name + ": frame (CPU)", "ms", getPercentile(samples.cpuFrame, 0.5) });
        metrics.push_back({ key + "_frame_gpu_ms", name + ": frame (GPU)", "ms", getPercentile(samples.gpuTotal, 0.5) });
    }
    const auto memory = measureMemory();
    if (!memory) {
        std::fputs("Can't measure the synthetic day's memory\n", stderr);
        return 1;
    }
    metrics.insert(metrics.begin(), memory->begin(), memory->end());

    auto list = nlohmann::ordered_json::array();
    for (const auto& metric : metrics) {
        list.push_back({ { "key", metric.key }, { "label", metric.label }, { "unit", metric.unit }, { "value", metric.value },
                         { "flaky", metric.flaky } });
    }
    nlohmann::ordered_json json{
        // Its own baseline: the web's memory differs (32-bit), and so do its times.
        { "platform", "native" },
        { "gpu", std::format("{} · {} · {}", info.device, info.backend, info.adapter) },
        { "trades", app->getTrades().size() },
        { "metrics", std::move(list) },
    };
    if (commit) {
        json["commit"] = *commit;
    }
    const auto jsonPath = std::filesystem::path(tablePath).replace_extension(".json");
    if (!(std::ofstream(jsonPath) << json.dump(2) << '\n')) {
        std::fputs(std::format("Can't write {}\n", jsonPath.string()).c_str(), stderr);
        return 1;
    }
    return 0;
}
