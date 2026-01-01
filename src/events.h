#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

template<typename T>
auto EventType() -> uint32_t
{
    const static uint32_t event_type = SDL_RegisterEvents(1);
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
    SDL_CommonEvent base_event = BaseEvent<MIDIInputEvent>();
    midi::EventType type;
    uint8_t note, velocity, channel;
};

struct MIDIPlayerEndEvent
{
    SDL_CommonEvent base_event = BaseEvent<MIDIPlayerEndEvent>();
};
