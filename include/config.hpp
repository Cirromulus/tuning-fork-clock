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

// should be big enough for allowing I2C measuring traffic
static constexpr size_t fifoSize = 16;


static constexpr double expectedOscFreq = 440;
static constexpr double expectedDeviation = expectedOscFreq * .1;

static constexpr size_t periodsPerMeasurement = expectedOscFreq;

static constexpr size_t referenceClockFrequency = 1000 * 1000;  // us per count

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
 * The lastest run:
 On damped data (0.002626624422360645):
 Factors of best fit: f(x) = 987938.3857636167x^0 + 1.5652514138478073x^1 + -5.585329066394693e-05x^2
 */

// The following values are taken from plot.py calculations. DIY if you want to change that.
static constexpr std::array temperatureCalibrationPolynom {
    987938.3857636167,      // Could be seen as "average"
    1.5652514138478073      // Can be seen as temperature depencence
    -5.585329066394693e-05, // "nonlinearity" of temperature dependence
};

// This is not calibrated against an actual time difference,
// but instead was "trained" on the average sample time.
// FIXME: This value might be too small to actually affect calc
static constexpr double dampFactor {0.002626624422360645};