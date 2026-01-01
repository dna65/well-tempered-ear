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

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
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
                .generator { spec.freq },
            },
            .file_playback {
                .player { midi::PlayerMode::FILE_PLAYBACK },
                .generator { spec.freq },
            }
        },
        .window = SDL_CreateWindow("The Well Tempered Ear", 800, 600, 0)
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

    if (auto result = ctx->game.LoadExercises(exercises_file_path); result.is_error()) {
        LoadError err = result.get_error();
        fmt::print("Failed to load exercises: {}\n", err.What());
        return SDL_APP_FAILURE;
    }

    if (auto result = ctx->game.LoadCadences(
            "midis/cadences/major.mid",
            "midis/cadences/minor.mid"
        ); result.is_error()) {
        midi::Error err = result.get_error();
        fmt::print("Failed to load cadence midi files: {}\n", err.What());
        return SDL_APP_FAILURE;
    }

    ctx->game.BeginNewExercise().ignore_error();

    std::random_device rd;
    std::uniform_int_distribution<int> dr(-6, 6);
    sound_ctx.file_playback.player.transposition_offset_ = dr(rd);
    sound_ctx.file_playback.player.SetMIDI(ctx->game.GetCurrentExercise()->midi);

    SDL_ResumeAudioStreamDevice(sound_ctx.file_playback.stream.get());

    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    // Disable all window-related SDL events
    for (uint32_t event_type = 0x200; event_type < 0x300; ++event_type)
        SDL_SetEventEnabled(event_type, false);

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
    constexpr uint64_t MS_PER_FRAME = 1000 / 30;
    static uint64_t ms_elapsed = SDL_GetTicks();

    uint64_t time_diff = SDL_GetTicks() - ms_elapsed;
    if (time_diff < MS_PER_FRAME)
        SDL_Delay(MS_PER_FRAME - time_diff);

    ms_elapsed = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* ctx = static_cast<AppContext*>(appstate);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN: {
        auto* ev = reinterpret_cast<SDL_KeyboardEvent*>(event);
        if (ev->key == SDLK_Q)
            return SDL_APP_SUCCESS;
        break;
    }
    default:
        break;
    }

    if (event->type == EventType<MIDIPlayerEndEvent>()) {
        // TODO: Game logic
    } else if (event->type == EventType<MIDIInputEvent>()) {
        auto* ev = reinterpret_cast<MIDIInputEvent*>(event);
        if (ev->type == midi::EventType::NOTE_ON)
            ctx->game.InputNote(ev->note);
    }

    return SDL_APP_CONTINUE;
}
