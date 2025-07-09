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

// the fork cycle counter
void
fork_osc_callback(uint gpio, uint32_t events);

// TODO: Add thread that
// watches serial input for set-time commands.
// Might also do the display sometime, perhaps.


// --------------

struct ForkMeasurement
{
    OscCount internalReference;
    std::optional<OscCount> externalReference;
};

// ugh, globals
queue_t period_fifo;

// will be set to true by environment samle timer
static volatile bool shouldSampleEnvironment = false;
repeating_timer_t environment_sample_timer;

using ExternalReferenceClock = clocksource::External<config::referenceClockPin,
                                                     config::referenceClockFrequency>;
ExternalReferenceClock* externalReferenceClock;

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

    ExternalReferenceClock externalClockSource{};   // init only now.
    externalReferenceClock = &externalClockSource;  // register for interrupt
    // This will block forever
    // externalSourceTest(externalClockSource, display);

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


    queue_init(&period_fifo, sizeof(ForkMeasurement), config::fifoSize);
    gpio_init(config::forkWatchPin);
    gpio_set_pulls(config::forkWatchPin, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(config::forkWatchPin,
                                        GPIO_IRQ_EDGE_RISE,
                                        true,
                                        &fork_osc_callback);

    // -- init done --

    static constexpr size_t printHeaderEveryNLines = 60 * (config::periodsPerMeasurement / config::expectedOscFreq);

    // Used for temperature sensing
    std::expected<BME280::EnvironmentMeasurement, std::string_view> lastEnvironmentSample = bme.readEnvironment();
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
        ForkMeasurement forkMeasurement;
        if (!queue_try_remove(&period_fifo, &forkMeasurement))
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
            const auto maybeChar = getchar_timeout_us(0);
            if (maybeChar > 0)
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
        status.expectedFrequency(forkMeasurement.externalReference.has_value());


        if (shouldSampleEnvironment)
        {
            // somewhen the timer fired
            const auto maybeCurrentEnv = bme.readEnvironment();
            if (maybeCurrentEnv)
            {
                lastEnvironmentSample = maybeCurrentEnv;
                shouldSampleEnvironment = false;
            }
            else
            {
                // FIXME: Sometimes, when I2C transmission somehow failed,
                // This is repeatedly shown.
                printf("Temp: %.*s\n",
                    static_cast<int>(maybeCurrentEnv.error().length()),
                    maybeCurrentEnv.error().data());
                display.showError("Err Temp");
                display.showError(maybeCurrentEnv.error());
                status.invalidTempReading();
                // (only) on timeout, we expect a stuck I2C bus, but we'll do it always.
                recoverTempI2c();
                printf("Tried to recover I2c bus.\n");
                // This will re-set the read request.
                // (only) necessary on "got default value", but we'll do it anyways.
                bme.init();
                printf("Inited BME\n");
            }
        }

        // Handling sanity of measured values
        // This is based on internal reference, because precision is not important
        if (forkMeasurement.internalReference > config::expectedMaxCount)
        {
            display.showError("fTooLow");
            status.tooLowFrequency();
            continue;
        }
        else if (forkMeasurement.internalReference < config::expectedMinCount)
        {
            display.showError("fTooHigh");
            status.tooHighFrequency();
            printf("Fork frequency too high: %lu counts for %u cycles\n",
                    forkMeasurement.internalReference,
                config::periodsPerMeasurement);
            continue;
        }
        else if (!lastEnvironmentSample)
        {
            // we never had a valid reading
            status.invalidTempReading();
            display.showError("No Temp Yet");
            display.showError(lastEnvironmentSample.error());
            printf("Temp: %.*s\n",
                    static_cast<int>(lastEnvironmentSample.error().length()),
                    lastEnvironmentSample.error().data());
            continue;
        }

        {
            // we effectively skipped all unexpected samples.
            // Now: Estimate! and print.

            // ------ The Interesting Thing ------
            const double delta = timeEstimator.consumeNextMeasurement(lastEnvironmentSample->temperature_centidegree);
            time.increaseDelta_us(delta);
            const DiffTime currentDriftSinceBoot_us =
                externalClockSource.getTimeSinceReferenceStable_us().value_or(clocksource::Internal::getTimeSinceReferenceStable_us())
                    - time.getElapsedTimeSinceBoot_us();
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

            if (lastEnvironmentSample)
            {
                display.setTemperature_deg(lastEnvironmentSample->getTemperatureDegree());
            }
            else
            {
                display.setTemperature_deg(std::nullopt);
            }
            display.update();
            // ------------------------------------

            // ----- emit measurements to log -----
            logger.addDataPoint(forkMeasurement.internalReference,
                                forkMeasurement.externalReference,
                                delta,
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
    // we just assume that this callback is only used for the correct gpio
    // if (gpio != config::forkWatchPin) {...}

    // Hot cycle:
    // The more repeatable this counts, the better phase variance gets
    static size_t currentCycle = 0;
    static AbsTime cycleStartTime_internal = 0;
    static std::optional<AbsTime> cycleStartTime_external = 0;
    if (currentCycle >= config::periodsPerMeasurement)
    {
        const AbsTime now_internal = clocksource::Internal::getCurrentReferenceTicks();
        const std::optional<AbsTime> now_external = externalReferenceClock->getCurrentReferenceTicks();

        ForkMeasurement newMeasurement;
        newMeasurement.internalReference = now_internal - cycleStartTime_internal;
        // I don't know whether this is actually readable or not... if both have value, then do difference.
        newMeasurement.externalReference = now_external.and_then(
                [](const AbsTime& now){
                    return cycleStartTime_external.transform(
                        [&now](const AbsTime& startTime){ return static_cast<DiffTime>(now - startTime);}
                    );
                });
        if (!queue_try_add(&period_fifo, &newMeasurement))
        {
            // this happens if we can't consume the counts
            // in the main estimate & display loop
            // Should not happen. Especially not regularly.
            printf("FIFO was full\n");
        };

        cycleStartTime_internal = now_internal;
        cycleStartTime_external = now_external;
        currentCycle = 0;
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

