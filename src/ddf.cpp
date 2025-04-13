
#include "ddf.hpp"
#include <pico/stdlib.h>
#include <pico/binary_info.h>   // for picotool help

#include <stdio.h>

i2c_inst_t* setupTempI2c()
{
    // BME280 is on I2C1 GP26/27
    static constexpr unsigned sht20_sda = 26;
    static constexpr unsigned sht20_scl = 27;

    const auto init = i2c_init(i2c1, 400 * 1000); // "baud" rate 400kHz
    printf("i2c_init(i2c1, 100 * 1000) -> %u\n", init);
    gpio_set_function(sht20_sda, GPIO_FUNC_I2C);
    gpio_set_function(sht20_scl, GPIO_FUNC_I2C);
    gpio_pull_up(sht20_sda);
    gpio_pull_up(sht20_scl);

    // announce to picotool.
    // Not mandatory, just nice to have.
    bi_decl(bi_2pins_with_func(sht20_sda, sht20_scl, GPIO_FUNC_I2C));

    return i2c1;
}
