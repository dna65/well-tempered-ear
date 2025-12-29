#define SDL_MAIN_USE_CALLBACKS 1

#include "app.h"
#include "midi.h"
#include "sound.h"
#include "usb.h"

#include <fmt/format.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

constexpr int SAMPLE_RATE = 64000;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        fmt::print("Failed to initialise SDL: {}\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (auto result = usb::Init(); result.is_error()) {
        fmt::print("Failed to initialise libusb: {}\n", result.get_error().What());
        return SDL_APP_FAILURE;
    }

    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32,
        .channels = 1,
        .freq = SAMPLE_RATE
    };

    AppContext* ctx = new AppContext {
        .sound_ctx = {
            .live_playback {
                .player { midi::PlayerMode::LIVE_PLAYBACK },
                .generator { spec.freq },
                .stream {
                }
            },
            .file_playback {
                .player { midi::PlayerMode::FILE_PLAYBACK },
                .generator { spec.freq },
                .stream {
                }
            }
        }
    };

    *appstate = ctx;
    SoundContext& sound_ctx = ctx->sound_ctx;

    sound_ctx.live_playback.stream.reset(
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec, Audio_LiveCallback, &sound_ctx)
    );
    sound_ctx.file_playback.stream.reset(
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec, Audio_FileCallback, &sound_ctx)
    );

    if (!sound_ctx.live_playback.stream || !sound_ctx.file_playback.stream) {
        fmt::print("Failed to open audio streams: {}\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (auto result = ctx->SetupMIDIControllerConnection();
        result.is_error() && argc < 2) {
        fmt::print("Couldn't find device for live MIDI playback - exiting\n");
        return SDL_APP_FAILURE;
    }

    if (ctx->device_handle.dev_handle)
        SDL_ResumeAudioStreamDevice(sound_ctx.live_playback.stream.get());

    if (argc >= 2) {
        if (auto result = ctx->LoadMIDIFile(argv[1]); result.is_error()) {
            midi::Error err = result.get_error();
            fmt::print("Failed to load MIDI: Error at byte {}: {}\n",
                err.byte_position, err.What());
            return SDL_APP_FAILURE;
        }
        SDL_ResumeAudioStreamDevice(sound_ctx.file_playback.stream.get());
    }

    fmt::print("Press Q to quit\n");

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto* ctx = static_cast<AppContext*>(appstate);

    ctx->device_handle.Close();
    delete ctx;
    usb::Exit();
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    int c = getchar();
    if (c == EOF || c == 'q' || c == 'Q')
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    return SDL_APP_CONTINUE;
}
