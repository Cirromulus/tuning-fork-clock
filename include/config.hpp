#pragma once

#include <stddef.h>
#include <inttypes.h>

#define GPIO_WATCH_PIN 2    // GP2; the first not-uart0 pin

using OscCount = uint64_t;

static constexpr size_t sampleFreq = 1;
static constexpr size_t fifoSize = 8;   // one would be also OK, probably