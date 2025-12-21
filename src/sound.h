#pragma once

#include <array>
#include <cmath>
#include <mutex>
#include <span>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

#include "midi.h"

constexpr int DEFAULT_SAMPLE_RATE = 4000;
constexpr float MAX_VELOCITY = UINT8_MAX;
constexpr size_t SAMPLE_BUFFER_SIZE = 8192;

using Sample = float;

struct Generator
{
    Generator(int sample_rate = DEFAULT_SAMPLE_RATE);

    auto GenerateSamples(std::span<Sample> dest, size_t count,
                         const midi::Player& midi_status, unsigned sample_offset)
    -> size_t;

    int sample_rate_ = DEFAULT_SAMPLE_RATE;
    unsigned sample_point_ = 0;
};

struct SoundContext
{
    Generator generator;
    midi::Player midi_player;
    std::mutex lock;
    SDL_AudioStream* astream = nullptr;
    std::array<Sample, SAMPLE_BUFFER_SIZE> sample_buffer {};
    unsigned samples_since_last_event = 0;
};

void SoundCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
                   int total_amount);
