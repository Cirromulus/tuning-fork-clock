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


void
jump_start_fork(uint16_t duration_ms);

void
setup_fork_input(bool enable = true);

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
ExternalReferenceClock* externalReferenceClock = nullptr;

// --------------

// Todo: Named parameter or something better readable
void
setup_fork_input(bool enable)
{
    if (enable)
    {
        gpio_set_dir(config::forkWatchPin, GPIO_IN);
        gpio_put(config::forkWatchPin, 0);  // probably
        gpio_set_pulls(config::forkWatchPin, false, true);    // "Weak" pulldown
    }
    else
    {
        gpio_set_dir(config::forkWatchPin, GPIO_OUT);
        gpio_put(config::forkWatchPin, 0);
    }
    gpio_set_function(config::forkWatchPin, GPIO_FUNC_SIO);
    // enable or disable the callback
    gpio_set_irq_enabled_with_callback(config::forkWatchPin,
                                    GPIO_IRQ_EDGE_RISE,
                                    enable,
                                    &fork_osc_callback);
}

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

    ExternalReferenceClock externalClockSource{config::referenceClockDiffPin};   // init only now. It uses PIO.
    externalReferenceClock = &externalClockSource;  // register for interrupt
    // This will block forever
    // externalSourceTest(externalClockSource, display);

    BME280 bme{setupTempI2c()};
    // this will block forever
    // bmeTest(bme);

    uart_inst_t* const commandPort = setupCommandPort();
    // uartTest(commandPort);
    uart_inst_t* const loggingPort = setupLogPort();
    // uartTest(loggingPort);

    CSVLogger logger{loggingPort};   // with default config
    AbsoluteTimeManager time{};
    CommandParser commandParser{time, display};

    for (BME280::MaybeError maybeSuccess = bme.init(); not maybeSuccess.has_value(); maybeSuccess = bme.init())
    {
        const auto& error = maybeSuccess.error();
        printf("Could not init BME280: %.*s\n", static_cast<int>(error.length()), error.data());
        display.showError("Could not init BME280");
        display.showError(error);
    }

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(-2000, env_sample_callback, NULL, &environment_sample_timer))
    {
        printf("Failed to add enviroment sampling timer\n");
        display.showError("Failed to add enviroment sampling timer");
    }


    queue_init(&period_fifo, sizeof(ForkMeasurement), config::fifoSize);
    setup_fork_input(true);

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
            // There is no new oscCount to get, currently.

            // --- check whether there was nothing for too long
            const auto diff = absolute_time_diff_us(lastValidForkSampleTime, get_absolute_time());
            if (diff > config::expectedMaxCycleTime)
            {
                display.showError("NoSignal");
                status.noSignal();
                jump_start_fork(500);
            }

            // --------------------------------------------------------------
            // No new Measurement, but not timed out either.
            // we have time to spare!
            // now and here would be time to do something

            if (uart_is_readable(commandPort))
            {
                // FIXME: Find out why spurious MSBit is set on every fourth char.
                // Perhaps unintentional FIFO flag?
                const uint8_t c = uart_getc(commandPort) & 0x7F;
                const auto maybeFeedback = commandParser.consumeCharacter(c);
                if (maybeFeedback)
                {
                    uart_puts(commandPort, *maybeFeedback);
                }
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
            // somewhen, the timer fired
            const auto maybeCurrentEnv = bme.readEnvironment();
            if (maybeCurrentEnv)
            {
                lastEnvironmentSample = maybeCurrentEnv;
                shouldSampleEnvironment = false;
            }
            else
            {
                display.showError(maybeCurrentEnv.error());
                status.invalidTempReading();
                // (only) on timeout, we expect a stuck I2C bus, but we'll do it always.
                recoverTempI2c();
                printf("Tried to recover I2c bus.\n");
                // This will re-set the read request.
                // (only) necessary on "got default value", but we'll do it anyways.
                const auto maybeSuccess = bme.init();
                if (maybeSuccess)
                {
                    printf("Inited BME\n");
                }
                else
                {
                    const auto& err = maybeSuccess.error();
                    printf("BME init fail: %.*s\n", static_cast<int>(err.length()), err.data());
                }
            }
        }

        // Handling sanity of measured values
        // This is based on internal reference, because precision is not important
        if (forkMeasurement.internalReference > config::expectedMaxCycleTime)
        {
            display.showError("fTooLow");
            status.tooLowFrequency();
            continue;
        }
        else if (forkMeasurement.internalReference < config::expectedMinCycleTime)
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
            const auto& err = lastEnvironmentSample.error();
            // we never had a valid reading
            status.invalidTempReading();
            display.showError("No Temp Yet");
            display.showError(err);
            continue;
        }

        {
            // we effectively skipped all unexpected samples.
            // Now: Estimate! and print.

            // ------ The Interesting Thing ------
            const double delta = timeEstimator.consumeNextMeasurement(lastEnvironmentSample->temperature_centidegree);
            time.increaseDelta_us(delta);
            // -----------------------------------

            // FIXME: The calculation for the "TimeSinceReferenceStable" of the external clock source
            // is slightly wrong because:
            //  1. It is not necessarily since boot
            //  2. It does not account for gaps in external clock (which also leads to 1.)
            // So this just uses the internal drifting source for a rough estimation of an error.
            // It is only for the looks anyway.
            const DiffTime currentDriftSinceBoot_us =
                // externalClockSource.getTimeSinceReferenceStable_us()
                        // .value_or(clocksource::Internal::getTimeSinceReferenceStable_us())
                        clocksource::Internal::getTimeSinceReferenceStable_us()
                        - time.getElapsedTimeSinceBoot_us();

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
    static AbsTime cycleStartTime_internal = clocksource::Internal::getCurrentReferenceTicks();
    static std::optional<AbsTime> cycleStartTime_external = externalReferenceClock->getCurrentReferenceTicks();

    if (currentCycle >= config::periodsPerMeasurement)
    {
        const AbsTime now_internal = clocksource::Internal::getCurrentReferenceTicks();
        const std::optional<AbsTime> now_external = externalReferenceClock->getCurrentReferenceTicks();

        ForkMeasurement newMeasurement{};
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
    // Also: using unsafe because this is just a rough congestion control not to loose samples.
    shouldSampleEnvironment = queue_get_level_unsafe(&period_fifo) < (config::fifoSize / 2);
    return true; // keep repeating
}

void
jump_start_fork(uint16_t duration_ms)
{
    setup_fork_input(false);

    static constexpr OscCount period_us = config::toMicroseconds(config::expectedOscFreq);
    // this division is not precise, so we probably will be slightly out of tune!
    static constexpr OscCount halfPeriod_us = period_us / 2;
    static_assert(period_us - (halfPeriod_us * 2) < config::expectedDeviation / 2, "Too much out of tune!");

    const OscCount periodsToStimulate = (duration_ms * 1000) / period_us;

    // instead of rounding up, loop always one more time than requested (hehe)
    for (OscCount i = 0; i <= periodsToStimulate; i++)
    {
        gpio_put(config::forkWatchPin, false);
        sleep_us(period_us / 2);
        gpio_put(config::forkWatchPin, true);
        sleep_us(period_us / 2);
    }
    gpio_put(config::forkWatchPin, false);

    // todo: Setup whatever it was before instead!
    setup_fork_input(true);
}