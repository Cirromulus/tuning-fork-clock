#pragma once

// For EnvironmentMeasurement.
// This include is not well for separation of concerns
#include <lib/bme280.hpp>
#include <include/config.hpp>   // for OscCount, which is also weird to include
#include "serial.hpp"

#include <pico/stdlib.h>
#include <cstdio>
#include <stddef.h>

// what to print.
struct LoggerConfig
{
    bool period = true;
    bool periodExternal = true;
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
    // TODO: Add abstraction layer to uart to not have the dependency here

    constexpr CSVLogger(uart_inst_t* output, LoggerConfig const& config = LoggerConfig{})
        : mConfig{config}, mCurrentLine{0}, mSerial{output}
    {
    }

    void addDataPoint(const OscCount& period_counts,    // unit is "reference counts"
                      const std::optional<OscCount> periodExternal_counts,
                      const OscCount& estimatedPeriod,
                      const uint64_t& estimatedTime_us,
                      const double& estimatedForkTemp_deg,
                      const int64_t& estimatedDrift_us,
                      const BME280::EnvironmentMeasurement& bmeData)
    {
        // I would like to make that more generic. Enum -> Type & member translation with templates?
        if (mConfig.period)
        {
            mSerial.print("%lu", period_counts);
        }
        if (mConfig.periodExternal)
        {
            mSerial.print(",%lu", periodExternal_counts.value_or(0));
        }
        if (mConfig.bmeData)
        {
            mSerial.print(",%ld,%lu,%lu",
                bmeData.temperature_centidegree,
                bmeData.pressure_q23_8,
                bmeData.humidity_q22_10);
        }
        if (mConfig.estimations)
        {
            mSerial.print(",%lu,%llu,%f", estimatedPeriod, estimatedTime_us, estimatedForkTemp_deg);
        }
        if (mConfig.humanReadables)
        {
            static constexpr double referenceCountsPerSecond = config::referenceClockFrequency * config::periodsPerMeasurement;
            mSerial.print(",%f,%ld,%lu,%lu",
                    static_cast<double>(referenceCountsPerSecond / period_counts),
                    bmeData.getTemperatureDegree(),
                    bmeData.getPressurePa(),
                    bmeData.getHumidityPercentRH());
        }
        if (mConfig.differenceToInternal)
        {
            mSerial.print(",%lld", estimatedDrift_us);
        }

        mSerial.print ("\r\n");

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
            mSerial.print ("Period duration (internal) [us / %lu]", config::periodsPerMeasurement);
        if (mConfig.periodExternal)
            mSerial.print ("Period duration (external) [us / %lu]", config::periodsPerMeasurement);
        if (mConfig.bmeData)
            mSerial.print (", Temperature [0.01 DegC], Pressure [2^(-8) Pa], Humidity [2^(-10) %RH]");
        if (mConfig.estimations)
        {
            mSerial.print (", Estimated Period duration [us / %lu]", config::periodsPerMeasurement);
            mSerial.print (", Estimated elapsed time [us]"
                    ", Current Fork Temperature Estimation [0.01 DegC]"
                    ", Current Period Estimation [us]");
        }
        if (mConfig.humanReadables)
            mSerial.print (", Current Frequency [Hz], Temperature [DegC], Pressure [Pa], Humidity [%RH]");
        if (mConfig.differenceToInternal)
            mSerial.print (", Difference to internal time [us]");
        mSerial.print ("\r\n");
    }

    LoggerConfig mConfig;
    size_t mCurrentLine;
    mutable Serial mSerial;


};