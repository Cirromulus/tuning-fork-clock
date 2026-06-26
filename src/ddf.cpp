
#include "ddf.hpp"
#include <include/config.hpp>
#include <pico/stdlib.h>
#include <pico/binary_info.h>   // for picotool help

#include <stdio.h>

template <unsigned sdaPin, unsigned sclPin, uint32_t frequency = 10'000>
void i2cBusClear()
{
    static constexpr uint32_t us_per_symbol = 1'000'000 / frequency;
    gpio_init(sdaPin);
    gpio_set_dir(sdaPin, GPIO_OUT);
    gpio_put(sdaPin, true);
    gpio_init(sclPin);
    gpio_set_dir(sclPin, GPIO_OUT);
    // clock some zeros
    for (unsigned i = 0; i <= 16; i++)
    {
        gpio_put(sclPin, false);
        sleep_us(us_per_symbol / 2);
        gpio_put(sclPin, true);
        sleep_us(us_per_symbol / 2);
    }
    // do stop condition ?
    // gpio_put(sclPin, false);
    // sleep_us(us_per_symbol);
    // gpio_put(sdaPin, true);
    // sleep_us(us_per_symbol);
    // gpio_put(sclPin, true);
}

uint
setupI2C(const I2cConfig& config)
{
    const uint actualBaudrate = i2c_init(config.i2c_inst, config.desiredBaudrate);
    gpio_set_function(config.sda, GPIO_FUNC_I2C);
    gpio_set_function(config.scl, GPIO_FUNC_I2C);
    gpio_pull_up(config.sda);
    gpio_pull_up(config.scl);
    if (actualBaudrate != config.desiredBaudrate)
    {
        printf("i2c_init failed to reach exact baudrate of %u, got instead %u\n",
               config.desiredBaudrate, actualBaudrate);
    }

    // announce to picotool.
    // Not mandatory, just nice to have.
    bi_decl(bi_2pins_with_func(config.sda, config.scl, GPIO_FUNC_I2C));
    return actualBaudrate;
}

i2c_inst_t* setupTempI2c()
{
    setupI2C(config::bme280);
    return config::bme280.i2c_inst;
}

uart_inst_t* setupCommandPort()
{
    // note use of UART_FUNCSEL_NUM for the general case
    // where the func sel used for UART depends on the pin number
    // Do this before calling uart_init to avoid losing data
    gpio_set_function(config::settimePort.tx, UART_FUNCSEL_NUM(config::settimePort.uart_inst, config::settimePort.tx));
    gpio_set_function(config::settimePort.rx, UART_FUNCSEL_NUM(config::settimePort.uart_inst, config::settimePort.rx));
    uart_init(config::settimePort.uart_inst, config::settimePort.desiredBaudrate);
    return config::settimePort.uart_inst;
}

uart_inst_t* setupLogPort()
{
    // oh well
    return setupCommandPort();
}

void
recoverTempI2c()
{
    i2cBusClear<config::bme280.sda, config::bme280.scl, config::bme280.desiredBaudrate / 2>();
    setupI2C(config::bme280);
}

i2c_inst_t* setupMcpI2c()
{
    setupI2C(config::mcp);
    return config::mcp.i2c_inst;
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
