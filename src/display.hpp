#pragma once

#include <lib/hdsp_21xx.hpp>
#include <mcp23017.h>

#include <string_view>
#include <charconv>

class ClockDisplay
{
    enum class DisplayState
    {
        elapsedTime,
        drift
    };

    using StringOptions = HDSP21XX::StringOptions;

public:

    constexpr
    ClockDisplay(Mcp23017& expander, HDSP21XXPins const& pinSetup) :
    mDriver{pinSetup, expander}
    {
    }

    void
    showError(std::string_view const& str)
    {
        mDriver.write_string_running(str, {.blink = true});
    }

    void
    showInfo(std::string_view const& str)
    {
        // TODO: Running text etc
        mDriver.write_string_running(str);
    }

    constexpr
    void
    setCurrentElapsedTime_us(uint64_t const& elapsedTime_us)
    {
        mCurrentElapsedTime_s = elapsedTime_us / 1'000'000;
    }

    constexpr
    void
    setCurrentDrift_us(uint64_t const& drift_us)
    {
        mCurrentDrift_ms = drift_us / 1'000;
    }

    void
    update()
    {
        // This currently does not work like a full state machine,
        // as there is no origin-state dependency.
        // Will probably be added once it gets more fancy.
        switch (getNextTransition())
        {
            case DisplayState::elapsedTime:
            {
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), mCurrentElapsedTime_s);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    mDriver.write_string_oneshot(std::string_view{mBuffer.begin(), end}, {.alignment = StringOptions::Alignment::right});
                }
                else
                {
                    showError("cEET str");
                }
            }
            break;
            case DisplayState::drift:
            {
                // Display as a float with ms precision
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), mCurrentDrift_ms / 1'000.);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    mDriver.write_string_oneshot(std::string_view{mBuffer.begin(), end}, {.alignment = StringOptions::Alignment::left});
                    static constexpr char deltaChar = 0x07;
                    // overwrite last char with delta sign
                    mDriver.write_builtin_char(7, deltaChar, true);
                }
                else
                {
                    showError("driftStr");
                }
            }
            break;
        }
    }

private:
    constexpr
    DisplayState
    getNextTransition() const
    {
        // Currently no fancy state machine.
        if (mCurrentElapsedTime_s % 10 < 2)
        {
            // two seconds of drift from [0, 1]
            return DisplayState::drift;
        }
        else
        {
            // Display elapsed time between [2, 9]
            return DisplayState::elapsedTime;
        }
    }

    // Todo: Beauty
    uint64_t mCurrentDrift_ms {0};
    uint32_t mCurrentElapsedTime_s {0};

    std::array<char, HDSP21XX::num_characters> mBuffer;

    HDSP21XX mDriver;
};