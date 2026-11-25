#pragma once

#include "Core/Telemetry.hpp"
#include "Gpu/Type.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Viewer {

/**
 * What the viewer reports, and when (see Telemetry in README.md).
 * App tells it what happens; it sums that up and sends a summary a minute,
 * only while the user agrees. Nothing it sends holds market data: counts,
 * sizes and times only.
 */
class Telemetry
{
public:
    using Property = Core::Telemetry::Property;

    Telemetry();

    void setSink(std::unique_ptr<Core::Telemetry::Sink> sink, std::vector<Property> environment, const Gpu::Type::Info& gpu);

    [[nodiscard]] bool isEnabled() const { return mClient.isEnabled(); }

    [[nodiscard]] std::vector<Property> getCommonProperties() const { return mClient.getCommonProperties(); }

    void recordViewChange(std::int64_t lengthUs, bool reset, bool wholeDay);
    void recordPick();
    void recordFrame(bool summarized, std::size_t visibleTrades);
    void recordFrameTimes(const Gpu::Type::FrameStats& stats);

    void update(double nowMs);
    void flush();

private:
    /** How the view was used, since the last summary. */
    struct ViewUse
    {
        std::size_t changes = 0;
        std::size_t resets = 0;
        std::size_t picks = 0;
        bool wholeDay = false;
        std::vector<double> lengthsS;
        std::size_t frames = 0;
        std::size_t summarizedFrames = 0;
        std::vector<double> visibleTrades;
    };

    /** How fast frames were, since the last summary. */
    struct Performance
    {
        std::size_t frames = 0;
        std::vector<double> cpuMs;  // acquire, encode and submit
        std::vector<double> encodeMs;
        std::vector<double> gpuMs;  // of the frames timed on the GPU
        std::map<std::string, std::vector<double>> passMs;
        std::size_t timingsLost = 0;  // frames whose GPU times went missing
        bool gpuUnsupported = false;
        std::uint64_t gpuBytesPeak = 0;
    };

    void sendSummaries();

    Core::Telemetry::Client mClient;
    ViewUse mView;
    Performance mPerformance;
    std::optional<double> mPeriodStartMs;
};

}  // namespace Viewer
