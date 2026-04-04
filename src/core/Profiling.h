#pragma once

#if defined(HYBRID_ENABLE_TRACY) && HYBRID_ENABLE_TRACY
#include <glad.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>

#define HYBRID_PROFILE_ZONE() ZoneScoped
#define HYBRID_PROFILE_ZONE_N(name) ZoneScopedN(name)
#define HYBRID_PROFILE_FRAME() FrameMark
#define HYBRID_PROFILE_GL_CONTEXT() TracyGpuContext
#define HYBRID_PROFILE_GL_COLLECT() TracyGpuCollect
#define HYBRID_PROFILE_GL_ZONE(name) TracyGpuZone(name)

#else

#define HYBRID_PROFILE_ZONE() ((void)0)
#define HYBRID_PROFILE_ZONE_N(name) ((void)0)
#define HYBRID_PROFILE_FRAME() ((void)0)
#define HYBRID_PROFILE_GL_CONTEXT() ((void)0)
#define HYBRID_PROFILE_GL_COLLECT() ((void)0)
#define HYBRID_PROFILE_GL_ZONE(name) ((void)0)

#endif
