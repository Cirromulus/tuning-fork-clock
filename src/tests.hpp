#pragma once

#include <mcp23017.h>
#include <lib/bme280.hpp>
#include <lib/hdsp_21xx.hpp>
#include "display.hpp"
#include "reference_tick.hpp"


// This file holds more or less stale test functions
// that are not needed any more (that is, as long as it works)

void
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

void bmeTest(BME280& bme)
{
    while (!bme.init())
    {
        printf("Could not init BME280.\n");
        sleep_ms(1000);
    }


    while(true)
    {
        if (const auto maybeTemperature = bme.readTemperature())
        {
            printf("Temp: %lu * 0.01 Celsius\n", *maybeTemperature);
        }
        else
        {
            printf("No worky-work\n");
        }
        sleep_ms(1000);
    }
}

template <typename ExternalSource>
void externalSourceTest(const ExternalSource& externalClockSource, ClockDisplay& display)
{
    while (true)
    {
        const auto timeSinceStable_us = externalClockSource.getTimeSinceReferenceStable_us();
        if (timeSinceStable_us)
        {
            printf("Time since ReferenceClock: %llu\n", timeSinceStable_us);
            // const auto maybeSomeThing = externalClockSource.lookIntoStateMachine();
            // printf("Something inside: %d\n", maybeSomeThing.value_or(-1));
            const AbsTime& tss_us = *timeSinceStable_us;
            const AbsTime eightDigits = tss_us % 100'000'000;
            display.setElapsedTimeSinceBoot_us(eightDigits * 1'000'000);
            display.update();
        }
    }
}