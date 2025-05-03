
#include "reference_tick.hpp"

#include <pico/stdlib.h>

namespace clocksource
{

AbsTime
getCurrentReferenceTicks()
{
    // Currently, only internal clack is a reference.
    // TODO: Make PIO counter for external reference
    return time_us_64();
}

AbsTime
getTimeSinceReferenceStable_us()
{
    // I did not do the ticks() / config::referenceClockFrequency * 1'000'000 to avoid multiplication overflow
    return getCurrentReferenceTicks();
}


} // namespace clocksource