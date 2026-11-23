// Measures what showing trades costs at several zoom levels, offscreen, and
// writes a Markdown table: the CPU time to update the view, the CPU phases of
// a frame and the GPU time of each pass. CI publishes it on each run's page.
//
// Beside the table, <table>.json holds the numbers tools/check-regressions.mjs
// checks: times to load and show the trades and each view's medians, compared
// with another commit's on the same machine, and the memory the synthetic day
// takes, the same on every machine, compared with tools/regression-baseline.csv.
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
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
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
    bool summaries;  // whether crowded views may be summarized: each case says, so none depends on the one before
    void (*show)(Viewer::App& app);
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
    if (values.empty())
    {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto rank = static_cast<std::size_t>(share * static_cast<double>(values.size()) + 0.999999);
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

/** "median (95th percentile)". */
std::string describe(const std::vector<double>& values)
{
    if (values.empty())
    {
        return "–";
    }
    return std::format("{:.3f} ({:.3f})", getPercentile(values, 0.5), getPercentile(values, 0.95));
}

/** A number compared between commits (see tools/check-regressions.mjs). */
struct Metric
{
    std::string key;  // a CSV column: letters, digits and underscores
    std::string label;
    std::string unit;  // "bytes", compared exactly; "ms", within a margin
    double value;
    // Too unsteady between runs to fail a build on: recorded and shown, never checked.
    bool flaky = false;
};

/** The command line: BenchmarkApp <table.md> [trades.csv[.gz]] [--commit <sha>]. */
struct Options
{
    std::string tablePath;
    std::optional<std::string> tradesPath;  // the synthetic day without
    std::optional<std::string> commit;
};

std::optional<Options> parseOptions(std::span<char* const> args)
{
    std::vector<std::string> paths;
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (std::string_view(args[i]) == "--commit" && i + 1 < args.size())
        {
            options.commit = args[++i];
        }
        else
        {
            paths.emplace_back(args[i]);
        }
    }
    if (paths.empty() || paths.size() > 2)
    {
        return std::nullopt;
    }
    options.tablePath = paths[0];
    if (paths.size() == 2)
    {
        options.tradesPath = paths[1];
    }
    return options;
}

