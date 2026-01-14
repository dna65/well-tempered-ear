#include "sound.h"

#include "events.h"

#include <algorithm>

constexpr float C_MINUS_2_A440 = 8.175f;
constexpr float COMMON_PITCH_RATIO = 1.0595f;

constexpr auto NOTE_TO_FREQUENCY_TABLE = [] () -> std::array<float, 128> {
    std::array<float, 128> result {};
    std::ranges::generate(result,
        [n = C_MINUS_2_A440] () mutable -> float {
            float x = n;
            n *= COMMON_PITCH_RATIO;
            return x;
        }
    );
    return result;
}();

auto Generator::GenerateSamples(std::span<Sample> samples, size_t count,
    const midi::Player& midi_status, unsigned sample_offset, const Synth& synth)
    -> size_t
{
    if (count > samples.size())
        count = samples.size();

    midi::Ticks current_time = midi_status.GetTicksElapsed();

    float volume = 0.3f;
    float decay_constant = synth.decay_constant;
    float decay_common_ratio = powf(2, (-1.f / sample_rate) * decay_constant);

    unsigned start_point = sample_point;

    for (uint8_t note = 0; note <= midi::NOTE_MAX; ++note) {
        const midi::NoteInfo& info = midi_status.GetCurrentNotes()[note];
        if (!info.note_on) continue;

        sample_point = start_point;
        float initial_ticks_diff
            = current_time - info.time
            + (sample_offset * midi_status.GetTicksPerSecond() / sample_rate);
        float decay = std::clamp(
            powf(2, initial_ticks_diff * decay_constant
                    / (-1.f * midi_status.GetTicksPerSecond())),
            -1.f, 1.f
        );

        uint8_t transposed_note
            = std::clamp<uint8_t>(note + midi_status.transposition_offset_, 0, 127);
        float freq = NOTE_TO_FREQUENCY_TABLE[transposed_note];

        for (size_t i = 0; i < count; ++i) {
            ++sample_point;
            decay *= decay_common_ratio;

            samples[i] += synth.wave_fn(freq, sample_point, sample_rate)
                        * volume
                        * (info.velocity / MAX_VELOCITY)
                        * decay;
        }
    }

    return count;
}

void Audio_LiveCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
    int total_amount)
{
    if (additional_amount < 1) return;

    auto* sound_ctx = static_cast<SoundContext*>(ctx);
    PlaybackUnit& playback_unit = sound_ctx->live_playback;

    Generator& generator = playback_unit.generator;
    midi::Player& live_player = playback_unit.player;
    auto& sample_buffer = playback_unit.sample_buffer;

    std::ranges::fill(sample_buffer, Sample {});

    std::lock_guard<std::mutex> guard(sound_ctx->lock);
    size_t samples = generator.GenerateSamples(sample_buffer,
        additional_amount, live_player, 0, DEFAULT_SYNTH);
    SDL_PutAudioStreamData(stream, sample_buffer.data(), samples * sizeof(Sample));
}

void Audio_FileCallback(void* ctx, SDL_AudioStream* stream, int additional_amount,
    int total_amount)
{
    if (additional_amount < 1) return;

    auto* sound_ctx = static_cast<SoundContext*>(ctx);
    PlaybackUnit& playback_unit = sound_ctx->file_playback;

    Generator& generator = playback_unit.generator;
    midi::Player& file_player = playback_unit.player;
    auto& sample_buffer = playback_unit.sample_buffer;
    unsigned& samples_since_last_event = playback_unit.samples_since_last_event;

    std::ranges::fill(sample_buffer, Sample {});

    float samples_per_tick = generator.sample_rate / file_player.GetTicksPerSecond();
    size_t samples = 0;

    while (samples < static_cast<size_t>(additional_amount)) {
        std::optional<midi::Ticks> ticks = file_player.TicksUntilNextEvent();
        if (!ticks) break;

        samples_per_tick = generator.sample_rate / file_player.GetTicksPerSecond();
        std::span<Sample> sample_buffer_range {
            sample_buffer.begin() + samples,
            sample_buffer.end()
        };

        size_t requested_samples = ticks.value() * samples_per_tick
            - samples_since_last_event;
        size_t samples_generated = generator.GenerateSamples(sample_buffer_range,
            requested_samples, file_player, samples_since_last_event, DEFAULT_SYNTH);

        samples += samples_generated;

        if (samples_generated < requested_samples) {
            samples_since_last_event += samples_generated;
            break;
        }

        samples_since_last_event = 0;

        if (file_player.Advance().is_error())
            break;

        if (file_player.Done()) {
            MIDIPlayerEndEvent ev {};
            SDL_PushEvent(reinterpret_cast<SDL_Event*>(&ev));
        }
    }

    SDL_PutAudioStreamData(stream, sample_buffer.data(), samples * sizeof(Sample));
}
