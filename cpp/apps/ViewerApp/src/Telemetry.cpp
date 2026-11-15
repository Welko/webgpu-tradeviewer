#include "Viewer/Telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <utility>

namespace Viewer {

namespace {

constexpr double SummaryPeriodMs = 60'000.0;

// Samples kept per summary: a minute of frames at 60 Hz fits, and more can't grow memory.
constexpr std::size_t MaxSamples = 4096;

#ifdef __EMSCRIPTEN__
constexpr const char* Platform = "web";
#elif defined(__linux__)
constexpr const char* Platform = "native";
#else
#error "Telemetry names Linux and the web only: add this platform's name"
#endif

void addSample(std::vector<double>& samples, double value)
{
    if (samples.size() < MaxSamples) {
        samples.push_back(value);
    }
}

/** The value `share` of the way up `values`, by the nearest-rank method; `values` is reordered. */
double getPercentile(std::vector<double>& values, double share)
{
    const auto rank = static_cast<std::size_t>(std::ceil(share * static_cast<double>(values.size())));
    const auto nth = values.begin() + static_cast<std::ptrdiff_t>(std::clamp<std::size_t>(rank, 1, values.size()) - 1);
    std::ranges::nth_element(values, nth);
    return *nth;
}

/** Rounded to hundredths: finer adds nothing, and fewer distinct values are stored. */
double rounded(double value)
{
    return std::round(value * 100.0) / 100.0;
}

Telemetry::Property property(std::string name, Core::Telemetry::Value value)
{
    return { .name = std::move(name), .value = std::move(value) };
}

Telemetry::Property count(std::string name, std::size_t value)
{
    return property(std::move(name), static_cast<std::int64_t>(value));
}

} // namespace

Telemetry::Telemetry()
    : mClient(Platform, TRADEVIEWER_BUILD, TRADEVIEWER_VERSION)
{}

void Telemetry::setSink(std::unique_ptr<Core::Telemetry::Sink> sink, std::vector<Property> environment, const Gpu::Type::Info& gpu)
{
    mClient.setSink(std::move(sink));
    mView = {};
    mPerformance = {};
    mPeriodStartMs.reset();
    if (!mClient.isEnabled()) {
        return;
    }
    // Browsers often hide the device's name; WebGL's renderer may still say it.
    const bool named = !gpu.description.empty() || !gpu.device.empty();
    environment.push_back(property("gpuVendor", gpu.vendor));
    environment.push_back(property("gpuArchitecture", gpu.architecture));
    environment.push_back(property("gpuName", !gpu.description.empty() ? gpu.description : !gpu.device.empty() ? gpu.device : gpu.webglRenderer));
    environment.push_back(property("gpuNameSource", named ? "webgpu" : gpu.webglRenderer.empty() ? "none" : "webgl"));
    environment.push_back(property("backend", gpu.backend));
    environment.push_back(property("adapter", gpu.adapter));
    mClient.send("session_started", std::move(environment));
}

void Telemetry::recordViewChange(std::int64_t lengthUs, bool reset, bool wholeDay)
{
    if (!isEnabled()) {
        return;
    }
    ++mView.changes;
    mView.resets += reset ? 1 : 0;
    mView.wholeDay = mView.wholeDay || wholeDay;
    addSample(mView.lengthsS, static_cast<double>(lengthUs) / 1e6);
}

void Telemetry::recordPick()
{
    if (isEnabled()) {
        ++mView.picks;
    }
}

void Telemetry::recordFrame(bool summarized, std::size_t visibleTrades)
{
    if (!isEnabled()) {
        return;
    }
    ++mView.frames;
    mView.summarizedFrames += summarized ? 1 : 0;
    addSample(mView.visibleTrades, static_cast<double>(visibleTrades));
}

void Telemetry::recordFrameTimes(const Gpu::Type::FrameStats& stats)
{
    if (!isEnabled()) {
        return;
    }
    auto& performance = mPerformance;
    ++performance.frames;
    performance.gpuBytesPeak = std::max(performance.gpuBytesPeak, stats.gpuBytes);
    if (stats.cpu) {
        addSample(performance.cpuMs, stats.cpu->acquireMs + stats.cpu->encodeMs + stats.cpu->submitMs);
        addSample(performance.encodeMs, stats.cpu->encodeMs);
    }
    if (stats.gpu) {
        addSample(performance.gpuMs, stats.gpu->totalMs);
        for (const auto& pass : stats.gpu->passes) {
            addSample(performance.passMs[pass.name], pass.gpuMs);
        }
        return;
    }
    using Reason = Gpu::Type::FrameStats::UntimedReason;
    switch (stats.gpu.error()) {
    case Reason::Unsupported:
        performance.gpuUnsupported = true;
        break;
    case Reason::NoFreeSlot:
    case Reason::ReadbackFailed:
    case Reason::Overflow:
    case Reason::NotStarted:
        ++performance.timingsLost;
        break;
    case Reason::Undefined:
    case Reason::ProfilingOff:
    case Reason::NotSampled:
        break;
    }
}

void Telemetry::update(double nowMs)
{
    if (!isEnabled()) {
        return;
    }
    if (!mPeriodStartMs) {
        mPeriodStartMs = nowMs;
    }
    if (nowMs - *mPeriodStartMs >= SummaryPeriodMs) {
        sendSummaries();
        mPeriodStartMs = nowMs;
    }
}

void Telemetry::flush()
{
    if (isEnabled()) {
        sendSummaries();
    }
}

void Telemetry::sendSummaries()
{
    auto& view = mView;
    if (view.changes > 0 || view.picks > 0) {
        std::vector<Property> properties{
            count("viewChanges", view.changes),
            count("resets", view.resets),
            count("picks", view.picks),
            property("fullDayUsed", view.wholeDay),
        };
        if (!view.lengthsS.empty()) {
            properties.push_back(property("minVisibleSpanS", rounded(*std::ranges::min_element(view.lengthsS))));
            properties.push_back(property("maxVisibleSpanS", rounded(*std::ranges::max_element(view.lengthsS))));
            properties.push_back(property("medianVisibleSpanS", rounded(getPercentile(view.lengthsS, 0.5))));
        }
        if (view.frames > 0) {
            properties.push_back(property("aggregatedFraction", rounded(static_cast<double>(view.summarizedFrames) / static_cast<double>(view.frames))));
        }
        mClient.send("view_summary", std::move(properties));
    }

    auto& performance = mPerformance;
    if (performance.frames > 0) {
        std::vector<Property> properties{
            count("frames", performance.frames),
            count("gpuFrames", performance.gpuMs.size()),
            count("timingsLost", performance.timingsLost),
            property("gpuUnsupported", performance.gpuUnsupported),
            property("gpuBytesPeak", static_cast<std::int64_t>(performance.gpuBytesPeak)),
            count("droppedEvents", mClient.getDropped()),
        };
        if (!performance.cpuMs.empty()) {
            properties.push_back(property("cpuFrameMsP50", rounded(getPercentile(performance.cpuMs, 0.5))));
            properties.push_back(property("cpuFrameMsP95", rounded(getPercentile(performance.cpuMs, 0.95))));
            properties.push_back(property("cpuFrameMsP99", rounded(getPercentile(performance.cpuMs, 0.99))));
            properties.push_back(property("cpuEncodeMsP95", rounded(getPercentile(performance.encodeMs, 0.95))));
        }
        if (!performance.gpuMs.empty()) {
            properties.push_back(property("gpuMsP50", rounded(getPercentile(performance.gpuMs, 0.5))));
            properties.push_back(property("gpuMsP95", rounded(getPercentile(performance.gpuMs, 0.95))));
            for (auto& [pass, times] : performance.passMs) {
                properties.push_back(property(std::format("gpu_{}_p95", pass), rounded(getPercentile(times, 0.95))));
            }
        }
        if (!view.visibleTrades.empty()) {
            properties.push_back(property("visibleTradesP95", std::round(getPercentile(view.visibleTrades, 0.95))));
        }
        mClient.send("perf_summary", std::move(properties));
    }
    mView = {};
    mPerformance = {};
}

} // namespace Viewer
