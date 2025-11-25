#pragma once

#include <cmath>
#include <mutex>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

#include "midi.h"

constexpr int DEFAULT_SAMPLE_RATE = 4000;
constexpr float MAX_VELOCITY = UINT8_MAX;

struct Generator
{
    Generator(int sample_rate = DEFAULT_SAMPLE_RATE);

    std::vector<float> GenerateSamples(size_t count, const midi::Player& midi_status);

    int sample_rate_ = DEFAULT_SAMPLE_RATE;
    unsigned sample_point_ = 0;
};

struct SoundContext
{
    Generator generator;
    midi::Player midi_player;
    std::mutex lock;
    SDL_AudioStream* astream = nullptr;
};

void SoundCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
    int total_amount);
