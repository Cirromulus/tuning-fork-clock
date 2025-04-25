#include <include/config.hpp>
#include <lib/hdsp_21xx.hpp>
#include <lib/bme280.hpp>
#include <lib/led.hpp>
#include <mcp23017.h>

#include "estimator.hpp"
#include "ddf.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>

// #include <pico/multicore.h>

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


void printCsvHeader()
{
    printf ("Period duration [us / %lu]", periodsPerMeasurement);
    printf (", Temperature [0.01 DegC], Pressure [2^(-8) Pa], Humidity [2^(-10) %RH]");
    // printf (", Current Temperature Estimation [0.01 DegC], Current Period Estimation [us]");
    printf (", Estimated elapsed time [us]");
    printf (", Difference to internal time [us]");
    printf ("\n");
}

void
[[noreturn]]
mcpTest(Mcp23017& mcp)
{
    bool on = true;
    for (size_t round = 0; ; round++)
    {
        for (size_t pin = 0; pin < 16; pin++)
        {
            printf ("Pin %d %s\n", pin, on ? "on" : "off");
            mcp.set_output_bit_for_pin(pin, on);
            mcp.flush_output();
            sleep_ms(500);
        }
        on = !on;
    }
}

void
minimalHDSPTest(Mcp23017& expander)
{
    struct ExpanderPin
    {
        void
        set(bool value)
        {
            expander.set_output_bit_for_pin(mPinNr, value);
        }

        Mcp23017& expander;
        int mPinNr;
    };
    static constexpr uint8_t A = 0;
    static constexpr uint8_t B = 8;

    ExpanderPin ce {expander, B+7};
    ExpanderPin a4 {expander, B+4};
    ExpanderPin fl {expander, B+6};
    ExpanderPin d6 {expander, A+6};

    for (size_t i = 0; ; i++)
    {
        static constexpr size_t magicWaitValue_ms = 400;
        printf ("selftest %d:\n", i);
        ce.set(false);
        expander.flush_output();
        sleep_ms(magicWaitValue_ms);
        a4.set(true);
        fl.set(true);
        d6.set(true);
        expander.flush_output();
        ce.set(true);  // this should apply the command
        expander.flush_output();
        printf ("sent.\n");
        for (size_t second = 0; second <= 5; second++)
        {
            printf("waiting gracefully for test end %d\n", second);
            sleep_ms(1000);
        }
    }
}

int
main() {
    setup_default_uart();
    stdio_init_all();

    WS2812LED led{onboardLedNr};
    Status status{led, 0x03};

    Mcp23017 expander{setupMcpI2c(), displayPortexpanderAddr};
    // mcpTest(expander);
    // minimalHDSPTest(expander);
    HDSP21XX display{displayPinSetup, expander};

    display.write_builtin_char(0, 0);
    display.write_builtin_char(1, 1);
    display.write_builtin_char(2, 0xFF);
    return 0;


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

    Estimator timeEstimator{
        PolynomCalc{temperatureCalibrationPolynom},
        PolynomCalc{tempRateCalibrationPolynom},
        Damper{dampFactor}
    };

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

        // Set status "default", will be overwritten later if something errorred
        status.expectedFrequency();

        if (shouldSampleEnvironment)
        {
            const auto maybeCurrentEnv = bme.readEnvironment();
            if (maybeCurrentEnv)
            {
                lastEnvironmentSample = *maybeCurrentEnv;
                shouldSampleEnvironment = false;
            }
            else
            {
                status.invalidTempReading();
            }
        }

        if (oscCount > expectedMaxCount)
        {
            status.tooLowFrequency();
            continue;
        }
        if (oscCount < expectedMinCount)
        {
            status.tooHighFrequency();
            continue;
        }
        if (!lastEnvironmentSample)
        {
            // we never had a valid reading
            status.invalidTempReading();
            continue;
        }

        {
            // we effectively skipped all unexpected samples

            timeEstimator.consumeNextMeasurement(lastEnvironmentSample->temperature_centidegree);
            const auto currentEstimatedElapsedTime = timeEstimator.getEstimatedElapsedTime();

            if (currentLine >= printHeaderEveryNLines)
            {
                printCsvHeader();
                currentLine = 0;
            }

            printf("%lu", oscCount);

            {
                printf(",%ld,%lu,%lu",
                    lastEnvironmentSample->temperature_centidegree,
                    lastEnvironmentSample->pressure_q23_8,
                    lastEnvironmentSample->humidity_q22_10);
            }

            printf(",%lld", currentEstimatedElapsedTime);
            printf(",%lld", time_us_64() - currentEstimatedElapsedTime);

            // now the derived values
            // printf(",%f,%ld,%lu,%lu",
            //         static_cast<double>(referenceClockFrequency * periodsPerMeasurement) / oscCount,
            //         lastEnvironmentSample->getTemperatureDegree(),
            //         lastEnvironmentSample->getPressurePa(),
            //         lastEnvironmentSample->getHumidityPercentRH()
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

