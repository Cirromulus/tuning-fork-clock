#include "config.hpp"
#include "averager.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <hardware/i2c.h>
// #include <pico/multicore.h>
#include <pico/binary_info.h>   // for picotool help

#include <stdio.h>
#include <cinttypes>   // uhg, oldschool
#include <array>
// #include <atomic>    -latomic not found?



bool timer_callback(repeating_timer_t *rt);

void osc_callback(uint gpio, uint32_t events);

// --------------

// ugh, globals
static OscCount oscCount = 0;
queue_t sample_fifo;

// Note: This whole timer thing should be replaced by an interrupt
// from an external clock of sufficient accuracy
repeating_timer_t sample_timer;


// --------------

i2c_inst_t* setupTempI2c()
{
    // BMP280 is on I2C1 GP26/27
    static constexpr unsigned sht20_sda = 26;
    static constexpr unsigned sht20_scl = 27;

    const auto init = i2c_init(i2c1, 100 * 1000); // "baud" rate 100kHz
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

// #### Copied from example, tmp #####

void busScan(i2c_inst_t* ic2_device)
{
    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
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

// ####                        #####

int main() {
    setup_default_uart();
    stdio_init_all();

    while(true)
    {
	    i2c_inst_t* temp_i2c = setupTempI2c();

	    for (int i = 0; i < 5; i++)
	    {
		uint8_t rxdata;
		int ret;
		ret = i2c_read_blocking_until(temp_i2c, 0x76, &rxdata, 1, false, make_timeout_time_us(100000));
		printf("Maybe temp sensor @ 0x40: %u (status %d)\n", rxdata, ret);
	    }
	    
	    busScan(temp_i2c);
    }


    queue_init(&sample_fifo, sizeof(OscCount), fifoSize);

    gpio_init(GPIO_WATCH_PIN);
    gpio_set_pulls(GPIO_WATCH_PIN, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE, true, &osc_callback);

    printf ("Period [us], Frequency [Hz]\n");
    while(true)
    {
        OscCount oscPeriod = 0;
        queue_remove_blocking(&sample_fifo, &oscPeriod);

        printf("%lu,%f\n", oscPeriod, static_cast<double>(1000 * 1000) / oscPeriod);
    }

    return 0;
}

void osc_callback(uint gpio, uint32_t events)
{
    // static_assert(decltype(declval(time_us_32())) == OscCount);
    const OscCount now = time_us_32();
    const OscCount diff = now - oscCount;
    oscCount = now;
    if (!queue_try_add(&sample_fifo, &diff)) {
        printf("FIFO was full\n");
    };
}
