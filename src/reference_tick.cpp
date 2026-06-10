
#include "reference_tick.hpp"

#include <pico/stdlib.h>

namespace clocksource
{

AbsTime
Internal::getCurrentReferenceTicks()
{
    return time_us_64();
}

AbsTime
Internal::getTimeSinceReferenceStable_us()
{
    // I did not do the ticks() / config::referenceClockFrequency * 1'000'000 to avoid multiplication overflow
    return getCurrentReferenceTicks();
}

} // namespace clocksource