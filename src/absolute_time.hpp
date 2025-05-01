#pragma once

#include <array>
#include <string_view>
#include <limits>
#include <cmath>
#include <optional>

class AbsoluteTimeManager
{
public:
    using TimeType = uint64_t;

    class Parser
    {

    public:
        constexpr
        std::optional<TimeType>
        parse_ms(const std::string_view& line)
        {
            // todo
            return std::nullopt;
        }

    private:
        static constexpr size_t max_uint64_digits = ceil(log10(std::numeric_limits<TimeType>::max()));
        static constexpr std::string_view expected_format {"1746090482222"};    // ms
        static_assert(max_uint64_digits >= expected_format.size());

        std::array<char, max_uint64_digits + 1> mParseBuffer;
    };

    constexpr
    void
    setAbsoluteTime_ms(const TimeType& abs_ms)
    {
        // This resets current delta to count from last abs update
        mAbsoluteTime_us = abs_ms * 1'000;
        mTimeDelta_us = 0;
    }

    constexpr
    void
    increaseDelta_us(const TimeType& delta)
    {
        mTimeDelta_us += delta;
    }

    // constexpr
    std::optional<std::string_view>
    getDayMonthYear()
    {
        // todo
        return std::nullopt;
    }

    std::optional<std::string_view>
    getTime()
    {
        // todo
        return std::nullopt;
    }

    // will be used when we don't have an absolute time set
    constexpr
    const TimeType&
    getTimeDelta() const
    {
        return mTimeDelta_us;
    }

private:
    // ??
    std::optional<TimeType> mAbsoluteTime_us{std::nullopt};
    TimeType mTimeDelta_us{0};

    std::array<char, 8> mStringBuffer;
};