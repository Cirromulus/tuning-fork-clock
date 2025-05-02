#pragma once

#include <lib/hdsp_21xx.hpp>
#include <mcp23017.h>

#include <string_view>
#include <charconv>

class ClockDisplay
{
    enum class DisplayState
    {
        absoluteTime_time,
        absoluteTime_calendar,
        elapsedTimeSinceBoot,
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
        mDriver.write_string_running(str, {.end_wait_us = 500'000, .blink = true});
    }

    void
    showInfo(std::string_view const& str, const HDSP21XX::RunningTextOptions& options = {})
    {
        // TODO: Running text etc
        mDriver.write_string_running(str, options);
    }

    constexpr
    void
    setElapsedTimeSinceBoot_us(uint64_t const& elapsedTime_us)
    {
        mElapsedSinceBoot_s = elapsedTime_us / 1'000'000;
    }

    constexpr
    void
    setAbsoluteTime_us(const uint64_t& absoluteTime_us)
    {
        mAbsoluteTime_s = absoluteTime_us / 1'000'000;
    }

    // optional input so that we can disable in in runtime if reference disconnects
    constexpr
    void
    setCurrentDrift_us(const std::optional<uint64_t>& maybeDrift_us)
    {
        mDrift_ms = maybeDrift_us.transform([](const auto drift_us){return drift_us / 1'000;});
    }

    void
    update()
    {
        // This currently does not work like a full state machine,
        // as there is no origin-state dependency.
        // Will probably be added once it gets more fancy.
        switch (getNextTransition())
        {
            case DisplayState::absoluteTime_time:
            case DisplayState::absoluteTime_calendar:
            {
                // Currently not implemented to end.
                // Only displays unix timestamp as seconds, skipping the most significant digits to fit to screen.
                const unsigned eightDigits = 100'000'000;
                const unsigned partThatIsTooMuch = *mAbsoluteTime_s / eightDigits; // eight digits
                const unsigned timeToDisplay = *mAbsoluteTime_s - (partThatIsTooMuch * eightDigits);
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), timeToDisplay);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    mDriver.write_string_oneshot(std::string_view{mBuffer.begin(), end}, {.alignment = StringOptions::Alignment::right});
                }
                else
                {
                    showError("aT_t str");
                }
            }
            break;
            case DisplayState::elapsedTimeSinceBoot:
            {
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), mElapsedSinceBoot_s);
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
                // check for mDrift_ms.has_value() should be done in transition...
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), *mDrift_ms / 1'000.);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    const std::string_view numberstr{mBuffer.begin(), end};
                    mDriver.write_string_oneshot(numberstr,
                                                {.alignment = StringOptions::Alignment::left,
                                                 .nofill = true});
                    for (size_t i = numberstr.size(); i < mDriver.num_characters - 1; i++)
                    {
                        // manually fill spaces except last element
                        mDriver.write_builtin_char(i, ' ');
                    }
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
        if (mDrift_ms.has_value() && (mElapsedSinceBoot_s % 10 < 2))
        {
            // two seconds of drift from [0, 1]
            return DisplayState::drift;
        }
        else if (mAbsoluteTime_s)
        {
            return DisplayState::absoluteTime_time;
        }
        else
        {
            return DisplayState::elapsedTimeSinceBoot;
        }
    }

    // Todo: Beauty
    std::optional<uint64_t> mDrift_ms {std::nullopt};
    std::optional<uint32_t> mAbsoluteTime_s {std::nullopt};
    uint32_t mElapsedSinceBoot_s{0};

    std::array<char, HDSP21XX::num_characters> mBuffer;

    HDSP21XX mDriver;
};