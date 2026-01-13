#pragma once

#include <vector>

#include "midi.h"
#include "tb.h"

using namespace std::literals;

enum class Difficulty { EASY, MEDIUM, HARD, EXPERT };

template<>
inline constexpr auto tb::enum_names<Difficulty> = std::to_array({
    "easy"sv, "medium"sv, "hard"sv, "expert"sv
});

enum class ExerciseType
{
    SINGLE_VOICE_TRANSCRIPTION
};

template<>
inline constexpr auto tb::enum_names<ExerciseType> = std::to_array({
    "single_voice_transcription"sv
});

enum class GameState
{
    WAIT_FOR_READY,
    PLAYING_CADENCE,
    PLAYING_EXERCISE,
    READING_INPUT,
    PLAYING_RESULT
};

struct LoadError
{
    enum Type
    {
        EXERCISES_NOT_FOUND, FORMAT_ERROR, MIDI_NOT_FOUND, MIDI_ERROR
    } type;

    constexpr auto What() const -> std::string_view
    {
        switch (type) {
        case EXERCISES_NOT_FOUND:
            return "exercise file not found or empty";
        case FORMAT_ERROR:
            return "incorrect format for exercise file";
        case MIDI_NOT_FOUND:
            return "midi file not found";
        case MIDI_ERROR:
            return "error loading midi";
        }
    }
};

enum class Tonality { MAJOR, MINOR };

template<>
inline constexpr auto tb::enum_names<Tonality> = std::to_array({
    "major"sv, "minor"sv
});

struct Exercise
{
    midi::MIDI midi;
    ExerciseType type;
    Tonality tonality;
    Difficulty difficulty;
};

struct NoExercisesError {};

class Game
{
public:
    Game() = default;

    auto LoadCadences(std::string_view major_path, std::string_view minor_path)
    -> tb::error<midi::Error>;
    auto LoadExercises(std::string_view path) -> tb::error<LoadError>;
    void InputNote(uint8_t note);
    auto BeginNewExercise() -> tb::error<NoExercisesError>;
    auto GetCurrentExercise() const -> const Exercise*;
    auto GetRequiredInputKey() const -> midi::PitchClass;
    auto GetCurrentCadenceMIDI() const -> const midi::MIDI*;
    void MIDIEnded();
    auto GetState() const -> GameState;

private:
    midi::MIDI major_cadence, minor_cadence;
    std::vector<Exercise> exercises;
    std::vector<uint8_t> note_input_buffer_ = tb::vec_with_capacity<uint8_t>(32);
    std::vector<uint8_t> exercise_notes_ = tb::vec_with_capacity<uint8_t>(32);
    const Exercise* current_exercise_ = nullptr;
    GameState state_ = GameState::WAIT_FOR_READY;
    midi::PitchClass required_input_key_ = midi::PitchClass::C;
    int8_t octave_displacement_ = 0;
};
