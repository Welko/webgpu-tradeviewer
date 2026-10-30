// Measures what showing trades costs at several zoom levels, offscreen, and
// writes a Markdown table: the CPU time to update the view, the CPU phases of
// a frame and the GPU time of each pass. CI publishes it on each run's page.
//
// Usage: BenchmarkApp <table.md> [trades.csv[.gz]]   (the synthetic day without a file)
// The table goes to a file because log messages go to stdout.

#include "Viewer/App.hpp"

#include "Core/Data/TradeFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// Task 3's viewport, at a pixel ratio of 1.
constexpr std::uint32_t Width = 1600;
constexpr std::uint32_t Height = 900;
constexpr int WarmUpFrames = 10;
constexpr int MeasuredFrames = 60;

/** A view to measure, and how to get there. */
struct Case
{
    std::string name;
    std::function<void(Viewer::App&)> show;
};

/** One view's measured frames, in milliseconds. */
struct Samples
{
    std::size_t tradesDrawn = 0;
    std::vector<double> viewUpdate;
    std::vector<double> acquire;
    std::vector<double> encode;
    std::vector<double> submit;
    std::map<std::string, std::vector<double>> passes;  // by pass name, in drawing order of first appearance
    std::vector<std::string> passOrder;
    std::vector<double> gpuTotal;
};

/** "median (95th percentile)", by the nearest-rank method. */
std::string describe(std::vector<double> values)
{
    if (values.empty()) {
        return "–";
    }
    std::ranges::sort(values);
    const auto at = [&](double share) {
        const auto rank = static_cast<std::size_t>(share * static_cast<double>(values.size()) + 0.999999);
        return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
    };
    return std::format("{:.3f} ({:.3f})", at(0.5), at(0.95));
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
    if (argc < 2 || argc > 3) {
        std::fputs("Usage: BenchmarkApp <table.md> [trades.csv[.gz]]\n", stderr);
        return 2;
    }
    auto trades = argc > 2 ? Core::Data::readTradeFile(argv[2]) : Viewer::generateSyntheticDay();
    if (!trades) {
        std::fputs(std::format("{}\n", trades.error()).c_str(), stderr);
        return 1;
    }

    std::unique_ptr<Viewer::App> app;
    Viewer::App::create(Gpu::Type::Target::Offscreen{ .width = Width, .height = Height }, Gpu::Type::PowerPreference::Default,
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
    app->resize(Width, Height);
    app->setPixelRatio(1.0f);
    app->setTrades(std::move(*trades));

    std::vector<Gpu::Type::FrameStats> reported;
    app->setStatsCallback([&reported](const Gpu::Type::FrameStats& stats) { reported.push_back(stats); });
    app->setProfiling(true);

    const std::vector<Case> cases{
        { "Whole day", [](Viewer::App& viewer) { viewer.showDay(); } },
        { "12:00–13:00", [](Viewer::App& viewer) { viewer.resetView(); } },
        { "1 minute", [](Viewer::App& viewer) { viewer.resetView(); viewer.zoomAt(Width / 2.0, 1.0 / 60.0); } },
        { "1 second", [](Viewer::App& viewer) { viewer.resetView(); viewer.zoomAt(Width / 2.0, 1.0 / 3600.0); } },
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
        samples.tradesDrawn = app->getViewSummary().count;
        for (std::size_t i = firstMeasured; i < firstMeasured + MeasuredFrames; ++i) {
            const auto& stats = reported[i];
            if (!stats.cpu || !stats.gpu) {
                std::fputs("A measured frame has no CPU or GPU times; the device needs timestamp-query\n", stderr);
                return 1;
            }
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

    std::ofstream table(argv[1]);
    const auto print = [&table](const std::string& text) { table << text; };
    const auto& info = app->getInfo();
    print(std::format("GPU: {} ({}, {}). {} × {} pixels; {} frames per view after {} to warm up. "
                      "Milliseconds: median (95th percentile).\n\n",
                      info.device, info.backend, info.adapter, Width, Height, MeasuredFrames, WarmUpFrames));
    print("| View | Trades drawn | View update (CPU) | Acquire (CPU) | Encode (CPU) | Submit (CPU) |");
    for (const auto& pass : results.front().passOrder) {
        print(std::format(" GPU: {} |", pass));
    }
    print(" GPU total |\n|---|--:|--:|--:|--:|--:|");
    for (std::size_t i = 0; i < results.front().passOrder.size(); ++i) {
        print("--:|");
    }
    print("--:|\n");
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& samples = results[i];
        print(std::format("| {} | {} | {} | {} | {} | {} |", cases[i].name, samples.tradesDrawn, describe(samples.viewUpdate),
                          describe(samples.acquire), describe(samples.encode), describe(samples.submit)));
        for (const auto& pass : results.front().passOrder) {
            const auto found = samples.passes.find(pass);
            print(std::format(" {} |", found != samples.passes.end() ? describe(found->second) : "–"));
        }
        print(std::format(" {} |\n", describe(samples.gpuTotal)));
    }
    if (!table) {
        std::fputs(std::format("Can't write {}\n", argv[1]).c_str(), stderr);
        return 1;
    }
    return 0;
}
