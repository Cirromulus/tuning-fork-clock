#pragma once

#include <stddef.h>
#include <cmath>
#include <optional>

template <size_t PolyCount>
class PolynomCalc
{
public:
    static constexpr size_t numberOfPolynoms = PolyCount;

    constexpr PolynomCalc(const std::array<double, PolyCount>& polynoms)
        : mPolynoms{polynoms}
    {}

    constexpr
    double
    calculate(const double& parameter) const
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
    Damper(const double& dampFactor) :
        mRollingEstimate{std::nullopt},
        mCurrentDiff{0},
        mDampFactor{dampFactor}
    {
    }

    constexpr
    void
    consumeNextCycle(const double& value)
    {
        const double previousValue = mRollingEstimate.value_or(value);
        mCurrentDiff = value - previousValue;
        mRollingEstimate = previousValue + mCurrentDiff * mDampFactor;
    }

    constexpr
    auto
    getEstimate() const
    {
        return mRollingEstimate.value_or(0);
    }

    constexpr
    auto
    getCurrentDiff() const
    {
        return mCurrentDiff;
    }

private:
    std::optional<double> mRollingEstimate;
    double mCurrentDiff;
    double mDampFactor;
};

template <
    typename PeriodEstimatorType,   // TODO: Contract or smthng
    typename ErrorEstimatorType,
    typename DamperType>
class Estimator
{
public:
    constexpr
    Estimator(const PeriodEstimatorType& pe,
              const ErrorEstimatorType& ee,
              const DamperType& da) :
        mPeriodEstimator{pe},
        mErrorEstimator{ee},
        mDamper{da}
    {}

    /**
     * @return Delta Time in microseconds
     */

    template <typename MeasurementType>
    constexpr
    uint64_t
    consumeNextMeasurement(const MeasurementType& temperatureMeasurement)
    {
        mDamper.consumeNextCycle(temperatureMeasurement);

        const double estimatedTemperature_cdg = mDamper.getEstimate();
        const double estimatedTemperatureGradient_cdg = mDamper.getCurrentDiff();

        const double estimatedPeriod_us = mPeriodEstimator.calculate(estimatedTemperature_cdg);
        const double estimatedErrorCorrection_us = mErrorEstimator.calculate(estimatedTemperatureGradient_cdg);

        const double estimatedCorrectedPeriod_us = estimatedPeriod_us + estimatedErrorCorrection_us;

        return llround(estimatedCorrectedPeriod_us);
    }

    /**
     * Used for logging purposes
     */
    constexpr
    auto
    getEstimatedForkTemperature() const
    {
        return mDamper.getEstimate();
    }

private:
    PeriodEstimatorType mPeriodEstimator;
    ErrorEstimatorType mErrorEstimator;
    Damper mDamper;
};