#pragma once

// For EnvironmentMeasurement.
// This include is not well for separation of concerns
#include <lib/bme280.hpp>
#include <include/config.hpp>   // for OscCount, which is also weird to include

#include <pico/stdlib.h>
#include <cstdio>
#include <stddef.h>

// what to print.
struct LoggerConfig
{
    bool period = true;
    bool bmeData = true;
    bool estimations = true;
    bool humanReadables = false;
    bool differenceToInternal = true;
    size_t headerEveryNumLines = 100;
    // TODO: Handle for different serials
};

class CSVLogger
{
public:
    constexpr CSVLogger(LoggerConfig const& config = LoggerConfig{})
        : mConfig{config}, mCurrentLine{0}
    {
    }

    void addDataPoint(const OscCount& period_counts,    // unit is "reference counts"
                      const uint64_t& estimatedTime_us,
                      const double& estimatedForkTemp_deg,
                      const int64_t& estimatedDrift_us,
                      const BME280::EnvironmentMeasurement& bmeData)
    {
        // I would like to make that more generic. Enum -> Type & member translation with templates?
        if (mConfig.period)
        {
            printf("%lu", period_counts);
        }
        if (mConfig.bmeData)
        {
            printf(",%ld,%lu,%lu",
                bmeData.temperature_centidegree,
                bmeData.pressure_q23_8,
                bmeData.humidity_q22_10);
        }
        if (mConfig.estimations)
        {
            printf(",%lld,%f", estimatedTime_us, estimatedForkTemp_deg);

        }
        if (mConfig.humanReadables)
        {
            static constexpr double referenceCountsPerSecond = config::referenceClockFrequency * config::periodsPerMeasurement;
            printf(",%f,%ld,%lu,%lu",
                    static_cast<double>(referenceCountsPerSecond / period_counts),
                    bmeData.getTemperatureDegree(),
                    bmeData.getPressurePa(),
                    bmeData.getHumidityPercentRH());
        }
        if (mConfig.differenceToInternal)
        {
            printf(",%lld", estimatedDrift_us);
        }

        printf ("\n");

        if (mCurrentLine % mConfig.headerEveryNumLines == 0)
        {
            emitHeader();
        }
        mCurrentLine++;
    }

private:
    void
    emitHeader() const
    {
        if (mConfig.period)
            printf ("Period duration [us / %lu]", config::periodsPerMeasurement);
        if (mConfig.bmeData)
            printf (", Temperature [0.01 DegC], Pressure [2^(-8) Pa], Humidity [2^(-10) %RH]");
        if (mConfig.estimations)
            printf (", Estimated elapsed time [us], Current Fork Temperature Estimation [0.01 DegC], Current Period Estimation [us]");
        if (mConfig.humanReadables)
            printf (", Current Frequency [Hz], Temperature [DegC], Pressure [Pa], Humidity [%RH]");
        if (mConfig.differenceToInternal)
            printf (", Difference to internal time [us]");
        printf ("\n");
    }

    LoggerConfig mConfig;
    size_t mCurrentLine;
};