/** How long `work` takes, in milliseconds. */
template<typename Work>
double timeMs(Work work)
{
    const auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

Core::Data::Trades takeTrades(Core::Data::TradeParser::Finished&& parsed)
{
    return std::move(parsed.trades);
}

/** An app drawing offscreen at the benchmark's size and a pixel ratio of 1, or null, with why printed. */
std::unique_ptr<Viewer::App> createApp()
{
    const Gpu::Market::Engine::Params params{ .target = Gpu::Type::Target::Offscreen{ .width = WidthPx, .height = HeightPx } };
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create(params, [&](Viewer::App::Result result) {
        if (!result)
        {
            std::fputs(std::format("{}\n", result.error()).c_str(), stderr);
            return;
        }
        app = std::move(*result);
    });
    if (app)
    {
        app->setSize(WidthPx, HeightPx);
        app->setPixelRatio(1.0f);
    }
    return app;
}

/**
 * What the synthetic day takes in memory, with the whole day in view so
 * column summaries are counted: the same on every machine. Nothing if no app
 * can be created.
 */
std::optional<std::vector<Metric>> measureMemory()
{
    const auto app = createApp();
    if (!app || !app->setTrades(Viewer::generateSyntheticDay()))
    {
        return std::nullopt;
    }
    app->setView(Core::getUtcDay(app->getTrades().timeUs.front()));
    const auto memory = app->getMemoryStats();
    const auto metric = [](std::string key, std::string label, std::uint64_t bytes) {
        return Metric{ .key = std::move(key), .label = std::move(label), .unit = "bytes", .value = static_cast<double>(bytes) };
    };
    return std::vector<Metric>{
        metric("memory_trades_bytes", "Memory: trades (synthetic day)", memory.tradesBytes),
        metric("memory_totals_bytes", "Memory: running totals", memory.totalsBytes),
        metric("memory_price_tree_bytes", "Memory: price tree", memory.priceTreeBytes),
        metric("memory_summaries_bytes", "Memory: column summaries (whole day)", memory.summariesBytes),
        metric("memory_gpu_bytes", "Memory: GPU buffers and textures", memory.gpuBytes),
    };
}

/**
 * The CPU's model and how many threads it runs: times depend on both,
 * lavapipe's GPU times too.
 */
std::string describeCpu()
{
    std::string model = "an unknown CPU";
#ifdef __linux__
    // Named in /proc/cpuinfo.
    std::ifstream cpuinfo("/proc/cpuinfo");
    for (std::string line; std::getline(cpuinfo, line);)
    {
        if (line.starts_with("model name"))
        {
            const auto start = line.find_first_not_of(" \t:", line.find(':'));
            if (start != std::string::npos)
            {
                model = line.substr(start);
            }
            break;
        }
    }
#else
#error "describeCpu() reads the CPU's model on Linux only"
#endif
    return std::format("{} · {} threads", model, std::thread::hardware_concurrency());
}

/** Waits until `count` frames have been reported; false if that takes 10 s. */
bool waitForStats(Viewer::App& app, const std::vector<Gpu::Type::FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline)
    {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return reported.size() >= count;
}

void showWholeDay(Viewer::App& app)
{
    app.setView(Core::getUtcDay(app.getTrades().timeUs.front()));
}

void showHour(Viewer::App& app)
{
    if (const auto initialView = app.getInitialView())
    {
        app.setView(*initialView);
    }
}

void showMinute(Viewer::App& app)
{
    showHour(app);
    app.zoomAt(WidthPx / 2.0, 1.0 / 60.0);
}

void showSecond(Viewer::App& app)
{
    showHour(app);
    app.zoomAt(WidthPx / 2.0, 1.0 / 3600.0);
}

const std::vector<Case>& getCases()
{
    static const std::vector<Case> Cases{
        { .name = "Whole day, every trade", .key = "day_every_trade", .summaries = false, .show = showWholeDay },
        { .name = "Whole day", .key = "day", .summaries = true, .show = showWholeDay },
        { .name = "12:00–13:00", .key = "hour", .summaries = true, .show = showHour },
        { .name = "1 minute", .key = "minute", .summaries = true, .show = showMinute },
        { .name = "1 second", .key = "second", .summaries = true, .show = showSecond },
    };
    return Cases;
}

/** Adds a measured frame's times to `samples`. False, with why printed, if it has none. */
bool addFrame(Samples& samples, const Gpu::Type::FrameStats& stats)
{
    if (!stats.cpu || !stats.gpu)
    {
        std::fputs("A measured frame has no CPU or GPU times; the device needs timestamp-query\n", stderr);
        return false;
    }
    samples.cpuFrame.push_back(stats.cpu->acquireMs + stats.cpu->encodeMs + stats.cpu->submitMs);
    samples.acquire.push_back(stats.cpu->acquireMs);
    samples.encode.push_back(stats.cpu->encodeMs);
    samples.submit.push_back(stats.cpu->submitMs);
    samples.gpuTotal.push_back(stats.gpu->totalMs);
    for (const auto& pass : stats.gpu->passes)
    {
        if (!samples.passes.contains(pass.name))
        {
            samples.passOrder.push_back(pass.name);
        }
        samples.passes[pass.name].push_back(pass.gpuMs);
    }
    return true;
}

/**
 * Shows `benchmark`'s view and draws it, frame after frame, with `reported`
 * receiving `app`'s frame stats. Nothing, with why printed, if a frame's
 * times don't arrive.
 */
std::optional<Samples> measure(Viewer::App& app, const Case& benchmark, const std::vector<Gpu::Type::FrameStats>& reported)
{
    Samples samples;
    const std::size_t firstMeasured = reported.size() + WarmUpFrames;
    for (int frame = 0; frame < WarmUpFrames + MeasuredFrames; ++frame)
    {
        const double updateMs = timeMs([&] {
            app.setSummariesAllowed(benchmark.summaries);
            benchmark.show(app);
        });
        if (frame >= WarmUpFrames)
        {
            samples.viewUpdate.push_back(updateMs);
        }
        app.render();
        // One frame at a time: each is measured alone, and the profiler always has a free slot.
        if (!waitForStats(app, reported, reported.size() + 1))
        {
            std::fputs("A frame's GPU times never arrived\n", stderr);
            return std::nullopt;
        }
    }
    samples.tradesDrawn = app.getView().getStats().count;
    for (std::size_t i = firstMeasured; i < firstMeasured + MeasuredFrames; ++i)
    {
        if (!addFrame(samples, reported[i]))
        {
            return std::nullopt;
        }
    }
    return samples;
}

/** Every pass any view drew, in order: crowded views summarize trades in columns. */
std::vector<std::string> getPassNames(const std::vector<Samples>& results)
{
    std::vector<std::string> names;
    for (const auto& samples : results)
    {
        for (const auto& pass : samples.passOrder)
        {
            if (std::ranges::find(names, pass) == names.end())
            {
                names.push_back(pass);
            }
        }
    }
    return names;
}

/** The Markdown table of `results`, a row per case. False, with why printed, if it can't be written. */
bool writeTable(const std::string& path, const Gpu::Type::Info& gpu, const std::vector<Samples>& results)
{
    const auto passNames = getPassNames(results);
    std::ofstream table(path);
    table << std::format("GPU: {} ({}, {}). {} × {} pixels; {} frames per view after {} to warm up. "
                         "Milliseconds: median (95th percentile).\n\n",
                         gpu.device, gpu.backend, gpu.adapter, WidthPx, HeightPx, MeasuredFrames, WarmUpFrames);
    table << "| View | Trades drawn | View update (CPU) | Acquire (CPU) | Encode (CPU) | Submit (CPU) |";
    for (const auto& pass : passNames)
    {
        table << std::format(" GPU: {} |", pass);
    }
    table << " GPU total |\n|---|--:|--:|--:|--:|--:|";
    for (std::size_t i = 0; i < passNames.size(); ++i)
    {
        table << "--:|";
    }
    table << "--:|\n";
    const auto& cases = getCases();
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& samples = results[i];
        table << std::format("| {} | {} | {} | {} | {} | {} |", cases[i].name, samples.tradesDrawn, describe(samples.viewUpdate),
                             describe(samples.acquire), describe(samples.encode), describe(samples.submit));
        for (const auto& pass : passNames)
        {
            const auto found = samples.passes.find(pass);
            table << std::format(" {} |", found != samples.passes.end() ? describe(found->second) : "–");
        }
        table << std::format(" {} |\n", describe(samples.gpuTotal));
    }
    if (!table)
    {
        std::fputs(std::format("Can't write {}\n", path).c_str(), stderr);
        return false;
    }
    return true;
}

