#pragma once
#include <SDL3/SDL_log.h>

#define LC_LOG(...) SDL_Log(__VA_ARGS__)
#define LC_WARN(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LC_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
