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

size_t Generator::GenerateSamples(std::span<Sample> samples, size_t count,
    const midi::Player& midi_status, unsigned sample_offset)
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

    if (count > samples.size())
        count = samples.size();

    unsigned start_point = sample_point_ - sample_offset;
    midi::Ticks current_time = midi_status.GetTicksElapsed();

    float samples_per_tick = sample_rate_ / midi_status.GetDeltaPerSecond();

    for (size_t i = 0; i < count; ++i) {
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

            samples[i] += pulse(note, sample_point_, sample_rate_)
                        * volume
                        * (info.velocity / MAX_VELOCITY)
                        * decay;
        }
    }

    return count;
}

void SoundCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
    int total_amount)
{
    if (additional_amount < 1) return;

    auto* sound_ctx = static_cast<SoundContext*>(ctx);
    Generator& generator = sound_ctx->generator;
    midi::Player& midi_player = sound_ctx->midi_player;
    auto& sample_buffer = sound_ctx->sample_buffer;
    unsigned& samples_since_last_event = sound_ctx->samples_since_last_event;

    sample_buffer.fill(0.f);

    std::lock_guard<std::mutex> guard(sound_ctx->lock);

    // Live playback
    if (midi_player.mode == midi::PlayerMode::LIVE_PLAYBACK) {
        size_t samples = generator.GenerateSamples(sample_buffer, additional_amount,
            midi_player, 0);
        SDL_PutAudioStreamData(stream, sample_buffer.data(), samples * sizeof(Sample));
        return;
    }

    float samples_per_tick = generator.sample_rate_ / midi_player.GetDeltaPerSecond();

    size_t total_samples = 0;

    // File playback
    while (total_samples < static_cast<size_t>(additional_amount)) {
        std::optional<midi::Ticks> ticks = midi_player.TicksUntilNextEvent();
        if (!ticks) break;

        samples_per_tick = generator.sample_rate_ / midi_player.GetDeltaPerSecond();
        std::span<Sample> sample_buffer_range {
            sample_buffer.begin() + total_samples,
            sample_buffer.end()
        };

        size_t requested_samples = ticks.value() * samples_per_tick
            - samples_since_last_event;
        size_t samples_generated = generator.GenerateSamples(sample_buffer_range,
            requested_samples, midi_player, samples_since_last_event);

        total_samples += samples_generated;

        if (samples_generated < requested_samples) {
            samples_since_last_event += samples_generated;
            break;
        }

        samples_since_last_event = 0;

        if (midi_player.Advance().is_error())
            break;
    }

    SDL_PutAudioStreamData(stream, sample_buffer.data(),
        total_samples * sizeof(Sample));
}
