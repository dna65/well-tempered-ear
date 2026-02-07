#pragma once

#include "events.h"
#include "game.h"
#include "midi.h"
#include "sound.h"
#include "usb.h"

#include <SDL3/SDL_video.h>

#include <tb/tb.h>

#include <memory>

using UWindow = std::unique_ptr<SDL_Window, tb::deleter<SDL_DestroyWindow>>;

struct LoadResourcesError {};

struct AppContext
{
    SoundContext sound_ctx;
    Resources resources;
    Game game { resources };
    usb::DeviceHandle device_handle;
    usb::PollingContext polling_ctx {};
    UWindow window;

    auto LoadResources(std::string_view exercises_path,
        std::string_view major_cadence, std::string_view minor_cadence)
    -> tb::error<LoadResourcesError>;
    auto SetupMIDIControllerConnection() -> tb::error<usb::Error>;
    void PlayLiveMIDIEvent(const MIDIInputEvent& event);
    void BeginExercise();
    void MIDIEnded();
};
