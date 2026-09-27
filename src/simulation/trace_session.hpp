#pragma once

namespace simulation::trace_session {
#if defined(VESTA_SHOT_TRACE_ENABLED) && VESTA_SHOT_TRACE_ENABLED
void initialize();
void shutdown() noexcept;
#else
inline void initialize() {}
inline void shutdown() noexcept {}
#endif
}
