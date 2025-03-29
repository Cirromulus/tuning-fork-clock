#pragma once

#include <stddef.h>
#include <inttypes.h>
#include <limits>

#define GPIO_WATCH_PIN 29    // Near "GND" for less cable tangling

// wrap every 1 hour. We don't expect that slow osc cycles.
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