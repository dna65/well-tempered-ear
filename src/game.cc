#include "game.h"

#include <type_traits>
#include <random>

template<size_t MAX_LENGTH, auto Predicate>
struct Token
{
    std::array<char, MAX_LENGTH> value { 0 };
};

template<tb::Enum E>
struct EnumName
{
    E value {};
};

template<size_t MAX_LENGTH, auto Predicate>
auto TypedRead(tb::type_tag_t<Token<MAX_LENGTH, Predicate>>, Stream stream)
-> tb::result<Token<MAX_LENGTH, Predicate>, StreamError>
{
    char c;
    do {
        if (fread(&c, 1, 1, stream.file_) < 1)
            return StreamError { StreamError::FILE_ERROR };
    } while (Predicate(c));

    if (ungetc(c, stream.file_) == EOF)
        return StreamError { StreamError::FILE_ERROR };

    Token<MAX_LENGTH, Predicate> result {};

    for (size_t i = 0; i < MAX_LENGTH - 1; ++i) {
        if (fread(&c, 1, 1, stream.file_) < 1) {
            if (!feof(stream.file_))
                return StreamError { StreamError::FILE_ERROR };
            break;
        }

        if (Predicate(c)) {
            if (ungetc(c, stream.file_) == EOF)
                return StreamError { StreamError::FILE_ERROR };
            break;
        }

        result.value[i] = c;
    }

    return result;
}

template<tb::Enum E>
auto TypedRead(tb::type_tag_t<EnumName<E>>, Stream stream)
-> tb::result<EnumName<E>, StreamError>
{
    auto token = TypedRead(
        tb::type_tag<Token<tb::longest_enum_name<E> + 1, isspace>>,
        stream
    );
    if (token.is_error())
        return token.get_error();

    std::optional<E> value = tb::string_to_enum<E>(token.get_unchecked().value.data());

    if (value == std::nullopt)
        return StreamError::OBJECT_READ_ERROR;

    return EnumName<E> { *value };
}

auto Resources::LoadMIDI(std::string_view path)
-> tb::result<MIDIIndex, midi::Error>
{
    auto midi_or_err = midi::MIDI::FromFile(path);
    if (midi_or_err.is_error())
        return midi_or_err.get_error();

    midis.emplace_back(std::move(midi_or_err.get_mut_unchecked()));
    return static_cast<MIDIIndex>(midis.size() - 1);
}

auto Resources::LoadExercises(std::string_view path) -> tb::error<LoadExercisesError>
{
    FILE* file = fopen(path.data(), "r");
    if (file == nullptr)
        return LoadExercisesError { LoadExercisesError::EXERCISES_NOT_FOUND };

    tb::scoped_guard close_file = [file] { fclose(file); };

    Stream stream(file);

    while (!feof(file)) {
        auto result = stream.Read(tb::type_tag<
            Token<128, isspace>,
            EnumName<ExerciseType>,
            EnumName<Tonality>,
            EnumName<Difficulty>
        >);

        if (result.is_error()) {
            StreamError err = result.get_error();
            if (err != StreamError::FILE_ERROR)
                return LoadExercisesError { LoadExercisesError::FORMAT_ERROR };
            break;
        }

        auto& [path, type, tonality, difficulty] = result.get_unchecked();

        auto midi_index = LoadMIDI(std::string_view { path.value.data() });

        if (midi_index.is_error()) {
            midi::Error err = midi_index.get_error();
            if (err.type == midi::Error::FILE_NOT_FOUND)
                return LoadExercisesError { LoadExercisesError::MIDI_NOT_FOUND };

            return LoadExercisesError { LoadExercisesError::MIDI_ERROR };
        }

        exercises.emplace_back(Exercise {
            .midi = midi_index.get_unchecked(),
            .type = type.value,
            .tonality = tonality.value,
            .difficulty = difficulty.value
        });
    }

    if (exercises.empty())
        return LoadExercisesError { LoadExercisesError::EXERCISES_NOT_FOUND };

    return tb::ok;
}

Game::Game(const Resources& resources) : resources_(resources) {}

void Game::SetCadences(MIDIIndex major, MIDIIndex minor)
{
    major_cadence = major;
    minor_cadence = minor;
}

void Game::InputNote(uint8_t note)
{
    if (state_ != GameState::READING_INPUT)
        return;

    if (note_input_buffer_.size() >= exercise_notes_.size())
        return;

    const uint8_t downward_transposition_factor
        = static_cast<uint8_t>(required_input_key_);
    const uint8_t comparator = exercise_notes_[note_input_buffer_.size()]
        + downward_transposition_factor;

    auto wrong_note = [this, comparator, note] () {
        fmt::print("Wrong note: {} (should have been {})!\n", midi::NoteName(note),
            midi::NoteName(comparator));
        state_ = GameState::WAIT_FOR_READY;
    };

    if (note_input_buffer_.empty()) {
        if (midi::GetPitchClass(note) != midi::GetPitchClass(comparator)) {
            wrong_note();
            return;
        }

        octave_displacement_ = comparator - note;
        note += octave_displacement_;
        note_input_buffer_.push_back(note);
        return;
    }

    note += octave_displacement_;
    if (note != comparator) {
        wrong_note();
        return;
    }

    note_input_buffer_.push_back(note);

    if (note_input_buffer_.size() >= exercise_notes_.size()) {
        fmt::print("Correct!\n");
        state_ = GameState::WAIT_FOR_READY;
        return;
    }
}

auto Game::BeginNewExercise() -> tb::error<NoExercisesError>
{
    static std::random_device rand_dev;
    static std::uniform_int_distribution<uint8_t> input_key(0, 11);

    if (resources_.exercises.empty())
        return NoExercisesError {};

    std::uniform_int_distribution<ExerciseIndex>
        exercise_index(0, resources_.exercises.size() - 1);

    note_input_buffer_.clear();
    exercise_notes_.clear();
    state_ = GameState::PLAYING_CADENCE;

    current_exercise_ = exercise_index(rand_dev);
    const Exercise* exercise_ptr = GetCurrentExercise();
    required_input_key_ = static_cast<midi::PitchClass>(input_key(rand_dev));

    if (exercise_ptr->type == ExerciseType::SINGLE_VOICE_TRANSCRIPTION) {
        resources_.midis[exercise_ptr->midi].tracks[0].ToNoteSeries(exercise_notes_);
    }

    return tb::ok;
}

auto Game::GetCurrentExercise() const -> const Exercise*
{
    return &(resources_.exercises[current_exercise_]);
}

auto Game::GetRequiredInputKey() const -> midi::PitchClass
{
    return required_input_key_;
}

auto Game::GetCurrentCadenceMIDI() const -> const midi::MIDI*
{
    if (current_exercise_ == INVALID_RESOURCE)
        return nullptr;

    switch (GetCurrentExercise()->tonality) {
    default:
    case Tonality::MAJOR:
        return &resources_.midis[major_cadence];
    case Tonality::MINOR:
        return &resources_.midis[minor_cadence];
    }
}

void Game::MIDIEnded()
{
    switch (state_) {
    case GameState::PLAYING_CADENCE:
        state_ = GameState::PLAYING_EXERCISE;
        break;
    case GameState::PLAYING_EXERCISE:
        state_ = GameState::READING_INPUT;
        break;
    case GameState::PLAYING_RESULT:
        state_ = GameState::WAIT_FOR_READY;
        break;
    default:
        break;
    }
}

auto Game::GetState() const -> GameState
{
    return state_;
}
