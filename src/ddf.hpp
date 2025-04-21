#pragma once

#include <hardware/i2c.h>

i2c_inst_t* setupTempI2c();

i2c_inst_t* setupMcpI2c();

void
busScan(i2c_inst_t* ic2_device);

