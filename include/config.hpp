#pragma once

#include <stddef.h>
#include <inttypes.h>
#include <limits>
#include <array>

#define GPIO_WATCH_PIN 29    // Near "GND" for less cable tangling

// wrap every 1 hour if measuring microseconds.
// We don't expect that slow osc cycles.
// Benefit is that it only takes one register read.
using OscCount = uint32_t;

// To track microseconds "absolute time" (posix timestamp or since boot)
using AbsTime = uint64_t;
// used for drift etc
using DiffTime = int64_t;

namespace config
{
// should be big enough for allowing I2C measuring traffic.
// This is translatable via `fifoSize * periodsPerMeasurement / expectedOscFreq`
// = number of seconds until fifo is full
static constexpr size_t fifoSize = 16;


static constexpr double expectedOscFreq = 440;
static constexpr double expectedDeviation = expectedOscFreq * .10;

static constexpr size_t periodsPerMeasurement = expectedOscFreq;

static constexpr size_t referenceClockFrequency = 1'000'000;  // us per count

// Count resolution is currently 1 us
static constexpr
OscCount
toMicroseconds(double frequency)
{
    // us resolution
    return (1 / frequency) * referenceClockFrequency;
}

static constexpr OscCount expectedMinCount {periodsPerMeasurement * toMicroseconds(expectedOscFreq + expectedDeviation)};
static constexpr OscCount expectedMaxCount {periodsPerMeasurement * toMicroseconds(expectedOscFreq - expectedDeviation)};

static_assert(std::numeric_limits<OscCount>::max() > expectedMaxCount);

/*
 * The last run of ./analysis/estimate.py:
Estimation for ../logs/2025-03-31_22-00-43_sensor_log.db (440280.027356 seconds)
Damp factor: 0.002279174504551375
Factors for damped period estimation: [987958.2603574242, 1.5367473577982667, -4.597156613378032e-05]
Factors for period error estimation based on temp gradient: [2.3213648517665533, -0.057893464820847264, -0.0004974635372516944]
 */

// The following values are taken from plot.py calculations. DIY if you want to change that.
static constexpr std::array temperatureCalibrationPolynom {
    987959.2603574242,      // Could be seen as "average"
    1.5367473577982667,     // Can be seen as temperature depencence
    -4.597156613378032e-05, // "nonlinearity" of temperature dependence
};

static constexpr std::array tempRateCalibrationPolynom {
    2.3213648517665533, // this is a bit pointless? Its a constant error.
    -0.057893464820847264,
    -0.0004974635372516944
};

// This is not calibrated against an actual time difference,
// but instead was "trained" on the average sample time.
static constexpr double dampFactor {0.002279174504551375};

} // namespace config