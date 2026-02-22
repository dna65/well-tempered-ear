#include "events.h"

#include <SDL3/SDL_init.h>

#include <cstdint>
#include <limits>

auto NextAvailableEventNumber() -> uint32_t
{
    if (!SDL_WasInit(SDL_INIT_EVENTS))
        SDL_Init(SDL_INIT_EVENTS);

    uint32_t event = SDL_RegisterEvents(1);
    if (event == std::numeric_limits<uint32_t>::min())
        throw std::runtime_error { "No available SDL events" };
    return event;
}
