#include "Core/Data/Synthetic.hpp"

#include <algorithm>
#include <cmath>

namespace Core::Data {

namespace {

constexpr double TradesPerSecond = 5.0;
constexpr double StartPrice = 7500.0;
constexpr double MaxPriceStep = 1e-4;  // per trade, relative
constexpr double MeanAmount = 0.14;  // close to the real day's

/** SplitMix64: small, and the same on every platform, unlike the standard distributions. */
class Random
{
public:
    explicit Random(std::uint64_t seed)
        : mState(seed)
    {}

    /** Uniform in (0, 1], so its logarithm is finite. */
    double uniform()
    {
        std::uint64_t z = (mState += 0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
        z ^= z >> 31;
        return static_cast<double>((z >> 11) + 1) * 0x1.0p-53;
    }

    /** Exponentially distributed with the given mean. */
    double exponential(double mean) { return -std::log(uniform()) * mean; }

private:
    std::uint64_t mState;
};

} // namespace

Trades generateSyntheticTrades(std::int64_t startUs, std::int64_t endUs, std::uint64_t seed)
{
    Random random(seed);
    Trades trades;
    double price = StartPrice;
    std::int64_t timeUs = startUs;
    while (true) {
        // Exponential gaps: trades arrive independently of each other.
        timeUs += static_cast<std::int64_t>(random.exponential(1e6 / TradesPerSecond));
        if (timeUs >= endUs) {
            return trades;
        }
        price *= 1.0 + (2.0 * random.uniform() - 1.0) * MaxPriceStep;
        const double amount = std::round(random.exponential(MeanAmount) * 1e6) / 1e6;

        trades.timeUs.push_back(timeUs);
        trades.price.push_back(std::round(price * 100.0) / 100.0);
        trades.amount.push_back(std::max(amount, 1e-6));
        trades.side.push_back(random.uniform() <= 0.5 ? Side::Buy : Side::Sell);
    }
}

} // namespace Core::Data
