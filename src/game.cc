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
auto TypedRead(tb::type_tag_t<Token<MAX_LENGTH, Predicate>>, Stream& stream)
-> tb::result<Token<MAX_LENGTH, Predicate>, StreamError>
{
    char c;
    while (true) {
        if (fread(&c, 1, 1, stream.file_) < 1)
            return StreamError { StreamError::FILE_ERROR };

        if (!Predicate(c)) {
            if (ungetc(c, stream.file_) == EOF)
                return StreamError { StreamError::FILE_ERROR };
            break;
        }
    }

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
auto TypedRead(tb::type_tag_t<EnumName<E>>, Stream& stream)
-> tb::result<EnumName<E>, StreamError>
{
    auto token = TypedRead(
        tb::type_tag<Token<tb::longest_enum_name<E> + 1, isspace>>, stream);
    if (token.is_error())
        return token.get_error();

    std::optional<E> value = tb::string_to_enum<E>(token.get_unchecked().value.data());

    if (value == std::nullopt)
        return StreamError::OBJECT_READ_ERROR;

    return EnumName<E> { *value };
}

auto Game::LoadCadences(std::string_view major_path, std::string_view minor_path)
-> tb::error<midi::Error>
{
    auto major = midi::MIDI::FromFile(major_path);
    if (major.is_error())
        return major.get_error();

    auto minor = midi::MIDI::FromFile(minor_path);
    if (minor.is_error())
        return minor.get_error();

    major_cadence = std::move(major.get_mut_unchecked());
    minor_cadence = std::move(minor.get_mut_unchecked());

    return tb::ok;
}

auto Game::LoadExercises(std::string_view path) -> tb::error<LoadError>
{
    FILE* file = fopen(path.data(), "r");
    if (file == nullptr)
        return LoadError { LoadError::EXERCISES_NOT_FOUND };

    tb::scoped_guard close_file = [file] { fclose(file); };

    Stream stream(file);

    while (true) {
        auto result = stream.Read(tb::type_tag<
            Token<128, isspace>,
            EnumName<ExerciseType>,
            EnumName<Tonality>,
            EnumName<Difficulty>
        >);

        if (result.is_error()) {
            StreamError err = result.get_error();
            if (err != StreamError::FILE_ERROR)
                return LoadError { LoadError::FORMAT_ERROR };

            break;
        }

        auto& [path, type, tonality, difficulty] = result.get_unchecked();

        auto midi = midi::MIDI::FromFile(std::string_view { path.value.data() });

        if (midi.is_error()) {
            midi::Error err = midi.get_error();
            if (err.type == midi::Error::FILE_NOT_FOUND)
                return LoadError { LoadError::MIDI_NOT_FOUND };

            return LoadError { LoadError::MIDI_ERROR };
        }

        exercises.emplace_back(Exercise {
            .midi = std::move(midi.get_mut_unchecked()),
            .type = type.value,
            .tonality = tonality.value,
            .difficulty = difficulty.value
        });
    }

    if (exercises.empty())
        return LoadError { LoadError::EXERCISES_NOT_FOUND };

    return tb::ok;
}

void Game::InputNote(uint8_t note)
{
    if (state_ != GameState::READING_INPUT)
        return;

    note_input_buffer_.push_back(note);
}

auto Game::BeginNewExercise() -> tb::error<NoExercisesError>
{
    if (exercises.empty())
        return NoExercisesError {};

    note_input_buffer_.clear();
    state_ = GameState::PLAYING_CADENCE;

    std::random_device rand_dev;
    std::uniform_int_distribution<size_t> distribution(0, exercises.size() - 1);

    current_exercise_ = &exercises[distribution(rand_dev)];

    return tb::ok;
}

auto Game::GetCurrentExercise() const -> const Exercise*
{
    return current_exercise_;
}
