
#include "ddf.hpp"
#include <pico/stdlib.h>
#include <pico/binary_info.h>   // for picotool help

#include <stdio.h>

uint
setupI2C(i2c_inst_t* ic2, unsigned sda, unsigned scl, uint baudrate = 400 * 1000)
{
    const uint actualBaudrate = i2c_init(ic2, baudrate);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    if (actualBaudrate != baudrate)
    {
        printf("i2c_init failed to reach baudrate of %u, got instead %u\n", baudrate, actualBaudrate);
    }

    // announce to picotool.
    // Not mandatory, just nice to have.
    bi_decl(bi_2pins_with_func(sda, scl, GPIO_FUNC_I2C));
    return actualBaudrate;
}

i2c_inst_t* setupTempI2c()
{
    // BME280 is on I2C1 GP26/27
    static constexpr unsigned sht20_sda = 26;
    static constexpr unsigned sht20_scl = 27;

    setupI2C(i2c1, sht20_sda, sht20_scl);
    return i2c1;
}

i2c_inst_t* setupMcpI2c()
{
    // FIXME: Is currently wired wrong. Should be i2c0!
    // 4, 5 ....
    static constexpr unsigned mcp_sda = 2;
    static constexpr unsigned mcp_scl = 3;

    setupI2C(i2c1, mcp_sda, mcp_scl, 100 * 1000);
    return i2c1;
}


void busScan(i2c_inst_t* ic2_device)
{
    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%1xx ", addr >> 4);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        ret = i2c_read_blocking_until(ic2_device, addr, &rxdata, 1, false, make_timeout_time_us(500000));

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("Done.\n");
}
