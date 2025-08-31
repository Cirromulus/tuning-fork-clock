#pragma once

#include "absolute_time.hpp"
#include "reference_tick.hpp"
#include "display.hpp"    // a little bit ugly from SW design perspective

#include <string_view>
#include <limits>
#include <cmath>
#include <cstdio>
#include <optional>
#include <expected>
#include <functional>
#include <cctype>
#include <time.h>
#include <stdlib.h>

class TimeParser
{
    static constexpr std::optional<uint8_t>
    convertCharacterToNumber(char c)  // convert individual ASCII character to interger
    {
        if (c >= '0' && c <= '9')
        {
            return (c - '0') + 0;
        }
        return std::nullopt;
    }

    static constexpr std::expected<AbsTime, std::string_view>
    stringToInt(const std::string_view& input)
    {
        if (input.size() == 0)  // sanity check if there is any data in the Slice
        {
            return std::unexpected("empty parse string");
        }
        AbsTime result = 0;

        // This constant defines the maximal value that can still be multiplied with 'base' and
        // contained in the <AbsTime>-Range.
        constexpr AbsTime maxSafeMultiplicationValue = std::numeric_limits<AbsTime>::max() / 10;
        constexpr bool hasAbsTimeNegativeValues = std::numeric_limits<AbsTime>::min() < 0;

        // check for negative input
        if (input[0] == '-')
        {
            return std::unexpected("Encountered minus sign: I don't care for the past");
        }

        for (const auto& c : input)  // converting character in buffer to one integer
        {
            // multiplication overflow check here
            // It checks if 'result' does not exceed 'maxSafeMultiplicationValue' and therefore can be
            // multiplied with 'base' without causing an overflow.
            if (result > maxSafeMultiplicationValue)
            {
                return std::unexpected("Integer overflow: Mult");
            }

            const auto maybeNextNumber = convertCharacterToNumber(c);
            if (!maybeNextNumber.has_value())
            {
                return std::unexpected("Encountered invalid character");
            }

            // additon overflow check here
            // Checks if the remaining space in the <AbsTime> range is bigger than the number to add.
            // This makes sure that the new result fits into the <AbsTime> range.
            const uint8_t& currentDigit = maybeNextNumber.value();

            if ((std::numeric_limits<AbsTime>::max() - result) < currentDigit)
            {
                return std::unexpected("Integer overflow: Add");
            }

            result *= 10;
            result += currentDigit;
        }

        return result;
    }


public:
    constexpr
    TimeParser() : mWritePointer{0}, mParseBuffer{}
    {
    }

    // returns an error explanation
    constexpr
    std::optional<std::string_view>
    consume_character_ms(const char& chr)
    {
        if (chr < '0' || chr > '9')
        {
            return "char not a number";
        }
        mParseBuffer[mWritePointer] = chr;
        mWritePointer++;
        if (mWritePointer >= mParseBuffer.size())
        {
            // We have wrapped. That should not happen.

            mWritePointer = 0;
            return "Too many characters";
        }
        return std::nullopt;
    }

    constexpr
    std::expected<AbsTime, std::string_view>
    parseBuffer()
    {
        if (mWritePointer < 4)
        {
            // We expect to be at least one second away from 1970
            return std::unexpected("Too small value");
        }
        const auto maybeParsedTime = stringToInt(std::string_view(mParseBuffer.begin(), mWritePointer));
        reset();
        return maybeParsedTime;
    }

    constexpr
    void
    reset()
    {
        mWritePointer = 0;
        mParseBuffer.fill(0);   // not really necessary
    }

private:
    static constexpr size_t max_uint64_digits = ceil(log10(std::numeric_limits<AbsTime>::max()));
    static constexpr std::string_view expected_format {"1746090482222"};    // ms
    static_assert(max_uint64_digits >= expected_format.size());

    size_t mWritePointer;
    std::array<char, max_uint64_digits + 1> mParseBuffer;
};

// static_assert(*TimeParser::convertCharacterToNumber('3') == 3);

// ----------------------------------------------------------

class TimeZoneParser
{
public:
    struct StringWithLength
    {
        const char* str;
        size_t size;
    };

