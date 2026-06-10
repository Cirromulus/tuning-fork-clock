#pragma once

#include <lib/hdsp_21xx.hpp>
#include <mcp23017.h>

#include <string_view>
#include <charconv>
#include <ctime>
#include <cstdio>   // only for debug print

class ClockDisplay
{
    enum class DisplayState
    {
        absoluteTime_stamp,
        absoluteTime_time,
        absoluteTime_calendar,
        elapsedTimeSinceBoot,
        temperature,
        drift
    };

    using StringOptions = HDSP21XX::StringOptions;

    static constexpr uint8_t nightBrightness = 1;
    static constexpr uint8_t dayBrightness = 5;
    static constexpr uint8_t mediumBrightness = 2;

public:

    ClockDisplay(Mcp23017& expander, HDSP21XXPins const& pinSetup) :
    mDriver{pinSetup, expander}
    {
        mDriver.set_brightness(mediumBrightness);
        mDegreeChar = mDriver.registerCustomCharacter(hdsp21xx::chars::degree);
        mHeartL = mDriver.registerCustomCharacter(hdsp21xx::chars::heartL);
        mHeartR = mDriver.registerCustomCharacter(hdsp21xx::chars::heartR);

        if (!mDegreeChar || !mHeartL || !mHeartR)
        {
            showError("Could not register custom chars!");
        }
    }

    void
    showError(std::string_view const& str)
    {
        printf("Error: %.*s\n", str.length(), str.data());
        mDriver.write_string_running(str, {.per_char_wait_us = 70'000, .end_wait_us = 250'000, .blink = true});
    }

    void
    showInfo(std::string_view const& str, const HDSP21XX::RunningTextOptions& options = {})
    {
        // TODO: Running text etc
        printf("Info: %.*s\n", str.length(), str.data());
        mDriver.write_string_running(str, options);
    }

    constexpr
    void
    setElapsedTimeSinceBoot_us(const AbsTime& elapsedTime_us)
    {
        mElapsedSinceBoot_s = elapsedTime_us / 1'000'000;
    }

    constexpr
    void
    setAbsoluteTime_us(const AbsTime& absoluteTime_us)
    {
        mAbsoluteTime_s = absoluteTime_us / 1'000'000;
    }

