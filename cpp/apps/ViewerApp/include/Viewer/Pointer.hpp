#pragma once

#include "Core/Data/Columns.hpp"
#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"
#include "Gpu/Market/Style.hpp"
#include "Viewer/Layout.hpp"
#include "Viewer/Summaries.hpp"
#include "Viewer/View.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace Viewer {

/** The pointer, and the trade or column summary under it. */
class Pointer
{
public:
    struct Picked
    {
        struct Trade
        {
            std::size_t index;
            std::int64_t timeUs;
            double price;
            double amount;
            Core::Data::Side side;
            double centerXPx;
            double centerYPx;
        };

        struct Column
        {
            Core::Data::ColumnSummary summary;
            Core::TimeRange time;  // the time the column covers: start included, end not
            double centerXPx;
            double topPx;
            double bottomPx;
        };

        using Variant = std::variant<std::monostate, Trade, Column>;
    };

    [[nodiscard]] const Picked::Variant& getPicked() const { return mPicked; }
    [[nodiscard]] std::optional<std::size_t> getHighlightedTrade() const;
    [[nodiscard]] std::optional<std::size_t> getHighlightedColumn() const { return mColumnPosition; }

    void setPosition(double xPx, double yPx);

    void clear();
    void update(const Core::Data::PreprocessedTrades& data, const View& view, const Summaries& summaries, const Layout& layout,
                const Gpu::Market::Style::MarkerSizing& markerSizing);

private:
    bool mOnSurface = false;
    double mXPx = 0.0;
    double mYPx = 0.0;
    Picked::Variant mPicked;
    std::optional<std::size_t> mColumnPosition;
};

}  // namespace Viewer