    constexpr
    bool
    consumeCharacter(char c)
    {
        mStringBuffer[mPointer] = c;
        mPointer++;
        if (mPointer >= mStringBuffer.size() - 1) // for zero delimiter
        {
            return false;
        }
        return true;
    }

    constexpr
    void
    reset()
    {
        mPointer = 0;
    }

    constexpr
    StringWithLength
    get()
    {
        // Currently no validation, because I don't know the complete list
        mStringBuffer[mPointer] = 0;
        return {mStringBuffer.data(), mPointer};
    }

private:
    size_t mPointer{0};
    std::array<char, 11+1> mStringBuffer;
};

// ----------------------------------------------------------

class CommandParser
{
    enum class ParseState
    {
        timestamp,
        timezone
        // TODO: enable/disable logging?
    };

public:
    constexpr
    CommandParser(AbsoluteTimeManager& timeManagerReference,
                  std::optional<std::reference_wrapper<ClockDisplay>> maybeDisplayReference = std::nullopt)
                  : timeManager{timeManagerReference},
                    maybeDisplay{maybeDisplayReference}
    {
    }

    constexpr
    std::optional<const char*>
    consumeCharacter(const char& chr)
    {
        // currently, there is only one command,
        // so there is no need for a header.
        // But here it would be checked with a switch and state.
        if (chr == ' ')
        {
            // transition from timestamp
            mState = ParseState::timezone;
            return " Now at timezone ";
        }

        if (chr == '\n')
        {
            // see that as an "apply"
            std::optional<const char*> maybeMsg;
            const auto [timezone_identifier, len] = mTzParser.get();
            if (len > 0)
            {

                maybeMsg = string("Setting timezone '%s', ", timezone_identifier);
                setenv("TZ", timezone_identifier, 1);
                // I currently don't know a way to check for success / correct timezone
                tzset();
            }
            const auto maybeParsedTime_ms = mTimeParser.parseBuffer();
            if (maybeParsedTime_ms)
            {
                // OK is the magic ACK value for the settime.py script
                maybeMsg = string("Applying timestamp %llu: OK\r\n", *maybeParsedTime_ms);
                if (maybeDisplay)
                {
                    maybeDisplay->get().showInfo("Set Time", {.end_wait_us = 200'000});
                }

                timeManager.setAbsoluteTime_ms(*maybeParsedTime_ms, clocksource::Internal::getTimeSinceReferenceStable_us());
            }
            else
            {
                maybeMsg = string("Could not parse timestamp: %.*s\r\n",
                        maybeParsedTime_ms.error().size(),
                        maybeParsedTime_ms.error().data());
            }
            reset();
            return maybeMsg;
        }

        // any other char

        if (!isprint(chr) || chr == '\r')
        {
            // ignore unprintables and \r for now
            return std::nullopt; //string("unprintable char (%02X)", chr);
        }
        switch (mState)
        {
        case ParseState::timestamp:
            if (const auto maybeFailReason = mTimeParser.consume_character_ms(chr))
            {
                reset();
                return string("Could not consume time character '%c (%X)': %.*s\r\n",
                        chr, chr,
                        maybeFailReason->size(),
                        maybeFailReason->data());
            }
            break;
        case ParseState::timezone:
            if (!mTzParser.consumeCharacter(chr))
            {
                reset();
                return string("Could not consume timezone character %c (%X)\r\n", chr, chr);
            }
        }

        return string("%c", chr);
    }

private:
    void
    reset()
    {
        mState = ParseState::timestamp;
        mTimeParser.reset();
        mTzParser.reset();
    }

    template<class...Args>
    std::optional<const char*>
    string(const char* format, Args&&...args)
    {
        const int chars = std::snprintf(mPrintBuffer.begin(), mPrintBuffer.size(), format, std::forward<Args>(args)...);
        if (chars > 0)
        {
            return mPrintBuffer.begin();
            // return std::string_view{mPrintBuffer.begin(), static_cast<unsigned int>(chars)};
        }
        else
        {
            return std::nullopt;
        }
    }

    ParseState mState;

    std::optional<std::reference_wrapper<ClockDisplay>> maybeDisplay;
    AbsoluteTimeManager& timeManager;
    TimeParser mTimeParser;
    TimeZoneParser mTzParser;

    std::array<char, 128> mPrintBuffer;
};