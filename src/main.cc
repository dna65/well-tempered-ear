#define SDL_MAIN_USE_CALLBACKS 1

#include "app.h"
#include "events.h"
#include "midi.h"
#include "sound.h"
#include "usb.h"

#include <fmt/format.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <random>

constexpr int SAMPLE_RATE = 64000;

auto SDL_AppInit_Safe(void** appstate, int argc, char** argv) -> SDL_AppResult
{
    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO)) {
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
                .generator { .sample_rate = spec.freq },
            },
            .file_playback {
                .player { midi::PlayerMode::FILE_PLAYBACK },
                .generator { .sample_rate = spec.freq },
            }
        },
        .window { SDL_CreateWindow("The Well Tempered Ear", 800, 600, 0) }
    };

    if (ctx->window == nullptr) {
        fmt::print("Failed to create window: {}\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

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

    if (auto result = ctx->SetupMIDIControllerConnection(); result.is_error()) {
        fmt::print("Couldn't find device for live MIDI playback\n");
    }

    if (ctx->device_handle.dev_handle)
        SDL_ResumeAudioStreamDevice(sound_ctx.live_playback.stream.get());

    std::string_view exercises_file_path = argc >= 2 ? argv[1] : "exercises.txt";
    constexpr std::string_view major_cadence = "midis/cadences/major.mid";
    constexpr std::string_view minor_cadence = "midis/cadences/minor.mid";

    if (ctx->LoadResources(exercises_file_path, major_cadence, minor_cadence).is_error())
        return SDL_APP_FAILURE;

    SDL_ResumeAudioStreamDevice(sound_ctx.file_playback.stream.get());

    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    // Disable all window-related SDL events
    for (uint32_t event_type = 0x200; event_type < 0x300; ++event_type)
        SDL_SetEventEnabled(event_type, false);

    fmt::print("Press Q to quit\n");

    return SDL_APP_CONTINUE;
}

auto SDL_AppInit(void** appstate, int argc, char** argv) -> SDL_AppResult
{
    try {
        return SDL_AppInit_Safe(appstate, argc, argv);
    } catch (std::exception& e) {
        fmt::print("Exception occurred: {}\n", e.what());
        return SDL_APP_FAILURE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto* ctx = static_cast<AppContext*>(appstate);

    delete ctx;
    usb::Exit();
}

auto SDL_AppIterate_Safe(void* appstate) -> SDL_AppResult
{
    constexpr static uint64_t MS_PER_FRAME = 1000 / 30;
    static uint64_t ms_elapsed = SDL_GetTicks();

    uint64_t time_diff = SDL_GetTicks() - ms_elapsed;
    if (time_diff < MS_PER_FRAME)
        SDL_Delay(MS_PER_FRAME - time_diff);

    ms_elapsed = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

auto SDL_AppIterate(void* appstate) -> SDL_AppResult
{
    try {
        return SDL_AppIterate_Safe(appstate);
    } catch (std::exception& e) {
        fmt::print("Exception occurred: {}\n", e.what());
        return SDL_APP_FAILURE;
    }
}

auto SDL_AppEvent_Safe(void* appstate, SDL_Event* event) -> SDL_AppResult
{
    auto* ctx = static_cast<AppContext*>(appstate);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN: {
        auto* ev = reinterpret_cast<SDL_KeyboardEvent*>(event);
        switch (ev->key) {
        case SDLK_Q:
            return SDL_APP_SUCCESS;
        case SDLK_R:
            ctx->BeginExercise();
            break;
        default:
            break;
        }
        return SDL_APP_CONTINUE;
    }
    default:
        break;
    }

    if (event->type == MIDIPlayerEndEvent::EVENT_NUMBER) {
        ctx->MIDIEnded();
    } else if (event->type == MIDIInputEvent::EVENT_NUMBER) {
        auto* ev = reinterpret_cast<MIDIInputEvent*>(event);

        if (ev->type == midi::EventType::NOTE_ON)
            ctx->game.InputNote(ev->note);

        ctx->PlayLiveMIDIEvent(*ev);
    }

    return SDL_APP_CONTINUE;
}

auto SDL_AppEvent(void* appstate, SDL_Event* event) -> SDL_AppResult
{
    try {
        return SDL_AppEvent_Safe(appstate, event);
    } catch (std::exception& e) {
        fmt::print("Exception occurred: {}\n", e.what());
        return SDL_APP_FAILURE;
    }
}
