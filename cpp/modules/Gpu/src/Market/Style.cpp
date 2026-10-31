#include "Gpu/Market/Style.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace Gpu::Market::Style {

namespace {

// Round amounts for a legend to show between its smallest and largest sizes.
constexpr std::array AreaReferences{ 0.25, 1.0, 4.0 };  // each doubles the radius
constexpr std::array LogReferences{ 0.01, 1.0 };  // every second power of ten

/** The amount of BTC that `scale` draws with `radiusCssPx`, from MarkerMinRadiusCssPx to MarkerMaxRadiusCssPx. */
double getAmountAt(float radiusCssPx, Type::MarkerScale scale)
{
    if (scale == Type::MarkerScale::Area) {
        const double ratio = static_cast<double>(radiusCssPx) / MarkerAreaRadiusAt1BtcCssPx;
        return ratio * ratio;
    }
    const double along = static_cast<double>(radiusCssPx - MarkerMinRadiusCssPx) / (MarkerMaxRadiusCssPx - MarkerMinRadiusCssPx);
    return MarkerLogLowest * std::pow(static_cast<double>(MarkerLogHighest) / MarkerLogLowest, along);
}

} // namespace

float getMarkerRadiusCssPx(double amount, Type::MarkerScale scale)
{
    if (!(amount > 0.0)) {
        return MarkerMinRadiusCssPx;
    }
    double radiusCssPx = 0.0;
    if (scale == Type::MarkerScale::Area) {
        radiusCssPx = MarkerAreaRadiusAt1BtcCssPx * std::sqrt(amount);
    } else {
        const double along
            = std::log2(amount / MarkerLogLowest) / std::log2(static_cast<double>(MarkerLogHighest) / MarkerLogLowest);
        radiusCssPx = std::lerp(static_cast<double>(MarkerMinRadiusCssPx), static_cast<double>(MarkerMaxRadiusCssPx), along);
    }
    return static_cast<float>(
        std::clamp(radiusCssPx, static_cast<double>(MarkerMinRadiusCssPx), static_cast<double>(MarkerMaxRadiusCssPx)));
}

Type::Legend getLegend(Type::MarkerScale scale, const Type::Theme& theme)
{
    Type::Legend legend{ .buy = theme.buy, .sell = theme.sell, .opacity = theme.markerOpacity, .sizes = {} };
    const double smallest = getAmountAt(MarkerMinRadiusCssPx, scale);
    const double largest = getAmountAt(MarkerMaxRadiusCssPx, scale);

    legend.sizes.push_back({ .amount = smallest, .radiusCssPx = MarkerMinRadiusCssPx, .bound = Type::LegendSize::Bound::AtMost });
    const auto references = scale == Type::MarkerScale::Area ? std::span<const double>(AreaReferences)
                                                             : std::span<const double>(LogReferences);
    for (const double amount : references) {
        if (amount > smallest && amount < largest) {
            legend.sizes.push_back({
                .amount = amount,
                .radiusCssPx = getMarkerRadiusCssPx(amount, scale),
                .bound = Type::LegendSize::Bound::Exact,
            });
        }
    }
    legend.sizes.push_back({ .amount = largest, .radiusCssPx = MarkerMaxRadiusCssPx, .bound = Type::LegendSize::Bound::AtLeast });
    return legend;
}

} // namespace Gpu::Market::Style
