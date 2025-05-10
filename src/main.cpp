#include <include/config.hpp>
#include <lib/bme280.hpp>
#include <lib/led.hpp>
#include <mcp23017.h>

#include "estimator.hpp"
#include "ddf.hpp"
#include "display.hpp"
#include "csvlogger.hpp"
#include "absolute_time.hpp"
#include "reference_tick.hpp"
#include "command.hpp"

#include "tests.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>

// #include <pico/multicore.h>

#include <stdio.h>
#include <cinttypes>   // uhg, oldschool
#include <array>


using namespace std::literals;

// Timer for enviroment sampling (i.e. Temp)
bool
env_sample_callback(repeating_timer_t *rt);

// the forc cycle counter
void
fork_osc_callback(uint gpio, uint32_t events);

// TODO: Add thread that
// watches serial input for set-time commands.
// Might also do the display sometime, perhaps.


// --------------

// ugh, globals
static OscCount oscCount = 0;
queue_t period_fifo;

// will be set to true by environment samle timer
static volatile bool shouldSampleEnvironment = false;
repeating_timer_t environment_sample_timer;


int
main()
{
    setup_default_uart();
    stdio_init_all();

    WS2812LED led{onboardLedNr};
    Status status{led, 0x03};
    status.noSignal();  // default led color

    // only used for display.
    Mcp23017 expander{setupMcpI2c(), displayPortexpanderAddr};
    // mcpTest(expander);
    // minimalHDSPTest(expander);
    ClockDisplay display{expander, displayPinSetup};
    display.showInfo("Startup", {.fade_in = true, .fade_out = true});

    clocksource::External<config::forkWatchPin, // config::referenceClockPin,
                          config::referenceClockFrequency> externalClockSource;

    externalSourceTest(externalClockSource, display);


    BME280 bme{setupTempI2c()};
    // this will block forever
    // bmeTest(bme);

    CSVLogger logger{};   // with default config
    AbsoluteTimeManager time{};
    CommandParser commandParser{time, display};

    while (!bme.init())
    {
        printf("Could not init BME280.\n");
        display.showError("Could not init BME280");
    }

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(-2000, env_sample_callback, NULL, &environment_sample_timer))
    {
        printf("Failed to add enviroment sampling timer\n");
        display.showError("Failed to add enviroment sampling timer");
    }


    queue_init(&period_fifo, sizeof(OscCount), config::fifoSize);
    gpio_init(config::forkWatchPin);
    gpio_set_pulls(config::forkWatchPin, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(config::forkWatchPin, GPIO_IRQ_EDGE_RISE, true, &fork_osc_callback);

    // -- init done --

    static constexpr size_t printHeaderEveryNLines = 60 * (config::periodsPerMeasurement / config::expectedOscFreq);

    // Used for temperature sensing
    std::optional<BME280::EnvironmentMeasurement> lastEnvironmentSample = bme.readEnvironment();
    // Used for detection of "no fork signal"
    absolute_time_t lastValidForkSampleTime = get_absolute_time();

    Estimator timeEstimator{
        PolynomCalc{config::temperatureCalibrationPolynom},
        PolynomCalc{config::tempRateCalibrationPolynom},
        Damper{config::dampFactor}
    };

    display.showInfo("InitDone");

    // main bigloop: get samples, estimate, print.
    while(true)
    {
        // Get new fork osc count from counter ITR
        OscCount oscCount = 0;
        if (!queue_try_remove(&period_fifo, &oscCount))
        {
            // There is no new oscCount to get
            const auto diff = absolute_time_diff_us(lastValidForkSampleTime, get_absolute_time());
            if (diff > config::expectedMaxCount)
            {
                display.showError("NoSignal");
                status.noSignal();
            }
            // --------------------------------------------------------------
            // No new updates, but now and here would be time to do something
            // TODO: make this more understandable
            const auto maybeChar = getchar_timeout_us(1);
            if (maybeChar != PICO_ERROR_TIMEOUT)
            {
                commandParser.consumeCharacter(static_cast<char>(maybeChar));
            }
            // --------------------------------------------------------------
            continue;
        }
        else
        {
            // We got a sample
            lastValidForkSampleTime = get_absolute_time();
        }

        // Set status "default", will be overwritten later if something errorred
        status.expectedFrequency();

        if (shouldSampleEnvironment)
        {
            // somewhen the timer fired
            const auto maybeCurrentEnv = bme.readEnvironment();
            if (maybeCurrentEnv)
            {
                lastEnvironmentSample = *maybeCurrentEnv;
                shouldSampleEnvironment = false;
            }
            else
            {
                display.showError("Err Temp");
                status.invalidTempReading();
            }
        }

        // Handling sanity of measured values
        if (oscCount > config::expectedMaxCount)
        {
            display.showError("fTooLow");
            status.tooLowFrequency();
            continue;
        }
        if (oscCount < config::expectedMinCount)
        {
            display.showError("fTooHigh");
            status.tooHighFrequency();
            continue;
        }
        if (!lastEnvironmentSample)
        {
            // we never had a valid reading
            status.invalidTempReading();
            display.showError("No Temp Yet");
            continue;
        }

        {
            // we effectively skipped all unexpected samples.
            // Now: Estimate! and print.

            // ------ The Interesting Thing ------
            const auto delta = timeEstimator.consumeNextMeasurement(lastEnvironmentSample->temperature_centidegree);
            time.increaseDelta_us(delta);
            const auto currentDriftSinceBoot_us = clocksource::Internal::getTimeSinceReferenceStable_us() - time.getElapsedTimeSinceBoot_us();
            // -----------------------------------


            // --- print current time to screen ---
            display.setElapsedTimeSinceBoot_us(time.getElapsedTimeSinceBoot_us());
            if (const auto maybeAbsoluteTime_us = time.getAbsoluteTime_us())
            {
                display.setAbsoluteTime_us(*maybeAbsoluteTime_us);
                if (const auto maybeDriftSinceUpdate_us = time.getDriftWhenAbsoluteTimeWasSet_us())
                {
                    // if we have it, show drift since set of time.
                    // FIXME: This decision probably should be done in the display itself.
                    display.setCurrentDrift_us(currentDriftSinceBoot_us - *maybeDriftSinceUpdate_us);
                }
            }
            else
            {
                // No absolute time, show drift since boot
                display.setCurrentDrift_us(currentDriftSinceBoot_us);
            }
            display.update();
            // ------------------------------------

            // ----- emit measurements to log -----
            logger.addDataPoint(oscCount,
                                time.getElapsedTimeSinceBoot_us(),
                                timeEstimator.getEstimatedForkTemperature(),
                                currentDriftSinceBoot_us,
                                *lastEnvironmentSample);
            // ------------------------------------
        }
    }

    return 0;
}

void fork_osc_callback(uint gpio, uint32_t events)
{
    // Hot cycle:
    // The more repeatable this counts, the better phase variance gets
    static size_t currentCycle = 0;
    if (currentCycle >= config::periodsPerMeasurement)
    {
        const AbsTime now = clocksource::Internal::getCurrentReferenceTicks();
        const OscCount diff = now - oscCount;
        oscCount = now;
        currentCycle = 0;
        if (!queue_try_add(&period_fifo, &diff))
        {
            // this happens if we can't consume the counts
            // in the main estimate & display loop
            // Should not happen. Especially not regularly.
            printf("FIFO was full\n");
        };
    }
    else
    {
        currentCycle++;
    }
}

bool
env_sample_callback(repeating_timer_t*)
{
    // as simple as it gets. Is not realtime-critical.
    shouldSampleEnvironment = true;
    return true; // keep repeating
}

