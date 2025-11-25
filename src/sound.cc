#include "sound.h"

constexpr auto midi_to_freq = [] (uint8_t note) -> float {
    return 440.f * powf(2, (note - 69) / 12.f);
};

constexpr auto clampf = [] (float min, float max, float val) -> float {
    if (val < min) return min;
    if (val > max) return max;
    return val;
};

Generator::Generator(int sample_rate) : sample_rate_(sample_rate) {}

std::vector<float> Generator::GenerateSamples(size_t count,
    const midi::Player& midi_status)
{
    constexpr auto pulse = [] (uint8_t note, unsigned point, unsigned rate) {
        float x = point * midi_to_freq(note) / rate;
        float x_i = floor(x);

        float x2 = x - (rate * 0.5) / rate;
        float x2_i = floor(x2);

        return (x_i - x) - (x2_i - x2);
    };

    [[maybe_unused]]
    constexpr auto sine = [] (uint8_t note, unsigned point, unsigned rate) {
        return sinf(point * 2.f * M_PI * midi_to_freq(note) / rate);
    };

    std::vector<float> samples(count, 0.f);

    unsigned start_point = sample_point_;
    midi::Ticks current_time = midi_status.GetTicksElapsed();

    float samples_per_tick = sample_rate_ / midi_status.GetDeltaPerSecond();

    for (float& sample : samples) {
        ++sample_point_;
        unsigned sample_point_diff
            = sample_point_ < start_point ? (UINT32_MAX - start_point + sample_point_)
                                          : (sample_point_ - start_point);

        midi::Ticks extra_time = sample_point_diff / samples_per_tick;

        for (auto& [note, info] : midi_status.GetCurrentNotes()) {
            float volume = 0.2f;
            float half_life = 0.3f;
            float time_diff = (current_time + extra_time - info.time)
                / midi_status.GetDeltaPerSecond();

            float decay = clampf(-1.f, 1.f, powf(2, time_diff / half_life * -1));

            sample += pulse(note, sample_point_, sample_rate_)
                    * volume
                    * (info.velocity / MAX_VELOCITY)
                    * decay;
        }
    }
    return samples;
}

void SoundCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
    int total_amount)
{
    if (additional_amount < 1) return;

    auto* sound_ctx = static_cast<SoundContext*>(ctx);
    Generator& generator = sound_ctx->generator;
    midi::Player& midi_player = sound_ctx->midi_player;

    std::lock_guard<std::mutex> guard(sound_ctx->lock);

    // Live playback
    if (midi_player.mode == midi::PlayerMode::LIVE_PLAYBACK) {
        std::vector<float> samples
            = generator.GenerateSamples(additional_amount, midi_player);
        SDL_PutAudioStreamData(stream, samples.data(),
                samples.size() * sizeof(float));
        return;
    }

    midi::Ticks total_ticks_advanced = 0;
    float samples_per_tick = generator.sample_rate_ / midi_player.GetDeltaPerSecond();

    // File playback
    while (total_ticks_advanced * samples_per_tick < additional_amount) {
        // TODO: Can potentially put indefinitely many samples, address
        std::optional<midi::Ticks> ticks = midi_player.TicksUntilNextEvent();
        if (!ticks) return;

        samples_per_tick = generator.sample_rate_ / midi_player.GetDeltaPerSecond();

        if (ticks > 0) {
            total_ticks_advanced += ticks.value();

            std::vector<float> samples
                = generator.GenerateSamples(ticks.value() * samples_per_tick,
                midi_player);
            SDL_PutAudioStreamData(stream, samples.data(),
                samples.size() * sizeof(float));
        }

        if (midi_player.Advance().is_error())
            return;
    }
}
