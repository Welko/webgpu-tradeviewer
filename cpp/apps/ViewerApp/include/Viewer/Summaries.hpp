#pragma once

#include "Core/Data/Columns.hpp"
#include "Core/Data/PreprocessedTrades.hpp"
#include "Gpu/Market/Type.hpp"
#include "Viewer/Layout.hpp"
#include "Viewer/View.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Viewer {

/** Column summaries, shown instead of a view's trades when there are too many of them to draw. */
class Summaries
{
public:
    [[nodiscard]] std::uint32_t getNumColumns() const { return mAcross; }
    [[nodiscard]] const std::vector<Core::Data::ColumnSummary>& getColumns() const { return mColumns; }
    [[nodiscard]] std::optional<Gpu::Market::Type::Columns> getEngineColumns() const;
    [[nodiscard]] std::size_t getBytes() const;

    void setAllowed(bool allowed) { mAllowed = allowed; }
    void setThreshold(double tradesPerColumn) { mThreshold = tradesPerColumn; }

    bool update(const Core::Data::PreprocessedTrades& data, const View& view, const Layout& layout);

private:
    bool mAllowed = true;
    double mThreshold = 32.0;
    std::uint32_t mAcross = 0;
    std::vector<Core::Data::ColumnSummary> mColumns;
};

}  // namespace Viewer
