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
        // todo
        reset();
        return std::unexpected("Not implemented, lol");
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
                printf("Applying timestamp %d\n", *maybeParsedTime_ms);
                if (maybeDisplay)
                {
                    maybeDisplay->get().showInfo("Set Time");
                }
                timeManager.setAbsoluteTime_ms(*maybeParsedTime_ms);
                printf("OK");   // this is the magic ACK value
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