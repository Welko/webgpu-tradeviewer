#include "Gpu/Market/Marker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace Gpu::Market::Marker {

namespace {

// Round amounts for a legend to show between its smallest and largest sizes.
constexpr std::array AreaReferences{ 0.25, 1.0, 4.0 };  // each doubles the radius
constexpr std::array LogReferences{ 0.01, 1.0 };  // every second power of ten

/** The amount of BTC that `scale` draws with `radius`, between MinRadius and MaxRadius. */
double getAmountAt(float radius, Type::MarkerScale scale)
{
    if (scale == Type::MarkerScale::Area) {
        const double ratio = static_cast<double>(radius) / AreaRadiusAt1Btc;
        return ratio * ratio;
    }
    const double along = static_cast<double>(radius - MinRadius) / (MaxRadius - MinRadius);
    return LogLowest * std::pow(static_cast<double>(LogHighest) / LogLowest, along);
}

} // namespace

float getRadius(double amount, Type::MarkerScale scale)
{
    if (!(amount > 0.0)) {
        return MinRadius;
    }
    double radius = 0.0;
    if (scale == Type::MarkerScale::Area) {
        radius = AreaRadiusAt1Btc * std::sqrt(amount);
    } else {
        const double along = std::log2(amount / LogLowest) / std::log2(static_cast<double>(LogHighest) / LogLowest);
        radius = std::lerp(static_cast<double>(MinRadius), static_cast<double>(MaxRadius), along);
    }
    return static_cast<float>(std::clamp(radius, static_cast<double>(MinRadius), static_cast<double>(MaxRadius)));
}

Type::Legend getLegend(Type::MarkerScale scale, const Type::Theme& theme)
{
    Type::Legend legend{ .buy = theme.buy, .sell = theme.sell, .opacity = theme.markerOpacity, .sizes = {} };
    const double smallest = getAmountAt(MinRadius, scale);
    const double largest = getAmountAt(MaxRadius, scale);

    legend.sizes.push_back({ .amount = smallest, .radius = MinRadius, .bound = Type::LegendSize::Bound::AtMost });
    const auto references = scale == Type::MarkerScale::Area ? std::span<const double>(AreaReferences)
                                                             : std::span<const double>(LogReferences);
    for (const double amount : references) {
        if (amount > smallest && amount < largest) {
            legend.sizes.push_back({ .amount = amount, .radius = getRadius(amount, scale), .bound = Type::LegendSize::Bound::Exact });
        }
    }
    legend.sizes.push_back({ .amount = largest, .radius = MaxRadius, .bound = Type::LegendSize::Bound::AtLeast });
    return legend;
}

} // namespace Gpu::Market::Marker
