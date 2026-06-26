#pragma once

// For configuration instances
#include <hardware/i2c.h>
#include <hardware/uart.h>

#include <stddef.h>
#include <inttypes.h>
#include <limits>
#include <array>

// wrap every 1 hour if measuring microseconds.
// We don't expect that slow osc cycles.
// Benefit is that it only takes one register read.
using OscCount = uint32_t;

// To track microseconds "absolute time" (posix timestamp or since boot)
using AbsTime = uint64_t;
// used for drift etc
using DiffTime = int64_t;

struct I2cConfig
{
    unsigned sda;
    unsigned scl;
    i2c_inst_t* i2c_inst;
    uint32_t desiredBaudrate = 350'000;
};

struct UartConfig
{
    unsigned rx;
    unsigned tx;
    uart_inst_t* uart_inst;
    uint32_t desiredBaudrate = 230'400;
};

namespace config
{

// Near "GND" for less cable tangling
static constexpr unsigned forkWatchPin = 29;
static constexpr unsigned referenceClockPin = 9;
static constexpr unsigned referenceClockDiffPin = 10;
static constexpr I2cConfig bme280 {.sda = 26, .scl = 27, .i2c_inst = i2c1};
static constexpr I2cConfig mcp {.sda = 4, .scl = 5, .i2c_inst = i2c0};
// uart structs are actually reinterpret-casted and thus not constexval
static const UartConfig settimePort {.rx = 13, .tx = 12, .uart_inst = uart0};
static const UartConfig logPort = settimePort;  // Currently (or forever) we only have one outside port

static constexpr AbsTime referenceClockFrequency = 1'000'000;  // counts per second

// should be big enough for allowing I2C measuring traffic.
// This is translatable via `fifoSize * periodsPerMeasurement / expectedOscFreq`
// = number of seconds until fifo is full
static constexpr size_t fifoSize = 32;


static constexpr double expectedOscFreq = 440;
static constexpr double expectedDeviation = expectedOscFreq * .10;

static constexpr size_t periodsPerMeasurement = expectedOscFreq;

// Count resolution is currently 1 us
static constexpr
OscCount
toMicroseconds(double frequency)
{
    // us resolution
    return (1 / frequency) * referenceClockFrequency;
}

static constexpr OscCount expectedMinCycleTime {periodsPerMeasurement * toMicroseconds(expectedOscFreq + expectedDeviation)};
static constexpr OscCount expectedMaxCycleTime {periodsPerMeasurement * toMicroseconds(expectedOscFreq - expectedDeviation)};

static_assert(std::numeric_limits<OscCount>::max() > expectedMaxCycleTime);

/*
 * The last run of ./analysis/estimate.py:
Estimation for logs/2026-04-01_17-35-11_sensor_log-pruned.db (4710088.388931 seconds)
Damp factor: 0.002130548230547027
Factors for damped period estimation:
9.880565853222419973e+05
1.254149979679611615e+00
7.671359632002468294e-06
Factors for period error estimation based on temp gradient:
2.663839631317995171e-01
-2.342039460984990384e-01
-3.829544454488955849e-03
 */

// The following values are taken from plot.py calculations. DIY if you want to change that.
static constexpr std::array temperatureCalibrationPolynom {
    9.880565853222419973e+05, // Could be seen as "average"
    1.254149979679611615e+00, // Can be seen as temperature depencence
    7.671359632002468294e-06, // "nonlinearity" of temperature dependence
};

static constexpr std::array tempRateCalibrationPolynom {
    2.663839631317995171e-01, // this is a bit pointless? Its a constant error.
    -2.342039460984990384e-01,
    -3.829544454488955849e-03
};

// This is not calibrated against an actual time difference,
// but instead was "trained" on the average sample time.
static constexpr double dampFactor {0.002130548230547027};

} // namespace config
