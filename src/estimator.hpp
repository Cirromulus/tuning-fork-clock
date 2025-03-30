#pragma once

#include <stddef.h>
#include <cmath>

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