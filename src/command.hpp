#pragma once

#include "absolute_time.hpp"
#include "display.hpp"    // a little bit ugly from SW design perspective

#include <string_view>
#include <limits>
#include <cmath>
#include <optional>
#include <expected>
#include <functional>
#include <cctype>

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

    static constexpr std::expected<TimeType, std::string_view>
    stringToInt(const std::string_view& input)
    {
        if (input.size() == 0)  // sanity check if there is any data in the Slice
        {
            return std::unexpected("empty parse string");
        }
        TimeType result = 0;

        // This constant defines the maximal value that can still be multiplied with 'base' and
        // contained in the <TimeType>-Range.
        constexpr TimeType maxSafeMultiplicationValue = std::numeric_limits<TimeType>::max() / 10;
        constexpr bool hasTimeTypeNegativeValues = std::numeric_limits<TimeType>::min() < 0;

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
            // Checks if the remaining space in the <TimeType> range is bigger than the number to add.
            // This makes sure that the new result fits into the <TimeType> range.
            const uint8_t& currentDigit = maybeNextNumber.value();

            if ((std::numeric_limits<TimeType>::max() - result) < currentDigit)
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
    std::expected<TimeType, std::string_view>
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
    static constexpr size_t max_uint64_digits = ceil(log10(std::numeric_limits<TimeType>::max()));
    static constexpr std::string_view expected_format {"1746090482222"};    // ms
    static_assert(max_uint64_digits >= expected_format.size());

    size_t mWritePointer;
    std::array<char, max_uint64_digits + 1> mParseBuffer;
};

// static_assert(*TimeParser::convertCharacterToNumber('3') == 3);

// ----------------------------------------------------------

class CommandParser
{
public:
    constexpr
    CommandParser(AbsoluteTimeManager& timeManagerReference,
                  std::optional<std::reference_wrapper<ClockDisplay>> maybeDisplayReference = std::nullopt)
                  : timeManager{timeManagerReference}, maybeDisplay{maybeDisplayReference}
    {
    }

    constexpr
    void
    consumeCharacter(const char& chr)
    {
        // currently, there is only one command,
        // so there is no need for a header.
        // But here it would be checked with a switch and state.

        if (chr == '\n')
        {
            // see that as an "apply"
            const auto maybeParsedTime_ms = mTimeParser.parseBuffer();
            if (maybeParsedTime_ms)
            {
                // OK is the magic ACK value
                printf("Applying timestamp %llu: OK\n", *maybeParsedTime_ms);
                if (maybeDisplay)
                {
                    maybeDisplay->get().showInfo("Set Time");
                }
                timeManager.setAbsoluteTime_ms(*maybeParsedTime_ms);
            }
            else
            {
                printf("Could not parse timestamp: %.*s\n",
                        maybeParsedTime_ms.error().size(),
                        maybeParsedTime_ms.error().data());
            }
            return;
        }

        // any other char

        if (!isprint(chr) || chr == '\r')
        {
            // ignore unprintables and \r for now
            return;
        }

        if (const auto maybeFailReason = mTimeParser.consume_character_ms(chr))
        {
            printf("Could not consume time character: %.*s\n",
                    maybeFailReason->size(),
                    maybeFailReason->data());
        }

    }

private:
    std::optional<std::reference_wrapper<ClockDisplay>> maybeDisplay;
    AbsoluteTimeManager& timeManager;
    TimeParser mTimeParser;
};