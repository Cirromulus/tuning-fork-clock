#pragma once

#include <inttypes.h>
#include <stddef.h>

// TODO: Besides this duration averager,
// I can imagine a "max resolution" averager that only divides
// just before integer overflow


template <typename Underlying = int64_t>
class
Averager
{
    size_t mAveragingNumSamples;
    Underlying mCounts;
    size_t mInputtedSamples;

public:
    constexpr Averager(size_t averagingNumSamples) :
        mAveragingNumSamples{averagingNumSamples}, mCounts{0}, mInputtedSamples{0}
    {
    }

    constexpr bool
    feed(Underlying count)
    {
        mCounts += count;
        mInputtedSamples++;

        return mInputtedSamples >= mAveragingNumSamples;
    }

    constexpr auto
    getInputtetSamples() const
    {
        return mInputtedSamples;
    }

    constexpr auto
    getCounts() const
    {
        return mCounts;
    }

    constexpr void
    reset()
    {
        mCounts = 0;
        mInputtedSamples = 0;
    }
};