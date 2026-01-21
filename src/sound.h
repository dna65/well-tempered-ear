#pragma once

#include <array>
#include <cmath>
#include <mutex>
#include <span>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "midi.h"

constexpr int DEFAULT_SAMPLE_RATE = 4000;
constexpr float MAX_VELOCITY = UINT8_MAX;
constexpr size_t SAMPLE_BUFFER_SIZE = 4096;

using Sample = float;
using UAudioStream = std::unique_ptr<SDL_AudioStream,
    tb::deleter<SDL_DestroyAudioStream>>;

using WaveFunction = float (*)(float, unsigned, unsigned);
using Harmonic = unsigned;
using Amplitude = uint8_t;

constexpr Amplitude MAX_AMPLITUDE = std::numeric_limits<Amplitude>::max();

template<WaveFunction Fn, Harmonic H = 1, Amplitude A = MAX_AMPLITUDE>
constexpr float Waveform(float freq, unsigned time, unsigned wavelength)
{
    return (static_cast<float>(A) / MAX_AMPLITUDE) * Fn(H * freq, time, wavelength);
}

template<WaveFunction... Fns>
constexpr float CompositeWaveform(float freq, unsigned time, unsigned wavelength)
{
    return (Fns(freq, time, wavelength) + ...);
}

struct Synth
{
    WaveFunction wave_fn = nullptr;
    float decay_constant = 0;
};

namespace waveforms
{

constexpr auto pulse = [] (float freq, unsigned time, unsigned wavelength) {
    float x = time * freq / wavelength;
    float x_i = floor(x);

    float x2 = x - (wavelength * 0.5f) / wavelength;
    float x2_i = floor(x2);

    return (x_i - x) - (x2_i - x2);
};

constexpr auto sine = [] (float freq, unsigned time, unsigned wavelength) {
    return sinf(time * 2.f * M_PI * freq / wavelength);
};

}

constexpr Synth DEFAULT_SYNTH {
    .wave_fn = CompositeWaveform<
        Waveform<waveforms::pulse>, Waveform<waveforms::sine, 2, 128>,
        Waveform<waveforms::sine, 3, 64>
    >,
    .decay_constant = 1 / 0.3f
};

struct Generator
{
    int sample_rate = DEFAULT_SAMPLE_RATE;
    unsigned sample_point = 0;

    auto GenerateSamples(std::span<Sample> dest, size_t count,
                         const midi::Player& midi_status, unsigned sample_offset,
                         const Synth& synth)
    -> size_t;
};

struct PlaybackUnit
{
    midi::Player player;
    std::vector<Sample> sample_buffer = std::vector<Sample>(SAMPLE_BUFFER_SIZE);
    UAudioStream stream;
    Generator generator;
    unsigned samples_since_last_event = 0;
};

struct SoundContext
{
    PlaybackUnit live_playback, file_playback;
    std::mutex lock;
};

void Audio_LiveCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
                        int total_amount);
void Audio_FileCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
                        int total_amount);
