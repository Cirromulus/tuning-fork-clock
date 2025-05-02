#pragma once

#include <array>
#include <string_view>
#include <optional>

using TimeType = uint64_t;


// -----------------------------------------------------------

class AbsoluteTimeManager
{
public:
    constexpr
    void
    setAbsoluteTime_ms(const TimeType& abs_ms)
    {
        // This resets current delta to count from last abs update
        mAbsoluteTime_us = abs_ms * 1'000;
        mElapsedTimeSinceAbsolute_us = 0;
    }

    constexpr
    void
    increaseDelta_us(const TimeType& delta)
    {
        mElapsedTimeSinceAbsolute_us += delta;
        mElapsedTimeSinceBoot_us += delta;
    }

    constexpr
    std::optional<TimeType>
    getAbsoluteTime_us() const
    {
        if (mAbsoluteTime_us)
        {
            return *mAbsoluteTime_us + mElapsedTimeSinceAbsolute_us;
        }
        return std::nullopt;
    }

    // will be used when we don't have an absolute time set
    constexpr
    const TimeType&
    getElapsedTimeSinceBoot_us() const
    {
        return mElapsedTimeSinceBoot_us;
    }

private:
    std::optional<TimeType> mAbsoluteTime_us{std::nullopt};
    TimeType mElapsedTimeSinceBoot_us{0};
    TimeType mElapsedTimeSinceAbsolute_us{0};
};
