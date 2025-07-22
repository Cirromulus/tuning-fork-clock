#pragma once

#include <include/config.hpp>

#include <array>
#include <string_view>
#include <optional>

// -----------------------------------------------------------

class AbsoluteTimeManager
{
public:
    struct MixedPrecisionTime
    {
        AbsTime mMicroseconds;
        double mSubMicroseconds; // the fraction after the decimal

        constexpr
        MixedPrecisionTime(const AbsTime& microseconds = 0, const double& subMicroseconds = 0)
            : mMicroseconds{microseconds}, mSubMicroseconds{subMicroseconds}
        {
            normalize();
        }

        constexpr
        operator const AbsTime&() const
        {
            return mMicroseconds;
        }

        constexpr
        MixedPrecisionTime operator+(const AbsTime& microseconds) const
        {
            return MixedPrecisionTime{mMicroseconds + microseconds, mSubMicroseconds};
        }

        constexpr
        MixedPrecisionTime operator+(const double& microseconds) const
        {
            return MixedPrecisionTime{mMicroseconds, mSubMicroseconds + microseconds};
        }

        constexpr
        void operator+=(const AbsTime& microseconds)
        {
            *this = *this + microseconds;
        }

        constexpr
        void operator+=(const double& microseconds)
        {
            *this = *this + microseconds;
        }

    private:
        constexpr void
        normalize()
        {
            const AbsTime subMicrosRounded = llround(mSubMicroseconds);
            mMicroseconds += subMicrosRounded;
            mSubMicroseconds = mSubMicroseconds - subMicrosRounded;
        }
    };

    struct AbsoluteTimeSet
    {
        AbsTime timestamp_us;
        // This is only for tracking the drift since setting absolute time.
        // Not really necessary for this function.
        std::optional<DiffTime> driftSinceBootUntilUpdate_us;
    };

public:
    constexpr
    void
    setAbsoluteTime_ms(const AbsTime& abs_ms, const std::optional<AbsTime>& maybeTrueBootTime_us = std::nullopt)
    {
        // This resets current delta to count from last abs update
        const std::optional<DiffTime> maybeCurrentDrift_us =
            maybeTrueBootTime_us.transform([this](const DiffTime& trueBootTime_us){ return trueBootTime_us - getElapsedTimeSinceBoot_us();});
        mAbsoluteTime = AbsoluteTimeSet{abs_ms * 1'000, maybeCurrentDrift_us};

        mElapsedTimeSinceAbsolute_us = 0;
    }

    constexpr
    void
    increaseDelta_us(const double& delta)
    {
        mElapsedTimeSinceAbsolute_us += delta;
        mElapsedTimeSinceBoot_us += delta;
    }

    constexpr
    std::optional<AbsTime>
    getAbsoluteTime_us() const
    {
        if (mAbsoluteTime)
        {
            return mAbsoluteTime->timestamp_us + mElapsedTimeSinceAbsolute_us;
        }
        return std::nullopt;
    }

    constexpr
    std::optional<DiffTime>
    getDriftWhenAbsoluteTimeWasSet_us() const
    {
        if (mAbsoluteTime && mAbsoluteTime->driftSinceBootUntilUpdate_us)
        {
            return *mAbsoluteTime->driftSinceBootUntilUpdate_us;
        }
        return std::nullopt;
    }

    // will be used when we don't have an absolute time set
    constexpr
    const AbsTime&
    getElapsedTimeSinceBoot_us() const
    {
        return mElapsedTimeSinceBoot_us;
    }

private:
    // will stay constant once set (or re-set)
    std::optional<AbsoluteTimeSet> mAbsoluteTime{std::nullopt};

    // Offset to boot time
    MixedPrecisionTime mElapsedTimeSinceBoot_us{};
    // Offset to absolute time
    MixedPrecisionTime mElapsedTimeSinceAbsolute_us{};
};


// static_assert(AbsoluteTimeManager::MixedPrecisionTime{1, 0.4}.subMicroseconds() == 1.4);
static_assert(AbsoluteTimeManager::MixedPrecisionTime{1, 0.4} == 1);
static_assert((AbsoluteTimeManager::MixedPrecisionTime{1, 0.4} + .1) == 2);
static_assert((AbsoluteTimeManager::MixedPrecisionTime{1, 0.4} + 1.1) == 3);