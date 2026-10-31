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

/** The amount of BTC that `scale` draws with `radius`, between MarkerMinRadius and MarkerMaxRadius. */
double getAmountAt(float radius, Type::MarkerScale scale)
{
    if (scale == Type::MarkerScale::Area) {
        const double ratio = static_cast<double>(radius) / MarkerAreaRadiusAt1Btc;
        return ratio * ratio;
    }
    const double along = static_cast<double>(radius - MarkerMinRadius) / (MarkerMaxRadius - MarkerMinRadius);
    return MarkerLogLowest * std::pow(static_cast<double>(MarkerLogHighest) / MarkerLogLowest, along);
}

} // namespace

float getMarkerRadius(double amount, Type::MarkerScale scale)
{
    if (!(amount > 0.0)) {
        return MarkerMinRadius;
    }
    double radius = 0.0;
    if (scale == Type::MarkerScale::Area) {
        radius = MarkerAreaRadiusAt1Btc * std::sqrt(amount);
    } else {
        const double along = std::log2(amount / MarkerLogLowest) / std::log2(static_cast<double>(MarkerLogHighest) / MarkerLogLowest);
        radius = std::lerp(static_cast<double>(MarkerMinRadius), static_cast<double>(MarkerMaxRadius), along);
    }
    return static_cast<float>(std::clamp(radius, static_cast<double>(MarkerMinRadius), static_cast<double>(MarkerMaxRadius)));
}

Type::Legend getLegend(Type::MarkerScale scale, const Type::Theme& theme)
{
    Type::Legend legend{ .buy = theme.buy, .sell = theme.sell, .opacity = theme.markerOpacity, .sizes = {} };
    const double smallest = getAmountAt(MarkerMinRadius, scale);
    const double largest = getAmountAt(MarkerMaxRadius, scale);

    legend.sizes.push_back({ .amount = smallest, .radius = MarkerMinRadius, .bound = Type::LegendSize::Bound::AtMost });
    const auto references = scale == Type::MarkerScale::Area ? std::span<const double>(AreaReferences)
                                                             : std::span<const double>(LogReferences);
    for (const double amount : references) {
        if (amount > smallest && amount < largest) {
            legend.sizes.push_back({ .amount = amount, .radius = getMarkerRadius(amount, scale), .bound = Type::LegendSize::Bound::Exact });
        }
    }
    legend.sizes.push_back({ .amount = largest, .radius = MarkerMaxRadius, .bound = Type::LegendSize::Bound::AtLeast });
    return legend;
}

} // namespace Gpu::Market::Style
