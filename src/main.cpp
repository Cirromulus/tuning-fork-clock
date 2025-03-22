#include "config.hpp"
// #include "averager.hpp"
#include "bme280.hpp"

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
queue_t period_fifo;

static volatile bool shouldSampleEnvironment = false;
repeating_timer_t environment_sample_timer;


// --------------

i2c_inst_t* setupTempI2c()
{
    // BME280 is on I2C1 GP26/27
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

int main() {
    setup_default_uart();
    stdio_init_all();

    BME280 bmp{setupTempI2c()};

    while (!bmp.init())
    {
        printf("Could not init BME280.\n");
        sleep_ms(1000);
    }

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(2000, timer_callback, NULL, &environment_sample_timer))
    {
        printf("Failed to add enviroment sampling timer\n");
    }


    queue_init(&period_fifo, sizeof(OscCount), fifoSize);
    gpio_init(GPIO_WATCH_PIN);
    gpio_set_pulls(GPIO_WATCH_PIN, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE, true, &osc_callback);

    // -- init done --

    printf ("Period [us], Frequency [Hz], Temperature [raw], Pressure [raw]\n");
    auto lastTemperature = bmp.readTemperatureRaw();
    auto lastPressure = bmp.readPressureRaw();
    while(true)
    {
        OscCount oscPeriod = 0;
        queue_remove_blocking(&period_fifo, &oscPeriod);

        if (shouldSampleEnvironment)
        {
            lastTemperature = bmp.readTemperatureRaw();
            lastPressure = bmp.readPressureRaw();
            shouldSampleEnvironment = false;
        }

        printf("%lu,%f,%d,%d\n",
            oscPeriod,
            static_cast<double>(1000 * 1000) / oscPeriod,
            lastTemperature.value_or(-1),
            lastPressure.value_or(-1));
    }

    return 0;
}

void osc_callback(uint gpio, uint32_t events)
{
    // static_assert(decltype(declval(time_us_32())) == OscCount);
    const OscCount now = time_us_32();
    const OscCount diff = now - oscCount;
    oscCount = now;
    if (!queue_try_add(&period_fifo, &diff)) {
        printf("FIFO was full\n");
    };
}

bool timer_callback(__unused repeating_timer_t *rt) {
    shouldSampleEnvironment = true;
    return true; // keep repeating
}

