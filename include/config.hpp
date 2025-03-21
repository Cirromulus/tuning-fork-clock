#pragma once

#include <stddef.h>
#include <inttypes.h>

#define GPIO_WATCH_PIN 29    // Near "GND" for less cable tangling

// wrap every 1 hour. We don't expect that slow osc cycles.
// Benefit is that it only takes one register read.
using OscCount = uint32_t;

static constexpr size_t fifoSize = 8;   // one would be also OK, probably

static constexpr double expectedOscFreq = 440;
static constexpr double expectedDeviation = expectedOscFreq * .1;