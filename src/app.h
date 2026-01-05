#pragma once

#include "events.h"
#include "game.h"
#include "midi.h"
#include "sound.h"
#include "tb.h"
#include "usb.h"

#include <SDL3/SDL_video.h>

struct AppContext
{
    SoundContext sound_ctx;
    Game game;
    usb::DeviceHandle device_handle;
    midi::MIDI midi_file;
    usb::PollingContext polling_ctx {};
    SDL_Window* window;

    auto SetupMIDIControllerConnection() -> tb::error<usb::Error>;
    auto LoadMIDIFile(std::string_view path) -> tb::error<midi::Error>;
    void PlayLiveMIDIEvent(const MIDIInputEvent& event);
    void BeginExercise();
    void MIDIEnded();
};
