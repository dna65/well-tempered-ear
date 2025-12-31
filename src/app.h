#pragma once

#include "game.h"
#include "midi.h"
#include "sound.h"
#include "tb.h"
#include "usb.h"

struct AppContext
{
    SoundContext sound_ctx;
    Game game;
    usb::DeviceHandle device_handle;
    midi::MIDI midi_file;
    usb::PollingContext polling_ctx {};

    auto SetupMIDIControllerConnection() -> tb::error<usb::Error>;
    auto LoadMIDIFile(std::string_view path) -> tb::error<midi::Error>;
};


