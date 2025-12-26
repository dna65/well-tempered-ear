#include "midi.h"

#include <limits>

struct VariableLengthInt
{
    uint32_t value = 0;
};

struct UInt24
{
    uint32_t value = 0;
};

template<>
auto TypedRead(Stream& stream) -> tb::result<VariableLengthInt, StreamError>
{
    VariableLengthInt result = { 0 };

    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        uint8_t byte;
        if (fread(&byte, 1, 1, stream.file_) < 1)
            return StreamError::FILE_ERROR;

        result.value <<= 7;
        result.value |= static_cast<uint32_t>(byte & 0x7F);

        if ((byte & 0x80) == 0) // Bit 7 off indicates end of number
            break;
    }

    return result;
}

template<>
auto TypedRead(Stream& stream) -> tb::result<UInt24, StreamError>
{
    UInt24 result = { 0 };

    for (size_t i = 0; i < 3 * sizeof(uint8_t); ++i) {
        uint8_t byte;
        if (fread(&byte, 1, 1, stream.file_) < 1)
            return StreamError::FILE_ERROR;

        result.value <<= 8;
        result.value |= byte;
    }

    return result;
}

namespace midi
{

auto MIDI::FromFile(std::string_view path) -> tb::result<MIDI, Error>
{
    FILE* file = fopen(path.data(), "rb");
    if (!file)
        return Error { Error::FILE_NOT_FOUND };

    tb::scoped_guard close_file = [file] { fclose(file); };
    return FromStream(file);
}

auto Track::FromStream(FILE* file) -> tb::result<Track, Error>
{
    Stream stream(file);

    EventType running_type;
    Track track;

    while (true) {
        auto event_info = stream.Read(tb::type_tag<VariableLengthInt, EventType>);
        if (event_info.is_error())
            return Error {
                Error::MISSING_EVENT,
                stream.Position().get_unchecked()
            };

        auto [delta, type] = event_info.get_unchecked();
        auto bad_event_error = [&stream] {
            return Error {
                Error::BAD_EVENT,
                stream.Position().get_unchecked()
            };
        };

        if (type == EventType::META) {
            auto meta_event_info
                = stream.Read(tb::type_tag<MetaType, VariableLengthInt>);
            if (meta_event_info.is_error())
                return bad_event_error();

            auto [meta_type, length] = meta_event_info.get_unchecked();

            if (meta_type == MetaType::END_TRACK) {
                track.events.push_back({
                    .delta_time = delta.value,
                    .type = EventType::META,
                    .meta_type = MetaType::END_TRACK
                });
                break;
            }

            switch (meta_type) {
            case MetaType::TEMPO: {
                auto usec_per_quarter = stream.Read<UInt24>();
                if (usec_per_quarter.is_error())
                    return bad_event_error();

                track.events.push_back(Event {
                    .delta_time = delta.value,
                    .type = EventType::META,
                    .meta_type = MetaType::TEMPO,
                    .usec_per_quarter_note = usec_per_quarter.get_unchecked().value
                });
                break;
            }
            default:
                stream.Skip(length.value).ignore_error();
                break;
            }
        } else {
            auto type_byte = static_cast<uint8_t>(type) & 0xF0;
            if (type_byte < 0x80) {
                type_byte = static_cast<uint8_t>(running_type) & 0xF0;
                stream.Skip(-1).ignore_error();
            } else {
                running_type = type;
            }

            auto e_type = static_cast<EventType>(type_byte);

            switch (e_type) {
            case EventType::NOTE_OFF:
            case EventType::NOTE_ON: {
                auto [note, vel]
                    = stream.Read(tb::type_tag<uint8_t, uint8_t>).get_unchecked();
                track.events.push_back({
                    .delta_time = delta.value,
                    .type = vel == 0 ? EventType::NOTE_OFF : e_type,
                    .note_event = {
                        .note = note,
                        .velocity = vel
                    }
                });
                break;
            }
            case EventType::POLYPHONIC_PRESSURE:
            case EventType::PITCH_BEND:
            case EventType::CONTROLLER:
                stream.Skip(2).ignore_error();
                break;
            case EventType::PROGRAM_CHANGE:
            case EventType::CHANNEL_PRESSURE:
                stream.Skip(1).ignore_error();
                break;
            case EventType::SYSEX:
                break;
            default:
                return bad_event_error();
            }
        }
    }

    return track;
}

auto MIDI::FromStream(FILE* file) -> tb::result<MIDI, Error>
{
    Stream stream(file);

    if (char chunk_type[4]; stream.ReadToArray(chunk_type, 4).is_error()
        || memcmp(chunk_type, "MThd", 4) != 0)
        return Error { Error::NO_HEADER_FOUND, 0 };

    constexpr auto to_native_endian = [] (std::integral auto x) {
        if constexpr (std::endian::native == std::endian::little)
            return tb::reverse_endian(x);
        else
            return x;
    };

    auto header = stream.Read(
        tb::type_tag<uint32_t, uint16_t, uint16_t, uint16_t>,
        to_native_endian
    );

    if (header.is_error())
        return Error {
            Error::INCOMPLETE_HEADER,
            stream.Position().get_unchecked()
        };

    auto [chunk_length, format, track_count, tick_div] = header.get_unchecked();

    if (format > 2)
        return Error {
            Error::INVALID_FORMAT,
            stream.Position().get_unchecked()
        };

    MIDI midi;
    midi.format_ = static_cast<Format>(format);
    midi.tracks_.reserve(track_count);
    // TODO: Handle tickdiv type = 1
    midi.tick_division_ = tick_div & 0x7FFF;

    for (size_t i = 0; i < track_count; ++i) {
        if (char chunk_type[4]; stream.ReadToArray(chunk_type, 4).is_error()
            || memcmp(chunk_type, "MTrk", 4) != 0)
            return Error {
                Error::MISSING_TRACK,
                stream.Position().get_unchecked()
            };

        // Chunk length, not needed
        if (stream.Read<uint32_t>().is_error())
            return Error {
                Error::MISSING_TRACK,
                stream.Position().get_unchecked()
            };

        auto track = Track::FromStream(file);
        if (track.is_error()) {
            return track.get_error();
        }

        midi.tracks_.emplace_back(track.get_mut_unchecked());
    }

    return midi;
}

Player::Player(PlayerMode mode) : mode_(mode) {}

auto Player::Advance() -> tb::error<EndOfMIDIError>
{
    std::optional<Ticks> ticks = TicksUntilNextEvent();
    if (!ticks) return EndOfMIDIError {};

    ticks_elapsed_ += ticks.value();

    for (auto& [track, info] : tracks_) {
        if (info.done) continue;

        const Event& next_ev = track->events[info.current_event_index + 1];
        if (next_ev.delta_time > ticks.value() + info.playback_ticks) {
            info.playback_ticks += ticks.value();
            continue;
        } else {
            info.playback_ticks = 0;
        }

        switch (next_ev.type) {
        case EventType::NOTE_ON:
            notes_.emplace(next_ev.note_event.note, NoteInfo {
                .time = ticks_elapsed_,
                .velocity = next_ev.note_event.velocity
            });
            break;
        case EventType::NOTE_OFF:
            notes_.erase(next_ev.note_event.note);
            break;
        case EventType::META:
            switch (next_ev.meta_type) {
            case MetaType::END_TRACK:
                info.done = true;
                break;
            case MetaType::TEMPO:
                delta_per_second_ = 1.f /
                    (next_ev.usec_per_quarter_note
                    / midi_ptr_->tick_division_
                    / 1000000.f);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        ++info.current_event_index;
    }

    return tb::ok;
}

void Player::PlayEvent(const Event& event)
{
    Seconds time_diff = Clock::now() - start_time_;
    ticks_elapsed_ = delta_per_second_ * time_diff.count();
    switch (event.type) {
    case EventType::NOTE_ON:
        notes_.emplace(event.note_event.note, NoteInfo {
            .time = ticks_elapsed_,
            .velocity = event.note_event.velocity
        });
        break;
    case EventType::NOTE_OFF:
        notes_.erase(event.note_event.note);
        break;
    default:
        break;
    }
}

auto Player::TicksUntilNextEvent() const -> std::optional<Ticks>
{
    if (!midi_ptr_) return std::nullopt;

    Ticks shortest = std::numeric_limits<Ticks>::max();
    for (auto& [track, info] : tracks_) {
        if (info.done) continue;
        const Event& next_ev = track->events[info.current_event_index + 1];
        if (next_ev.delta_time - info.playback_ticks < shortest)
            shortest = next_ev.delta_time - info.playback_ticks;
    }

    if (shortest == std::numeric_limits<Ticks>::max())
        return std::nullopt;

    return shortest;
}

auto Player::GetCurrentNotes() const -> const NoteMap&
{
    return notes_;
}

void Player::SetMIDI(const MIDI& midi)
{
    midi_ptr_ = &midi;
    tracks_.clear();
    for (const Track& track : midi.tracks_) {
        tracks_.emplace_back(&track, PlaybackInfo {});
    }
}

auto Player::GetTicksElapsed() const -> Ticks
{
    if (mode_ == PlayerMode::LIVE_PLAYBACK) {
        Seconds time_diff = Clock::now() - start_time_;
        return delta_per_second_ * time_diff.count();
    }
    return ticks_elapsed_;
}

auto Player::GetDeltaPerSecond() const -> float
{
    return delta_per_second_;
}

}
