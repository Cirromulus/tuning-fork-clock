#pragma once

#include <hardware/i2c.h>
#include <lib/hdsp_21xx.hpp>    // Ugly, only because of pin setup struct

static constexpr size_t onboardLedNr = 16;
// The default
static constexpr uint8_t displayPortexpanderAddr = 0x20;

i2c_inst_t* setupTempI2c();

i2c_inst_t* setupMcpI2c();

void
recoverTempI2c();

void
busScan(i2c_inst_t* ic2_device);

// stoopid offsets for mcp to easier remember
static constexpr uint8_t A = 0;
static constexpr uint8_t B = 8;
static constexpr HDSP21XXPins displayPinSetup
{
    {A+0, A+1, A+2, A+3, A+4, A+5, A+6, A+7},   // data
    {B+0, B+1, B+2, B+3, B+4},  // address
    B+7,            // chip enable
    std::nullopt,   // read
    B+5,            // write
    B+6,            // flash
    std::nullopt    // reset
};