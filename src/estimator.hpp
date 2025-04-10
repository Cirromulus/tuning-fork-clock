#pragma once

#include <stddef.h>
#include <cmath>
#include <optional>

template <size_t PolyCount>
class CompensationEstimator
{
public:
    static constexpr size_t numberOfPolynoms = PolyCount;

    constexpr CompensationEstimator(const std::array<double, PolyCount>& polynoms)
        : mPolynoms{polynoms}
    {}

    constexpr
    double
    estimate(const double& parameter) const
    {
        double sum = mPolynoms[0];
        for (size_t i = 1; i < mPolynoms.size(); i++)
        {
            sum += mPolynoms[i] * std::pow(parameter, i);
        }
        return sum;
    }
private:
    std::array<double, numberOfPolynoms> mPolynoms;
};


struct Damper
{
    constexpr
    Damper(const double& dampFactor)
        : mRollingEstimate{std::nullopt}, mDampFactor{dampFactor}
    {
    }

    constexpr
    void
    consumeNextCycle(const double& value)
    {
        const double previousValue = mRollingEstimate.value_or(value);
        const double diff = value - previousValue;
        mRollingEstimate = previousValue + diff * mDampFactor;
    }

    constexpr
    double
    getEstimate() const
    {
        return mRollingEstimate.value_or(0);
    }


private:
    std::optional<double> mRollingEstimate;
    double mDampFactor;
};