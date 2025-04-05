#include "config.hpp"
#include "bme280.hpp"
#include "led.hpp"
#include "estimator.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <hardware/i2c.h>
// #include <pico/multicore.h>
#include <pico/binary_info.h>   // for picotool help

#include <stdio.h>
#include <cinttypes>   // uhg, oldschool
#include <array>

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

void printCsvHeader()
{
    printf ("Period duration [us / %lu]", periodsPerMeasurement);
    printf (", Temperature [0.01 DegC], Pressure [2^(-8) Pa], Humidity [2^(-10) %RH]");
    printf (", Current Period Estimation [us], Estimated elapsed time [us]");
    printf (", Difference to internal time [us]");
    printf ("\n");
}

int main() {
    setup_default_uart();
    stdio_init_all();

    OnboardLED led{16};
    Status status{led, 0x03};

    BME280 bme{setupTempI2c()};

    // this will block forever
    // bmeTest(bme);

    while (!bme.init())
    {
        printf("Could not init BME280.\n");
        sleep_ms(1000);
    }

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(-2000, timer_callback, NULL, &environment_sample_timer))
    {
        printf("Failed to add enviroment sampling timer\n");
    }


    queue_init(&period_fifo, sizeof(OscCount), fifoSize);
    gpio_init(GPIO_WATCH_PIN);
    gpio_set_pulls(GPIO_WATCH_PIN, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE, true, &osc_callback);

    // -- init done --

    static constexpr size_t printHeaderEveryNLines = 60 * (periodsPerMeasurement / expectedOscFreq);
    size_t currentLine = printHeaderEveryNLines;

    auto lastEnvironmentSample = bme.readEnvironment();
    auto lastValidOscSampleTime = get_absolute_time();
    uint64_t estimatedElapsedTime_us = 0;
    constexpr CompensationEstimator estimator{temperatureCalibrationPolynom};
    constexpr Damper tempDamp{dampFactor};
    
    // is here because of no signal not working on the first occurrence dunno
    status.noSignal();
    while(true)
    {
        OscCount oscCount = 0;
        if (!queue_try_remove(&period_fifo, &oscCount))
        {
            const auto diff = absolute_time_diff_us(lastValidOscSampleTime, get_absolute_time());
            if (diff > expectedMaxCount)
            {
                // ugly enough, without the printf, it will not set the led
                // ON THE FIRST OCCURENCE. Ich habe Feierabend, just hack around it in led init
                // printf("%lld\n", diff);
                status.noSignal();
            }
            // skip and retry
            continue;
        }
        else
        {
            lastValidOscSampleTime = get_absolute_time();
        }

        if (shouldSampleEnvironment)
        {
            // TODO: Only if read was successful!
            const auto maybeCurrentEnv = bme.readEnvironment();
            if (maybeCurrentEnv)
            {
                lastEnvironmentSample = *maybeCurrentEnv;
                shouldSampleEnvironment = false;
            }
        }

        if (oscCount > expectedMaxCount)
        {
            status.tooLowFrequency();
        }
        else if (oscCount < expectedMinCount)
        {
            status.tooHighFrequency();
        }
        else
        {
            // we effectively skip unexpected samples

            status.expectedFrequency();

            const auto env = lastEnvironmentSample.value_or(BME280::EnvironmentMeasurement{-66666,0,0});

            // estimator polynom is based on unnormalized data
            const double estimatedPeriod_us = estimator.estimate(env.temperature_centidegree);
            estimatedElapsedTime_us += llround(estimatedPeriod_us);

            if (currentLine >= printHeaderEveryNLines)
            {
                printCsvHeader();
                currentLine = 0;
            }

            printf("%lu", oscCount);

            printf(",%ld,%lu,%lu",
                env.temperature_centidegree,
                env.pressure_q23_8,
                env.humidity_q22_10);

            printf(",%f,%lld", estimatedPeriod_us, estimatedElapsedTime_us);
            printf(",%lld", time_us_64() - estimatedElapsedTime_us);

            // now the derived values
            // printf(",%f,%ld,%lu,%lu",
            //         static_cast<double>(referenceClockFrequency * periodsPerMeasurement) / oscCount,
            //         env.getTemperatureDegree(),
            //         env.getPressurePa(),
            //         env.getHumidityPercentRH()
            // );

            printf("\n");

            currentLine++;
        }
    }

    return 0;
}

void osc_callback(uint gpio, uint32_t events)
{
    static size_t currentCycle = 0;
    if (currentCycle >= periodsPerMeasurement)
    {
        const OscCount now = time_us_32();
        const OscCount diff = now - oscCount;
        oscCount = now;
        currentCycle = 0;
        if (!queue_try_add(&period_fifo, &diff)) {
            printf("FIFO was full\n");
        };
    }
    else
    {
        currentCycle++;
    }
}

bool timer_callback(__unused repeating_timer_t *rt) {
    shouldSampleEnvironment = true;
    return true; // keep repeating
}