/** Each case's medians, compared between commits. */
std::vector<Metric> getViewMetrics(const std::vector<Samples>& results)
{
    std::vector<Metric> metrics;
    const auto& cases = getCases();
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& samples = results[i];
        const auto& name = cases[i].name;
        const auto& key = cases[i].key;
        metrics.push_back({ .key = key + "_update_ms",
                            .label = name + ": view update (CPU)",
                            .unit = "ms",
                            .value = getPercentile(samples.viewUpdate, 0.5) });
        metrics.push_back(
            { .key = key + "_frame_cpu_ms", .label = name + ": frame (CPU)", .unit = "ms", .value = getPercentile(samples.cpuFrame, 0.5) });
        metrics.push_back(
            { .key = key + "_frame_gpu_ms", .label = name + ": frame (GPU)", .unit = "ms", .value = getPercentile(samples.gpuTotal, 0.5) });
    }
    return metrics;
}

/** `metrics` and what measured them, beside the table. False, with why printed, if it can't be written. */
bool writeJson(const std::string& tablePath, const std::vector<Metric>& metrics, const Gpu::Type::Info& gpu, std::size_t trades,
               const std::optional<std::string>& commit)
{
    auto list = nlohmann::ordered_json::array();
    for (const auto& metric : metrics)
    {
        list.push_back({
            { "key", metric.key },
            { "label", metric.label },
            { "unit", metric.unit },
            { "value", metric.value },
            { "flaky", metric.flaky },
        });
    }
    nlohmann::ordered_json json;
    // Its own baseline: the web's memory differs (32-bit), and so do its times.
    json["platform"] = "native";
    json["gpu"] = std::format("{} · {} · {}", gpu.device, gpu.backend, gpu.adapter);
    json["cpu"] = describeCpu();
    json["trades"] = trades;
    json["metrics"] = std::move(list);
    if (commit)
    {
        json["commit"] = *commit;
    }
    const auto path = std::filesystem::path(tablePath).replace_extension(".json");
    if (!(std::ofstream(path) << json.dump(2) << '\n'))
    {
        std::fputs(std::format("Can't write {}\n", path.string()).c_str(), stderr);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(std::span<char* const>(argv + 1, static_cast<std::size_t>(argc - 1)));
    if (!options)
    {
        std::fputs("Usage: BenchmarkApp <table.md> [trades.csv[.gz]] [--commit <sha>]\n", stderr);
        return 2;
    }

    std::expected<Core::Data::Trades, std::string> trades;
    const double loadMs = timeMs([&] {
        trades =
            options->tradesPath ? Core::Data::readTradeFile(*options->tradesPath).transform(takeTrades) : Viewer::generateSyntheticDay();
    });
    if (!trades)
    {
        std::fputs(std::format("{}\n", trades.error()).c_str(), stderr);
        return 1;
    }

    const auto app = createApp();
    if (!app)
    {
        return 1;
    }
    std::expected<void, std::string> set;
    const double setTradesMs = timeMs([&] { set = app->setTrades(std::move(*trades)); });
    if (!set)
    {
        Core::Log::error("{}", set.error());
        return 1;
    }

    std::vector<Gpu::Type::FrameStats> reported;
    app->setStatsCallback([&reported](const Gpu::Type::FrameStats& stats) { reported.push_back(stats); });
    app->setProfiling(true);
    std::vector<Samples> results;
    for (const auto& benchmark : getCases())
    {
        auto samples = measure(*app, benchmark, reported);
        if (!samples)
        {
            return 1;
        }
        results.push_back(std::move(*samples));
    }
    if (!writeTable(options->tablePath, app->getInfo(), results))
    {
        return 1;
    }

    const auto memory = measureMemory();
    if (!memory)
    {
        std::fputs("Can't measure the synthetic day's memory\n", stderr);
        return 1;
    }
    std::vector<Metric> metrics = *memory;
    metrics.push_back({ .key = "load_ms", .label = "Reading the trades", .unit = "ms", .value = loadMs });
    metrics.push_back({ .key = "set_trades_ms", .label = "Showing them: derived data and upload", .unit = "ms", .value = setTradesMs });
    std::ranges::move(getViewMetrics(results), std::back_inserter(metrics));
    return writeJson(options->tablePath, metrics, app->getInfo(), app->getTrades().size(), options->commit) ? 0 : 1;
}
