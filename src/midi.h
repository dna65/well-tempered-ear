#pragma once

#include <algorithm>
#include <chrono>
#include <iterator>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "stream.h"

#include <tb/tb.h>

namespace midi
{

constexpr size_t MESSAGE_SIZE = 4;
constexpr uint8_t MAX_NOTE = std::numeric_limits<int8_t>::max();
constexpr float MAX_VELOCITY = std::numeric_limits<int8_t>::max();

using Ticks = uint64_t;

enum class Format
{
    SINGLE_TRACK = 0, MULTI_TRACK = 1, MULTI_TRACK_INDEPENDENT = 2
};

enum class Timing
{
    METRICAL = 0, ABSOLUTE = 1
};

enum class EventType : uint8_t
{
    // MIDI events, high nibble = type, low nibble = channel number
    NOTE_OFF = 0x80, NOTE_ON = 0x90, POLYPHONIC_PRESSURE = 0xA0,
    CONTROLLER = 0xB0, PROGRAM_CHANGE = 0xC0, CHANNEL_PRESSURE = 0xD0,
    PITCH_BEND = 0xE0,

    SYSEX_OR_META = 0xF0,
    SYSEX = 0xF0,
    META = 0xFF
};

enum class MetaType : uint8_t
{
    SEQUENCE_OR_TRACK_NAME = 0x03, END_TRACK = 0x2F, TEMPO = 0x51
};

enum class CodeIndexNumber : uint8_t
{
    NOTE_OFF = 0x08, NOTE_ON = 0x09, POLY_KEYPRESS = 0x0A,
    SINGLE_BYTE = 0x0F
};

enum class PlayerMode
{
    FILE_PLAYBACK, LIVE_PLAYBACK
};

enum class PitchClass
{
    C, C_SHARP, D, E_FLAT, E, F, F_SHARP, G, A_FLAT, A, B_FLAT, B
};

struct EndOfMIDIError {};

struct Error
{
    enum ErrorType
    {
        FILE_NOT_FOUND, NO_HEADER_FOUND, INCOMPLETE_HEADER, INVALID_FORMAT,
        MISSING_TRACK, MISSING_EVENT, BAD_EVENT
    };

    size_t byte_position;
    ErrorType type;

    constexpr Error(ErrorType type, size_t byte_pos = 0)
    : byte_position(byte_pos), type(type) {}

    constexpr auto What() const -> std::string_view
    {
        switch (type) {
        case FILE_NOT_FOUND: return "file not found";
        case NO_HEADER_FOUND: return "no header found";
        case INCOMPLETE_HEADER: return "incomplete header";
        case INVALID_FORMAT: return "invalid format";
        case MISSING_TRACK: return "missing track";
        case MISSING_EVENT: return "missing event";
        case BAD_EVENT: return "bad event";
        default: return "unknown error";
        }
    }
};

struct Event
{
    uint32_t delta_time;
    EventType type;
    MetaType meta_type;
    union {
        struct {
            uint8_t note;
            uint8_t velocity;
            uint8_t channel;
        } note_event;

        uint32_t usec_per_quarter_note; // Tempo
    };
};

struct Track
{
    std::vector<Event> events;

    static auto FromStream(FILE* file, uint32_t track_size) -> tb::result<Track, Error>;
    void ToNoteSeries(std::vector<uint8_t>& output) const;
};

constexpr size_t BEFORE_FIRST_EVENT = std::numeric_limits<size_t>::max();

struct TrackInfo
{
    size_t current_event_index = BEFORE_FIRST_EVENT;
    Ticks playback_ticks = 0;
    bool done = false;
};

struct NoteInfo
{
    Ticks time;
    uint8_t velocity;
    bool note_on = false;
};

struct MIDI
{
    std::vector<Track> tracks;
    Format format;
    uint16_t ticks_per_quarter_note;

    static auto FromFile(std::string_view path) -> tb::result<MIDI, Error>;
    static auto FromStream(FILE* file) -> tb::result<MIDI, Error>;
};

class Player
{
public:
    using NoteMap = std::array<NoteInfo, MAX_NOTE + 1>;
    Player(PlayerMode mode = PlayerMode::FILE_PLAYBACK);

    auto Advance() -> tb::error<EndOfMIDIError>;
    void PlayEvent(const Event& event);
    auto TicksUntilNextEvent() const -> std::optional<Ticks>;
    auto GetCurrentNotes() const -> const NoteMap&;
    auto GetTicksElapsed() const -> Ticks;
    auto GetTicksPerSecond() const -> float;
    void SetMIDI(const MIDI& midi);
    auto Done() const -> bool;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Seconds = std::chrono::duration<double>;

    NoteMap notes_ = {};
    std::vector<std::pair<const Track*, TrackInfo>> tracks_;
    const MIDI* midi_ptr_ = nullptr;
    TimePoint start_time_ = Clock::now();
    Ticks ticks_elapsed_ = 0;
    float ticks_per_second_ = 960.f;
    PlayerMode mode_;
public:
    uint8_t transposition_offset_ = 0;
};

constexpr std::array<std::string_view, 12> NOTE_NAMES {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

constexpr auto NoteName(uint8_t note) -> std::string_view
{
    return NOTE_NAMES[note % 12];
}

constexpr auto NoteName(PitchClass note) -> std::string_view
{
    return NOTE_NAMES[static_cast<uint8_t>(note)];
}

constexpr auto GetPitchClass(uint8_t note) -> PitchClass
{
    return static_cast<PitchClass>(note % 12);
}

}
