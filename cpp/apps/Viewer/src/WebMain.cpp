#include "App.hpp"

#include "Core/Data/TradeParser.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using emscripten::val;

/** Accepts the WebGPU spelling: "low-power", "high-performance", anything else is the default. */
Gpu::Type::PowerPreference parsePowerPreference(const std::string& value)
{
    if (value == "low-power") {
        return Gpu::Type::PowerPreference::LowPower;
    }
    if (value == "high-performance") {
        return Gpu::Type::PowerPreference::HighPerformance;
    }
    return Gpu::Type::PowerPreference::Default;
}

/** Core::Data::TradeSummary in numbers JavaScript reads exactly, with times in milliseconds for Date. */
struct WebTradeSummary
{
    double count;
    double firstMs;
    double lastMs;
    double totalAmount;
};

/**
 * The object JavaScript holds. JavaScript owns the frame loop and must
 * call delete() when done.
 */
class WebViewer
{
public:
    /** Calls `onDone(error)` once: `null` on success, a string on failure. */
    void init(std::string selector, std::string powerPreference, val onDone)
    {
        std::weak_ptr<std::unique_ptr<Viewer::App>> slot = mApp;
        Viewer::App::create(Gpu::Type::Target::Canvas{ .selector = std::move(selector) }, parsePowerPreference(powerPreference),
            [slot, onDone](Viewer::App::Result result) {
                auto app = slot.lock();
                if (!app) {
                    // delete() was called before initialization finished.
                    return;
                }
                if (!result) {
                    onDone(result.error());
                    return;
                }
                *app = std::move(*result);
                (*app)->setTrades(Viewer::syntheticDay());
                onDone(val::null());
            });
    }

    /** The GPU in use, as a plain JavaScript object. Empty until init() succeeds. */
    Gpu::Type::Info info() const
    {
        return *mApp ? (*mApp)->info() : Gpu::Type::Info{};
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        if (*mApp) {
            (*mApp)->resize(width, height);
        }
    }

    void update(double nowMs)
    {
        if (*mApp) {
            (*mApp)->update(nowMs);
        }
    }

    bool needsRender() const
    {
        return *mApp && (*mApp)->needsRender();
    }

    void render()
    {
        if (*mApp) {
            (*mApp)->render();
        }
    }

    /** The trades on show. Empty until init() succeeds. */
    WebTradeSummary tradeSummary() const
    {
        if (!*mApp) {
            return {};
        }
        const auto summary = Core::Data::summarize((*mApp)->trades());
        return {
            .count = static_cast<double>(summary.count),
            .firstMs = static_cast<double>(summary.firstUs) / 1000.0,
            .lastMs = static_cast<double>(summary.lastUs) / 1000.0,
            .totalAmount = summary.totalAmount,
        };
    }

    /** Starts reading a trades file, which appendChunk() then receives piece by piece. */
    void beginLoad()
    {
        mParser.emplace();
    }

    /** Parses a Uint8Array of the file. An error message, or "" if all is well. */
    std::string appendChunk(const val& bytes)
    {
        if (!mParser) {
            return "beginLoad() wasn't called";
        }
        mChunk.resize(bytes["length"].as<std::size_t>());
        val(emscripten::typed_memory_view(mChunk.size(), mChunk.data())).call<void>("set", bytes);
        auto appended = mParser->append(std::string_view(mChunk.data(), mChunk.size()));
        if (!appended) {
            mParser.reset();
            return appended.error();
        }
        return {};
    }

    /** Shows the file's trades in place of the current ones. An error message, or "" if all is well. */
    std::string finishLoad()
    {
        if (!mParser || !*mApp) {
            return "beginLoad() wasn't called";
        }
        auto trades = mParser->finish();
        mParser.reset();
        if (!trades) {
            return trades.error();
        }
        (*mApp)->setTrades(std::move(*trades));
        return {};
    }

private:
    std::shared_ptr<std::unique_ptr<Viewer::App>> mApp = std::make_shared<std::unique_ptr<Viewer::App>>();
    std::optional<Core::Data::TradeParser> mParser;  // while a file loads
    std::vector<char> mChunk;  // the last chunk, copied out of JavaScript
};

} // namespace

EMSCRIPTEN_BINDINGS(Viewer)
{
    emscripten::value_object<Gpu::Type::Info>("GpuInfo")
        .field("vendor", &Gpu::Type::Info::vendor)
        .field("device", &Gpu::Type::Info::device)
        .field("architecture", &Gpu::Type::Info::architecture)
        .field("description", &Gpu::Type::Info::description)
        .field("backend", &Gpu::Type::Info::backend)
        .field("adapter", &Gpu::Type::Info::adapter)
        .field("webglRenderer", &Gpu::Type::Info::webglRenderer);

    emscripten::value_object<WebTradeSummary>("TradeSummary")
        .field("count", &WebTradeSummary::count)
        .field("firstMs", &WebTradeSummary::firstMs)
        .field("lastMs", &WebTradeSummary::lastMs)
        .field("totalAmount", &WebTradeSummary::totalAmount);

    emscripten::class_<WebViewer>("Viewer")
        .constructor<>()
        .function("init", &WebViewer::init)
        .function("info", &WebViewer::info)
        .function("resize", &WebViewer::resize)
        .function("update", &WebViewer::update)
        .function("needsRender", &WebViewer::needsRender)
        .function("render", &WebViewer::render)
        .function("tradeSummary", &WebViewer::tradeSummary)
        .function("beginLoad", &WebViewer::beginLoad)
        .function("appendChunk", &WebViewer::appendChunk)
        .function("finishLoad", &WebViewer::finishLoad);
}
