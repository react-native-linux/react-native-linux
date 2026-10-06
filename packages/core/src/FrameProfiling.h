#pragma once

// The Tracy zones the frame loop names (#20). TracyClient defines TRACY_ENABLE when RNL_ENABLE_TRACY links it in;
// otherwise the two macros the loop uses expand to nothing and no Tracy header is needed. See *Tracy* in
// docs/cpp-toolchain.md.
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name)
#define FrameMark
#endif
