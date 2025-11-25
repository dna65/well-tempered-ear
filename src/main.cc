#include "midi.h"
#include "sound.h"
#include "usb.h"

#include <fmt/format.h>

#include <SDL3/SDL.h>

void ReadUSBPacket(libusb_transfer* transfer)
{
    auto* sound_ctx = static_cast<SoundContext*>(
        static_cast<usb::DeviceHandle*>(transfer->user_data)->user_data
    );

    int samples_queued = SDL_GetAudioStreamQueued(sound_ctx->astream);
    SDL_ClearAudioStream(sound_ctx->astream);

    std::lock_guard<std::mutex> guard(sound_ctx->lock);
    for (int i = 0; i < transfer->actual_length; i += midi::MESSAGE_SIZE) {
        auto* message = static_cast<uint8_t*>(transfer->buffer + i);

        auto cin = static_cast<midi::CodeIndexNumber>(message[0] & 0x0F);

        if (cin == midi::CodeIndexNumber::SINGLE_BYTE) continue;

        switch (cin) {
        case midi::CodeIndexNumber::NOTE_ON:
            sound_ctx->midi_player.PlayEvent(midi::Event {
                .type = midi::EventType::NOTE_ON,
                .note_event = {
                    .note = message[2],
                    .velocity = message[3]
                }
            });
            break;
        case midi::CodeIndexNumber::NOTE_OFF:
            sound_ctx->midi_player.PlayEvent(midi::Event {
                .type = midi::EventType::NOTE_OFF,
                .note_event = {
                    .note = message[2]
                }
            });
            break;
        default:
            break;
        }
    }

    sound_ctx->generator.sample_point_ -= samples_queued / sizeof(float);
}

constexpr int SAMPLE_RATE = 64000;

int LivePlayback(SoundContext& ctx)
{
    if (auto result = usb::Init(); result.is_error()) {
        fmt::print("Failed to initialise libusb: {}\n", result.get_error().What());
        return 1;
    };

    tb::scoped_guard clean_up = [] {
        usb::Exit();
    };

    auto list_or_err = usb::IndexDevices();
    if (list_or_err.is_error()) {
        fmt::print("Failed to index USB devices\n");
        return 1;
    }

    fmt::print("Indexed USB devices\n");
    auto devs_or_err = usb::SearchMIDIDevices(list_or_err.get_unchecked());
    if (devs_or_err.is_error()) {
        fmt::print("Failed to search for midi devices\n");
        return 1;
    }

    const std::vector<usb::DeviceEntry>& entries = devs_or_err.get_unchecked();
    fmt::print("Found {} midi devices\n", entries.size());

    usb::DeviceHandle handle;

    if (entries.empty()) {
        fmt::print("No MIDI devices found\n");
        return 1;
    }

    const usb::DeviceEntry& entry = entries[0];
    fmt::print("Device '{}' from '{}'\n", entry.product_name,
                entry.manufacturer);

    auto handle_or_err = entry.Open(&ctx);
    if (handle_or_err.is_error()) {
        fmt::print("Error opening MIDI device for IO: {}\n",
            handle_or_err.get_error().What());
        return 1;
    }

    handle = std::move(handle_or_err.get_mut_unchecked());

    handle.ReceiveBulkPackets(ReadUSBPacket);

    ctx.midi_player.mode = midi::PlayerMode::LIVE_PLAYBACK;
    SDL_ResumeAudioStreamDevice(ctx.astream);

    fmt::print("Press any key to quit\n");

    {
        usb::PollingContext poll_ctx {};
        getchar();
        handle.Close();
    }

    return 0;
}

int FilePlayback(std::string_view filename, SoundContext& ctx)
{
    auto result = midi::MIDI::FromFile(filename);

    if (result.is_error()) {
        midi::Error e = result.get_error();
        fmt::print("MIDI error at pos {}: {}\n", e.byte_position, e.What());
        return 1;
    }

    midi::MIDI midi = result.get_unchecked();
    ctx.midi_player.SetMIDI(midi);
    fmt::print("Loaded MIDI\n");

    ctx.midi_player.mode = midi::PlayerMode::FILE_PLAYBACK;
    SDL_ResumeAudioStreamDevice(ctx.astream);

    fmt::print("Press any key to quit\n");
    getchar();

    return 0;
}

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        fmt::print("Failed to initialise SDL\n");
        return 1;
    }

    SoundContext ctx = {
        .generator { SAMPLE_RATE }
    };

    SDL_AudioSpec spec = {
        .channels = 1,
        .format = SDL_AUDIO_F32,
        .freq = SAMPLE_RATE
    };

    ctx.astream
        = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                    SoundCallback, &ctx);

    if (!ctx.astream) {
        fmt::print("Failed to open audio stream\n");
        return 1;
    }

    int return_code = 0;

    if (argc < 2) {
        fmt::print("Well Tempered Ear - Live playback\n");
        return_code = LivePlayback(ctx);
    } else {
        fmt::print("Well Tempered Ear - MIDI file playback\n");
        return_code = FilePlayback(argv[1], ctx);
    }

    SDL_DestroyAudioStream(ctx.astream);

    return return_code;
}
