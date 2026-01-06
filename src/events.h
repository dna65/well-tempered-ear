#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include "midi.h"

struct NoAvailableSDLEvents {};

template<typename T>
auto EventType() -> uint32_t
{
    const static uint32_t event_type = [] {
        uint32_t event = SDL_RegisterEvents(1);
        if (event == std::numeric_limits<uint32_t>::max())
            throw NoAvailableSDLEvents {};
        return event;
    }();
    return event_type;
}

template<typename T>
auto BaseEvent() -> SDL_CommonEvent
{
    return {
        .type = EventType<T>(),
        .timestamp = SDL_GetTicksNS()
    };
}

struct MIDIInputEvent
{
    const SDL_CommonEvent base_event = BaseEvent<MIDIInputEvent>();
    midi::EventType type;
    uint8_t note, velocity, channel;
};

struct MIDIPlayerEndEvent
{
    const SDL_CommonEvent base_event = BaseEvent<MIDIPlayerEndEvent>();
};
