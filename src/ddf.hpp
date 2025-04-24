#pragma once

#include <hardware/i2c.h>

static constexpr size_t onboardLedNr = 16;
// The default
static constexpr uint8_t displayPortexpanderAddr = 0x20;

i2c_inst_t* setupTempI2c();

i2c_inst_t* setupMcpI2c();

void
busScan(i2c_inst_t* ic2_device);