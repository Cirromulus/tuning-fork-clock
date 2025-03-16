#pragma once

#include <stddef.h>
#include <inttypes.h>

#define GPIO_WATCH_PIN 29    // Near "GND" for less cable tangling

using OscCount = uint64_t;

static constexpr size_t sampleFreq = 1;
static constexpr size_t fifoSize = 8;   // one would be also OK, probably

static constexpr size_t expectedOscFreq = 440;  // Hz
static constexpr size_t numSamplesForAvg = 5 /*s*/ * sampleFreq;