    // optional input so that we can disable in in runtime if reference disconnects
    constexpr
    void
    setCurrentDrift_us(const std::optional<DiffTime>& maybeDrift_us)
    {
        mDrift_ms = maybeDrift_us.transform([](const auto& drift_us){return drift_us / 1'000;});
    }

    constexpr
    void
    setTemperature_deg(const std::optional<float> maybeTemp_deg)
    {
        mTemperature = maybeTemp_deg;
    }

    void
    update()
    {
        // This currently does not work like a full state machine,
        // as there is no origin-state dependency.
        // Will probably be added once it gets more fancy.
        switch (getNextTransition())
        {
            case DisplayState::absoluteTime_stamp:
            {
                // displays unix timestamp as seconds, skipping the most significant digits to fit to screen.
                constexpr unsigned eightDigits = 100'000'000;
                const unsigned partThatIsTooMuch = *mAbsoluteTime_s / eightDigits;
                const unsigned timeToDisplay = *mAbsoluteTime_s - (partThatIsTooMuch * eightDigits);
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), timeToDisplay);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    mDriver.write_string_oneshot(std::string_view{mBuffer.begin(), end}, {.alignment = StringOptions::Alignment::right});
                }
                else
                {
                    showError("aT_s str");
                }
            }
            break;
            case DisplayState::absoluteTime_time:
            {
                const time_t now_stamp = *mAbsoluteTime_s;
                const std::tm* now = std::localtime(&now_stamp);
                size_t c = 0;
                writeChars<2>(c, now->tm_hour);
                mDriver.write_builtin_char(c++, ':', true);
                writeChars<2>(c, now->tm_min);
                mDriver.write_builtin_char(c++, ':', true);
                writeChars<2>(c, now->tm_sec);

                // While we are at it, we can decide whether it is nighttime or not...
                // Currently disabled because of too intense
                // heat production by the display that throws off measurement
                // setBrightnessBasedOnTime(now->tm_hour);
            }
            break;
            case DisplayState::absoluteTime_calendar:
            {
                const time_t now_stamp = *mAbsoluteTime_s;
                const std::tm* now = std::localtime(&now_stamp);
                size_t c = 0;
                writeChars<2>(c, now->tm_mday);
                mDriver.write_builtin_char(c++, '.');
                writeChars<2>(c, now->tm_mon+1);
                mDriver.write_builtin_char(c++, '.');
                writeChars<2>(c, now->tm_year);
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
                const float drift_s = *mDrift_ms / 1'000.;
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), drift_s);
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
                    printf("String error drift: Could not print %f to screen\n", drift_s);
                }
            }
            break;
            case DisplayState::temperature:
            {
                const auto [end, code] = std::to_chars(mBuffer.begin(), mBuffer.end(), *mTemperature);
                if (code == std::errc())    // this is considered a success. meh.
                {
                    const std::string_view numberstr{mBuffer.begin(), end};
                    mDriver.write_string_oneshot(numberstr,
                                                {.alignment = StringOptions::Alignment::left,
                                                 .nofill = true});
                    for (size_t i = numberstr.size(); i < mDriver.num_characters - 2; i++)
                    {
                        // manually fill spaces except last element
                        mDriver.write_builtin_char(i, ' ');
                    }

                    // overwrite last char
                    // TODO: Custom char "degree" symbol
                    mDriver.write_user_char(6, mDegreeChar);
                    mDriver.write_builtin_char(7, 'C');
                }
                else
                {
                    showError("TempStr");
                    printf("String error temp: Could not print %f to screen\n", *mTemperature);
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
        // the period in which this sequence is re-played.
        static constexpr unsigned timeWindow {25};

        // Currently no fancy state machine. Could be done more beautifully.

        const unsigned tempEndSecondToShow = mTemperature.has_value() ? 1 : 0;
        if (mElapsedSinceBoot_s % timeWindow < tempEndSecondToShow)
        {
            return DisplayState::temperature;
        }

        const unsigned driftEndSecondToShow = tempEndSecondToShow + (mDrift_ms.has_value() ? 1 : 0);
        if (mElapsedSinceBoot_s % timeWindow < driftEndSecondToShow)
        {
            return DisplayState::drift;
        }

        if (mAbsoluteTime_s)
        {
            if (mElapsedSinceBoot_s % timeWindow > driftEndSecondToShow + 5)
            {
                return DisplayState::absoluteTime_calendar;
            }
            else
            {
                // "The rest"
                return DisplayState::absoluteTime_time;
            }
        }
        else
        {
            return DisplayState::elapsedTimeSinceBoot;
        }
    }

    template <size_t numDigits>
    constexpr
    void
    writeChars(size_t& offset, const auto& number)
    {
        size_t tenthSequence = 1;
        for (size_t digit = 1; digit <= numDigits; digit++)
        {
            const char c = ((number / tenthSequence) % 10) + '0';
            mDriver.write_builtin_char(offset + (numDigits - digit), c);
            tenthSequence *= 10;
        }
        offset += numDigits;
    }

    static constexpr
    bool isItNighttime(unsigned hour)
    {
        return hour < 5 || hour > 23;
    }

    constexpr
    void
    setBrightnessBasedOnTime(const auto& hour)
    {
        mDriver.set_brightness(isItNighttime(hour) ? nightBrightness : dayBrightness);
    }

    // Todo: Beauty
    std::optional<DiffTime> mDrift_ms {std::nullopt};
    std::optional<AbsTime> mAbsoluteTime_s {std::nullopt};
    AbsTime mElapsedSinceBoot_s{0};

    std::optional<float> mTemperature;

    std::array<char, HDSP21XX::num_characters> mBuffer;

    HDSP21XX mDriver;
    HDSP21XX::CustomCharacterHandle mDegreeChar;
    HDSP21XX::CustomCharacterHandle mHeartL;
    HDSP21XX::CustomCharacterHandle mHeartR;
};
