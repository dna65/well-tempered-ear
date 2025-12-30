#pragma once

#include <cstddef>
#include <type_traits>
#include <span>
#include <ranges>
#include <fmt/core.h>

namespace tb
{

namespace detail
{

struct ok_t {};

template<typename R, typename E>
struct result_internal
{
    union {
        R value;
        E err;
    };
    const bool is_err;

    result_internal(const R& value) : value(value), is_err(false) {}
    result_internal(R&& value) : value(std::move(value)), is_err(false) {}
    result_internal(const E& err) : err(err), is_err(true) {}
    ~result_internal() { if (!is_err) value.~R(); }
};

template<typename E>
struct result_internal<void, E>
{
    union { E err; };
    const bool is_err;

    result_internal(ok_t) : is_err(false) {}
    result_internal(const E& err) : err(err), is_err(true) {}
};

template<typename R, typename E> requires std::is_empty_v<E>
struct result_internal<R, E>
{
    union { R value; };
    const bool is_err;

    result_internal(const R& value) : value(value), is_err(false) {}
    result_internal(R&& value) : value(std::move(value)), is_err(false) {}
    result_internal(const E&) : is_err(true) {}
    ~result_internal() { if (!is_err) value.~R(); }
};

template<typename E> requires std::is_empty_v<E>
struct result_internal<void, E>
{
    const bool is_err;

    result_internal(ok_t) : is_err(false) {}
    result_internal(const E&) : is_err(true) {}
};

struct bad_tuple_access {};

template<size_t I, typename Tuple, typename Callable>
constexpr void visit_tuple_impl(Tuple& tuple, size_t index, Callable&& fn)
{
    if constexpr (I == 0) {
        throw bad_tuple_access {};
    } else {
        if (index == I - 1)
            fn(std::get<I - 1>(tuple));
        else
            visit_tuple_impl<I - 1>(tuple, index, fn);
    }
}

} // namespace detail

template<typename T>
struct range_to_t {};

template<typename T, size_t N>
constexpr auto make_span(T (&&array)[N]) { return std::span<T>(array, N); }

template<typename T>
constexpr auto range_to() { return range_to_t<T> {}; }

template<std::ranges::view View, typename T>
constexpr auto operator|(View&& view, range_to_t<T>&&)
{
    return T { view.begin(), view.end() };
}

template<typename Tuple, typename Callable>
constexpr void visit_tuple(Tuple& tuple, size_t index, Callable&& fn)
{
    detail::visit_tuple_impl<std::tuple_size_v<Tuple>>(tuple, index, fn);
}

template<typename Lambda> requires std::invocable<Lambda>
struct scoped_guard
{
    Lambda lambda;
    scoped_guard(Lambda&& lambda) : lambda(std::move(lambda)) {}
    ~scoped_guard() { lambda(); }
};

template<typename Range, typename Type>
concept IntegerWidthRange = std::integral<std::ranges::range_value_t<Range>>
    && sizeof(std::ranges::range_value_t<Range>) == sizeof(Type);

constexpr detail::ok_t ok;

template<typename R, typename E>
struct [[nodiscard]] result
{
private:
    detail::result_internal<R, E> members;
public:
    result() = delete;

    // Success/value initialisation
    template<typename T = R> requires (std::is_void_v<T> && std::is_same_v<T, R>)
    result(detail::ok_t) : members { ok } {}

    template<typename T = R> requires (!std::is_void_v<T> && std::is_same_v<T, R>)
    result(const T& value) : members { value } {}

    template<typename T = R> requires (!std::is_void_v<T> && std::is_same_v<T, R>)
    result(T&& value) : members { std::move(value) } {}

    // Error initialisation
    template<typename T = E> requires (!std::is_empty_v<T> && std::is_same_v<T, E>)
    result(const T& err) : members { err } {}

    template<typename T = E> requires (std::is_empty_v<T> && std::is_same_v<T, E>)
    result(const T& err) : members { err } {}

    bool is_error() const { return members.is_err; }
    bool is_ok() const { return !members.is_err; }

    template<typename Callable>
    const result& if_ok(Callable&& cb) const
    {
        if constexpr (std::is_void_v<R>) {
            if (!members.is_err) cb();
        } else {
            if (!members.is_err) cb(members.value);
        }
        return *this;
    }

    template<typename Callable>
    result& if_ok_mut(Callable&& cb)
    {
        if constexpr (std::is_void_v<R>) {
            if (!members.is_err) cb();
        } else {
            if (!members.is_err) cb(members.value);
        }
        return *this;
    }

    template<typename Callable>
    const result& if_err(Callable&& cb) const
    {
        if constexpr (std::is_empty_v<E>) {
            if (members.is_err) cb(E {});
        } else {
            if (members.is_err) cb(members.err);
        }
        return *this;
    }

    template<typename T = R> requires (!std::is_void_v<T>)
    const T& get_or(T&& alternative) const
    {
        return members.is_err ? alternative : members.value;
    }

    template<typename T = R> requires (!std::is_same_v<T, void>)
    const T& get_unchecked() const { return members.value; }

    template<typename T = R> requires (!std::is_same_v<T, void>)
    T& get_mut_unchecked() { return members.value; }

    auto get_error() const
    {
        if constexpr (std::is_empty_v<E>) return E {};
        else return members.err;
    }

    void ignore_error() const {}
};

template<typename E>
using error = result<void, E>;

template<typename Integer> requires std::integral<Integer>
constexpr Integer reverse_endian(Integer x)
{
    Integer result = 0;
    for (size_t i = 0; i < sizeof(Integer); ++i) {
        result <<= 8;
        result += (x & 0xFF);
        x >>= 8;
    }

    return result;
}

template<typename... Ts>
struct type_tag_t {};

template<typename... Ts>
constexpr type_tag_t<Ts...> type_tag {};

template<auto Deleter>
struct deleter
{
    constexpr void operator()(auto* ptr) { Deleter(ptr); }
};

template<typename E>
concept Enum = std::is_enum_v<E>;

template<Enum E>
constexpr auto enum_names = std::to_array<std::string_view>({ "" });

template<Enum E>
constexpr auto longest_enum_name = [] () -> size_t {
    size_t longest {};
    for (auto name : enum_names<E>)
        if (name.size() > longest) longest = name.size();

    return longest;
}();

template<Enum E>
constexpr auto string_to_enum = [] (std::string_view str) -> std::optional<E> {
    auto it = std::ranges::find(enum_names<E>, str);
    if (it == enum_names<E>.end())
        return std::nullopt;

    return static_cast<E>(it - enum_names<E>.begin());
};

} // namespace tb
