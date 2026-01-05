#include "app.h"

#include <fmt/format.h>

#include "events.h"

#include <random>

void ReadUSBPacket(libusb_transfer* transfer)
{
    for (int i = 0; i < transfer->actual_length; i += midi::MESSAGE_SIZE) {
        auto* message = static_cast<uint8_t*>(transfer->buffer + i);

        auto cin = static_cast<midi::CodeIndexNumber>(message[0] & 0x0F);

        switch (cin) {
        case midi::CodeIndexNumber::NOTE_ON: {
            MIDIInputEvent ev {
                .type = midi::EventType::NOTE_ON,
                .note = message[2],
                .velocity = message[3]
            };
            SDL_PushEvent(reinterpret_cast<SDL_Event*>(&ev));
            break;
        }
        case midi::CodeIndexNumber::NOTE_OFF: {
            MIDIInputEvent ev {
                .type = midi::EventType::NOTE_OFF,
                .note = message[2]
            };
            SDL_PushEvent(reinterpret_cast<SDL_Event*>(&ev));
            break;
        }
        default:
            break;
        }
    }
}

auto AppContext::SetupMIDIControllerConnection() -> tb::error<usb::Error>
{
    auto list_or_err = usb::IndexDevices();
    if (list_or_err.is_error()) {
        fmt::print("Failed to index USB devices: {}\n",
            list_or_err.get_error().What());
        return list_or_err.get_error();
    }

    auto devs_or_err = usb::SearchMIDIDevices(list_or_err.get_unchecked());
    if (devs_or_err.is_error()) {
        fmt::print("Failed to search for midi devices: {}\n",
            devs_or_err.get_error().What());
        return devs_or_err.get_error();
    }

    const std::vector<usb::DeviceEntry>& entries = devs_or_err.get_unchecked();
    usb::DeviceHandle handle;

    if (entries.empty())
        return usb::Error { LIBUSB_ERROR_NO_DEVICE };

    const usb::DeviceEntry& entry = entries[0];

    auto handle_or_err = entry.Open(nullptr);
    if (handle_or_err.is_error()) {
        fmt::print("Error opening MIDI device for IO: {}\n",
            handle_or_err.get_error().What());
        return handle_or_err.get_error();
    }

    device_handle = std::move(handle_or_err.get_mut_unchecked());
    device_handle.ReceiveBulkPackets(ReadUSBPacket);

    return tb::ok;
}

auto AppContext::LoadMIDIFile(std::string_view path) -> tb::error<midi::Error>
{
    auto result = midi::MIDI::FromFile(path);
    if (result.is_error())
        return result.get_error();

    midi_file = std::move(result.get_mut_unchecked());
    sound_ctx.file_playback.player.SetMIDI(midi_file);

    return tb::ok;
}

void AppContext::PlayLiveMIDIEvent(const MIDIInputEvent& event)
{
    SDL_AudioStream* stream = sound_ctx.live_playback.stream.get();
    midi::Player& player = sound_ctx.live_playback.player;
    Generator& generator = sound_ctx.live_playback.generator;

    int bytes_queued = SDL_GetAudioStreamQueued(stream);
    if (bytes_queued == -1) {
        fmt::print("Error inspecting audio stream: {}\n", SDL_GetError());
        return;
    }

    size_t samples_queued = static_cast<size_t>(bytes_queued) / sizeof(Sample);

    SDL_ClearAudioStream(stream);

    std::lock_guard<std::mutex> guard(sound_ctx.lock);

    player.PlayEvent({
        .type = event.type,
        .note_event {
            .note = event.note,
            .velocity = event.velocity
        }
    });

    generator.sample_point_ -= samples_queued;
}

void AppContext::BeginExercise()
{
    static std::random_device rand_dev;
    static std::uniform_int_distribution<int> player_transposition(-6, 6);

    if (game.GetState() != GameState::WAIT_FOR_READY)
        return;

    if (game.BeginNewExercise().is_error())
        return;

    midi::Player& player = sound_ctx.file_playback.player;

    player.transposition_offset_ = player_transposition(rand_dev);
    player.SetMIDI(*game.GetCurrentCadenceMIDI());
}

void AppContext::MIDIEnded()
{
    midi::Player& player = sound_ctx.file_playback.player;

    switch (game.GetState()) {
    case GameState::PLAYING_CADENCE:
        player.SetMIDI(game.GetCurrentExercise()->midi);
        game.MIDIEnded();
        break;
    case GameState::PLAYING_EXERCISE:
        fmt::print("Now play it in the key of {}!\n",
            midi::NoteName(game.GetRequiredInputKey()));
        game.MIDIEnded();
        break;
    default:
        break;
    }
}
