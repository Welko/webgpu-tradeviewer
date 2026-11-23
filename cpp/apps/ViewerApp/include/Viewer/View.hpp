#pragma once

#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Trades.hpp"
#include "Core/Plot/Pick.hpp"
#include "Core/Range.hpp"
#include "Viewer/Layout.hpp"

namespace Viewer {

/** The time range on show, and the trades in it. */
class View
{
public:
    [[nodiscard]] Core::TimeRange getTimeRange() const { return mTime; }
    [[nodiscard]] Core::ValueRange getPriceRange() const { return mPrice; }
    [[nodiscard]] Core::IndexRange getTrades() const { return mTrades; }
    [[nodiscard]] const Core::Data::TradeStats& getStats() const { return mStats; }
    [[nodiscard]] Core::Plot::Mapping getMapping(const Layout& layout) const;

    void setTimeRange(const Core::Data::PreprocessedTrades& data, Core::TimeRange time);

    void zoomAt(const Core::Data::PreprocessedTrades& data, const Layout& layout, double xPx, double factor);
    void panBy(const Core::Data::PreprocessedTrades& data, const Layout& layout, double dxPx);

private:
    Core::TimeRange mTime{};
    Core::ValueRange mPrice{};
    Core::IndexRange mTrades{};
    Core::Data::TradeStats mStats{};
};

}  // namespace Viewer
