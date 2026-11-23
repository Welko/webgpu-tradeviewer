#pragma once

#include "Core/Plot/Axis.hpp"
#include "Gpu/Plot/Type.hpp"
#include "Viewer/Layout.hpp"
#include "Viewer/View.hpp"

#include <vector>

namespace Viewer {

/** The ticks that label the view along the plot's edges, as many as fit. */
class Axes
{
public:
    [[nodiscard]] const std::vector<Core::Plot::Axis::Tick>& getTimes() const { return mTimes; }
    [[nodiscard]] const std::vector<Core::Plot::Axis::Tick>& getPrices() const { return mPrices; }
    [[nodiscard]] Gpu::Plot::Type::Grid getGrid() const;

    void update(const View& view, const Layout& layout);

private:
    std::vector<Core::Plot::Axis::Tick> mTimes;
    std::vector<Core::Plot::Axis::Tick> mPrices;
};

}  // namespace Viewer
