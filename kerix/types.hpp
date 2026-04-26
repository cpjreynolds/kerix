#ifndef KERIX_TRAITS_HPP
#define KERIX_TRAITS_HPP

#include <type_traits>
#include <utility>

namespace kerix {

// a wrapper for passing constant template parameters as arguments.
template<auto V>
struct nontype_t {
    explicit nontype_t() = default;
};

template<auto V>
constexpr nontype_t<V> nontype{};

// checks whether T is a function pointer.
template<typename T>
struct is_function_ptr
    : std::conjunction<std::is_pointer<T>,
                       std::is_function<std::remove_pointer_t<T>>> {};

template<typename T>
inline constexpr bool is_function_ptr_v = is_function_ptr<T>::value;

// checks whether T is a pointer to anything other than function or member.
template<typename T>
struct is_data_ptr
    : std::conjunction<std::is_pointer<T>,
                       std::negation<std::disjunction<std::is_member_pointer<T>,
                                                      is_function_ptr<T>>>> {};

template<typename T>
inline constexpr bool is_data_ptr_v = is_data_ptr<T>::value;

// checks whether T is a pointer, including member pointers.
template<typename T>
struct is_any_ptr
    : std::disjunction<std::is_pointer<T>, std::is_member_pointer<T>> {};

template<typename T>
inline constexpr bool is_any_ptr_v = is_any_ptr<T>::value;

class undefined_class;

using erased_fn = void (*)();
using erased_mem_fn = void (undefined_class::*)();

template<typename>
struct erased_type;

template<typename T>
    requires is_data_ptr_v<T>
struct erased_type<T> {
    using type = void*;
};

template<typename T>
    requires std::is_member_function_pointer_v<T>
struct erased_type<T> {
    using type = void (undefined_class::*)();
};

template<typename T>
    requires is_function_ptr_v<T>
struct erased_type<T> {
    using type = void (*)();
};

template<typename T>
using erased_type_t = erased_type<T>::type;

template<typename T>
    requires is_any_ptr_v<T>
constexpr auto erase_type(T v) noexcept
{
    if constexpr (is_function_ptr_v<T>) {
        return reinterpret_cast<void (*)()>(v);
    }
    else if constexpr (std::is_member_function_pointer_v<T>) {
        return reinterpret_cast<void (undefined_class::*)()>(v);
    }
    else {
        return const_cast<void*>(static_cast<const void*>(v));
    }
}

template<typename>
struct thunk_traits;

template<typename M, typename T>
struct thunk_traits<M T::*> : thunk_traits<M> {
    using class_type = T;
    using qualified_ref = thunk_traits<M>::template qualify<T>;
    using qualified_type = std::remove_reference_t<qualified_ref>;
    using qualified_ptr = std::add_pointer_t<qualified_ref>;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...)> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) &> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) &&> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) & noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const&> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const & noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) && noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T>&&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const&&> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&&;
};

template<typename R, typename... Args>
struct thunk_traits<R(Args...) const && noexcept> {
    using signature = R(Args...);

    template<typename T>
    using qualify = std::remove_cvref_t<T> const&&;
};

template<typename F>
    requires is_function_ptr_v<F>
struct thunk_traits<F> : thunk_traits<std::remove_pointer_t<F>> {};

template<typename T>
using thunk_signature_t = thunk_traits<T>::signature;

template<typename T>
using thunk_qualified_t = thunk_traits<T>::qualified_type;

template<typename T>
using thunk_qualified_ref_t = thunk_traits<T>::qualified_ref;

template<typename T>
using thunk_qualified_ptr_t = thunk_traits<T>::qualified_ptr;

template<typename T>
using thunk_class_t = thunk_traits<T>::class_type;

} // namespace kerix

#endif
