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

    auto& live_playback = static_cast<PlaybackUnit&>(sound_ctx->live_playback);

    SDL_AudioStream* stream = live_playback.stream;
    midi::Player& player = live_playback.player;
    Generator& generator = live_playback.generator;

    int bytes_queued = SDL_GetAudioStreamQueued(stream);
    if (bytes_queued == -1) {
        fmt::print("Error inspecting audio stream: {}\n", SDL_GetError());
        return;
    }

    size_t samples_queued = static_cast<size_t>(bytes_queued) / sizeof(Sample);

    bool should_clear_stream = false;

    {
    std::lock_guard<std::mutex> guard(sound_ctx->lock);
    for (int i = 0; i < transfer->actual_length; i += midi::MESSAGE_SIZE) {
        auto* message = static_cast<uint8_t*>(transfer->buffer + i);

        auto cin = static_cast<midi::CodeIndexNumber>(message[0] & 0x0F);

        switch (cin) {
        case midi::CodeIndexNumber::NOTE_ON:
            player.PlayEvent(midi::Event {
                .type = midi::EventType::NOTE_ON,
                .note_event = {
                    .note = message[2],
                    .velocity = message[3]
                }
            });
            should_clear_stream = true;
            break;
        case midi::CodeIndexNumber::NOTE_OFF:
            player.PlayEvent(midi::Event {
                .type = midi::EventType::NOTE_OFF,
                .note_event = {
                    .note = message[2]
                }
            });
            should_clear_stream = true;
            break;
        default:
            break;
        }
    }
    }

    generator.sample_point_ -= samples_queued;

    if (should_clear_stream)
        SDL_ClearAudioStream(stream);
}

constexpr int SAMPLE_RATE = 64000;

auto LivePlayback(SoundContext& ctx) -> tb::result<usb::DeviceHandle, usb::Error>
{
    if (auto result = usb::Init(); result.is_error()) {
        fmt::print("Failed to initialise libusb: {}\n", result.get_error().What());
        return result.get_error();
    };

    auto list_or_err = usb::IndexDevices();
    if (list_or_err.is_error()) {
        fmt::print("Failed to index USB devices: {}\n",
            list_or_err.get_error().What());
        return list_or_err.get_error();
    }

    fmt::print("Indexed USB devices\n");
    auto devs_or_err = usb::SearchMIDIDevices(list_or_err.get_unchecked());
    if (devs_or_err.is_error()) {
        fmt::print("Failed to search for midi devices: {}\n",
            devs_or_err.get_error().What());
        return devs_or_err.get_error();
    }

    const std::vector<usb::DeviceEntry>& entries = devs_or_err.get_unchecked();
    fmt::print("Found {} midi devices\n", entries.size());

    usb::DeviceHandle handle;

    if (entries.empty()) {
        fmt::print("No MIDI devices found\n");
        return usb::Error { LIBUSB_ERROR_NO_DEVICE };
    }

    const usb::DeviceEntry& entry = entries[0];
    fmt::print("Device '{}' from '{}'\n", entry.product_name,
                entry.manufacturer);

    auto handle_or_err = entry.Open(&ctx);
    if (handle_or_err.is_error()) {
        fmt::print("Error opening MIDI device for IO: {}\n",
            handle_or_err.get_error().What());
        return handle_or_err.get_error();
    }

    handle = std::move(handle_or_err.get_mut_unchecked());

    return handle;
}

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        fmt::print("Failed to initialise SDL\n");
        return 1;
    }

    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32,
        .channels = 1,
        .freq = SAMPLE_RATE
    };

    SoundContext ctx = {
        .live_playback {
            .player { midi::PlayerMode::LIVE_PLAYBACK },
            .generator { spec.freq }
        },
        .file_playback {
            .player { midi::PlayerMode::FILE_PLAYBACK },
            .generator { spec.freq }
        }
    };

    ctx.live_playback.stream
        = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
            Audio_LiveCallback, &ctx);
    ctx.file_playback.stream
        = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
            Audio_FileCallback, &ctx);

    if (!ctx.live_playback.stream || !ctx.file_playback.stream) {
        fmt::print("Failed to open audio streams: {}\n", SDL_GetError());
        return 1;
    }

    tb::scoped_guard clean_up = [&ctx] {
        SDL_DestroyAudioStream(ctx.live_playback.stream);
        SDL_DestroyAudioStream(ctx.file_playback.stream);
    };

    auto handle_or_err = LivePlayback(ctx);
    tb::scoped_guard close_usb = [&handle_or_err] {
        if (handle_or_err.is_ok())
            handle_or_err.get_mut_unchecked().Close();
        usb::Exit();
    };

    if (handle_or_err.is_error() && argc < 2) {
        fmt::print("Couldn't find device for live MIDI playback - exiting\n");
        return 1;
    }

    if (handle_or_err.is_ok()) {
        handle_or_err.get_mut_unchecked().ReceiveBulkPackets(ReadUSBPacket);
        SDL_ResumeAudioStreamDevice(ctx.live_playback.stream);
    }

    midi::MIDI midi;
    if (argc >= 2) {
        auto result = midi::MIDI::FromFile(argv[1]);
        if (result.is_error()) {
            midi::Error e = result.get_error();
            fmt::print("MIDI error at pos {}: {}\n", e.byte_position, e.What());
            return 1;
        }

        midi = std::move(result.get_mut_unchecked());
        ctx.file_playback.player.SetMIDI(midi);
        fmt::print("Loaded MIDI\n");

        SDL_ResumeAudioStreamDevice(ctx.file_playback.stream);
    }

    usb::PollingContext polling_ctx {};
    getchar();

    return 0;
}
