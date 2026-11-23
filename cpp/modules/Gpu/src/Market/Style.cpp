#include "Gpu/Market/Style.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Gpu::Market::Style {

namespace {

/** The nearest of 1, 2 and 5 times a power of ten to `amount`, by ratio, for a legend. */
double roundNicely(double amount)
{
    const double power = std::pow(10.0, std::floor(std::log10(amount)));
    double nearest = power;
    for (const double step : { 2.0, 5.0, 10.0 })
    {
        if (std::abs(std::log(step * power / amount)) < std::abs(std::log(nearest / amount)))
        {
            nearest = step * power;
        }
    }
    return nearest;
}

/** The amount that `sizing` draws with `radiusCssPx`, from Core::Plot::MarkerMinRadiusCssPx to Core::Plot::MarkerMaxRadiusCssPx. */
double getAmountAt(float radiusCssPx, const Core::Plot::MarkerSizing& sizing)
{
    if (sizing.scale == Core::Plot::MarkerScale::Area)
    {
        const double ratio = static_cast<double>(radiusCssPx) / Core::Plot::MarkerMaxRadiusCssPx;
        return sizing.highAmount * ratio * ratio;
    }
    const double along = static_cast<double>(radiusCssPx - Core::Plot::MarkerMinRadiusCssPx)
                         / (Core::Plot::MarkerMaxRadiusCssPx - Core::Plot::MarkerMinRadiusCssPx);
    return sizing.lowAmount * std::pow(sizing.highAmount / sizing.lowAmount, along);
}

/** Round amounts for a legend to show between `smallest` and `largest`, smallest first. */
std::vector<double> getReferences(const Core::Plot::MarkerSizing& sizing, double smallest, double largest)
{
    std::vector<double> references;
    if (sizing.scale == Core::Plot::MarkerScale::Area)
    {
        // Each four times the one before, so twice its radius.
        for (const double share : { 1.0 / 64.0, 1.0 / 16.0, 1.0 / 4.0 })
        {
            references.push_back(roundNicely(sizing.highAmount * share));
        }
    }
    else
    {
        // Powers of ten; only every second one or fewer, if that's more than three.
        const auto first = static_cast<int>(std::ceil(std::log10(smallest)));
        const auto last = static_cast<int>(std::floor(std::log10(largest)));
        const int stride = std::max(1, (last - first + 3) / 3);
        for (int exponent = first; exponent <= last; ++exponent)
        {
            if (exponent % stride == 0)
            {
                references.push_back(std::pow(10.0, exponent));
            }
        }
    }
    const auto inside = [&](double amount) { return amount > smallest * 1.0001 && amount < largest * 0.9999; };
    std::erase_if(references, [&](double amount) { return !inside(amount); });
    // A scale of a decade or less may have no power of ten inside: one round amount from its middle.
    if (const double middle = roundNicely(std::sqrt(smallest * largest)); references.empty() && inside(middle))
    {
        references.push_back(middle);
    }
    return references;
}

}  // namespace

Type::Legend getLegend(const Core::Plot::MarkerSizing& sizing, const Type::Theme& theme)
{
    Type::Legend legend{ .buy = theme.buy, .sell = theme.sell, .opacity = theme.markerOpacity, .sizes = {} };
    const double smallest = getAmountAt(Core::Plot::MarkerMinRadiusCssPx, sizing);
    const double largest = getAmountAt(Core::Plot::MarkerMaxRadiusCssPx, sizing);

    legend.sizes.push_back(
        { .amount = smallest, .radiusCssPx = Core::Plot::MarkerMinRadiusCssPx, .bound = Type::LegendSize::Bound::AtMost });
    for (const double amount : getReferences(sizing, smallest, largest))
    {
        legend.sizes.push_back({
            .amount = amount,
            .radiusCssPx = sizing.getRadiusCssPx(amount),
            .bound = Type::LegendSize::Bound::Exact,
        });
    }
    legend.sizes.push_back(
        { .amount = largest, .radiusCssPx = Core::Plot::MarkerMaxRadiusCssPx, .bound = Type::LegendSize::Bound::AtLeast });
    return legend;
}

}  // namespace Gpu::Market::Style
