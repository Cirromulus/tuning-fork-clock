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
    uint32_t desiredBaudrate = 300'000;
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
static constexpr I2cConfig bme280 {.sda = 26, .scl = 27, .i2c_inst = i2c1};
static constexpr I2cConfig mcp {.sda = 4, .scl = 5, .i2c_inst = i2c0};
// uart structs are actually reinterpret-casted and thus not constexval
static const UartConfig settimePort {.rx = 13, .tx = 12, .uart_inst = uart0};
static const UartConfig logPort = settimePort;  // Currently (or forever) we only have one outside port

static constexpr AbsTime referenceClockFrequency = 1'000'000;  // counts per second

// should be big enough for allowing I2C measuring traffic.
// This is translatable via `fifoSize * periodsPerMeasurement / expectedOscFreq`
// = number of seconds until fifo is full
static constexpr size_t fifoSize = 16;


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
Estimation for 2025-05-28_13-04-25_sensor_log.db (619487.946153 seconds)
Damp factor: 0.0032832224304482674
Factors for damped period estimation: [np.float64(985174.3597262433), np.float64(3.579194938902257), np.float64(-0.00038942541470154155)]
Factors for period error estimation based on temp gradient: [np.float64(-0.30233783483291354), np.float64(0.5111493052797728), np.float64(0.00605863137525046)]

Estimation for 2025-08-06_09-05-58_sensor_log_zwischenkopie.db (432769.614446 seconds)
Damp factor: 0.001893494029314581
Factors for damped period estimation:
9.885662144472119398e+05
1.213906162853203297e+00
8.097113008716144879e-06
Factors for period error estimation based on temp gradient:
-6.554192502536039244e-01
-1.555599069694961478e-01
2.188748997581093855e-02
 */

// The following values are taken from plot.py calculations. DIY if you want to change that.
static constexpr std::array temperatureCalibrationPolynom {
    9.885662144472119398e+05, // Could be seen as "average"
    1.213906162853203297e+00, // Can be seen as temperature depencence
    8.097113008716144879e-06, // "nonlinearity" of temperature dependence
};

static constexpr std::array tempRateCalibrationPolynom {
    -6.554192502536039244e-01, // this is a bit pointless? Its a constant error.
    -1.555599069694961478e-01,
    2.188748997581093855e-02
};

// This is not calibrated against an actual time difference,
// but instead was "trained" on the average sample time.
static constexpr double dampFactor {0.001893494029314581};

} // namespace config