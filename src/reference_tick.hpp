#pragma once

#include <include/config.hpp>

namespace clocksource
{

AbsTime
getCurrentReferenceTicks();

AbsTime
getTimeSinceReferenceStable_us();

} // namespace clocksource