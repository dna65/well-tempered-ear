#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include "midi.h"

template<typename T>
auto BaseEvent() -> SDL_CommonEvent
{
    return {
        .type = T::EVENT_NUMBER,
        .timestamp = SDL_GetTicksNS()
    };
}

auto NextAvailableEventNumber() -> uint32_t;

struct MIDIInputEvent
{
    const static inline uint32_t EVENT_NUMBER = NextAvailableEventNumber();
    const SDL_CommonEvent base_event = BaseEvent<MIDIInputEvent>();
    midi::EventType type;
    uint8_t note, velocity, channel;
};

struct MIDIPlayerEndEvent
{
    const static inline uint32_t EVENT_NUMBER = NextAvailableEventNumber();
    const SDL_CommonEvent base_event = BaseEvent<MIDIPlayerEndEvent>();
};
