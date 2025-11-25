#pragma once

#include <cstdio>

#include "tb.h"

enum class StreamError
{
    FILE_ERROR, ZERO_COUNT, OBJECT_READ_ERROR
};

struct Stream;

template<typename T>
auto TypedRead(Stream& stream) -> tb::result<T, StreamError>;

struct Stream
{
    FILE* file_ = nullptr;
    constexpr static auto identity_transform = [] (auto&) {};
    using IdentityTransform = decltype(identity_transform);

    Stream(FILE* file) : file_(file) {}

    template<typename T, typename Transform = IdentityTransform>
    auto Read(const Transform& fn = identity_transform) -> tb::result<T, StreamError>
    {
        tb::result<T, StreamError> result = TypedRead<T>(*this);
        if (result.is_error()) return result;

        if constexpr (requires {
            { fn(result.get_unchecked()) } -> std::same_as<T>;
        }) {
            return fn(result.get_unchecked());
        } else {
            fn(result.get_mut_unchecked());
            return result.get_mut_unchecked();
        }
    }

    template<typename T>
    auto ReadToArray(T* dest, size_t count = 1) -> tb::error<StreamError>
    {
        if (count < 1) return StreamError::ZERO_COUNT;

        if (fread(dest, sizeof(T), count, file_) < count)
            return StreamError::FILE_ERROR;

        return tb::ok;
    }

    template<typename Transform = IdentityTransform, typename... Ts>
        requires(sizeof...(Ts) != 0)
    auto Read(tb::type_tag_t<Ts...>, const Transform& fn = identity_transform)
    -> tb::result<std::tuple<Ts...>, StreamError>
    {
        std::tuple<Ts...> result;
        bool okay = true;
        StreamError error;
        for (size_t i = 0; i < sizeof...(Ts); ++i) {
            tb::visit_tuple(result, i, [&] (auto& elem) {
                auto result = TypedRead<std::remove_reference_t<decltype(elem)>>(*this);
                if (result.is_error()) {
                    error = result.get_error();
                    okay = false;
                    return;
                }
                elem = result.get_unchecked();
                if constexpr (requires { elem = fn(elem); })
                    elem = fn(elem);
                else
                    fn(elem);
            });

            if (!okay) return error;
        }

        return result;
    }

    auto Skip(size_t bytes) -> tb::error<StreamError>;

    auto Position() -> tb::result<size_t, StreamError>;
};

template<typename T> requires std::integral<T> || std::is_enum_v<T>
auto TypedRead(Stream& stream) -> tb::result<T, StreamError>
{
    T result;
    if (fread(&result, sizeof(T), 1, stream.file_) < 1)
        return StreamError::FILE_ERROR;

    return result;
}
