#include "Core/Plot/Sizes.hpp"

#include <algorithm>
#include <cmath>

namespace Core::Plot {

namespace {

// The smallest amount a marker scale reaches: what the shader's 32-bit floats can hold, ratio included.
constexpr double MarkerFitMinAmount = 1e-30;

}  // namespace

float MarkerSizing::getRadiusCssPx(double amount) const
{
    if (!(amount > 0.0))
    {
        return MarkerMinRadiusCssPx;
    }
    double radiusCssPx = 0.0;
    if (scale == MarkerScale::Area)
    {
        radiusCssPx = MarkerMaxRadiusCssPx * std::sqrt(amount / highAmount);
    }
    else
    {
        const double along = std::log2(amount / lowAmount) / std::log2(highAmount / lowAmount);
        radiusCssPx = std::lerp(static_cast<double>(MarkerMinRadiusCssPx), static_cast<double>(MarkerMaxRadiusCssPx), along);
    }
    return static_cast<float>(
        std::clamp(radiusCssPx, static_cast<double>(MarkerMinRadiusCssPx), static_cast<double>(MarkerMaxRadiusCssPx)));
}

MarkerSizing fitMarkerSizing(MarkerScale scale, const Data::Trades& trades)
{
    if (trades.empty())
    {
        return { .scale = scale };
    }
    const auto amounts = Data::getAmountPercentiles(trades, MarkerFitLowShare, MarkerFitHighShare);
    // Within what the shader's 32-bit floats can hold, ratio included, and at
    // least a tenfold span, so the log scale has room even for a single amount.
    const double high = std::max(amounts.high, MarkerFitMinAmount);
    const double lowest = std::max(high * MarkerFitMinAmount, MarkerFitMinAmount / 10.0);
    return { .scale = scale, .lowAmount = std::clamp(amounts.low, lowest, high / 10.0), .highAmount = high };
}

}  // namespace Core::Plot
