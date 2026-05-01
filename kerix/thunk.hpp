// kerix
// Copyright (C) 2025-2026  Cole Reynolds
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef KERIX_THUNK_HPP
#define KERIX_THUNK_HPP

#ifdef _MSC_VER
#pragma pointers_to_members(full_generality)
#endif

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/compat/function_ref.hpp>
#include <boost/compat/move_only_function.hpp>
#include <kerix/types.hpp>

#include "doctest.h"

namespace kerix {

static_assert(
    sizeof(void*) == sizeof(void (*)()),
    "data and function pointer size mismatch. you have a wild platform.");

class bad_thunk : public std::bad_function_call {};

// SBO-enabling storage for a callable.
//
// The struct layout is designed to allow memcmp comparisons between
// storage instances containing either function pointers or object & member
//
// [     8 bytes    ][            16 bytes              ]
// [  bound object  ][ .  pointer-to-member-function    ]
// [0000000000000000][function pointer][0000000000000000]
//
// thunks that do not contain a function or delegate are incomparable.
//
// thus sorting will order by functions then delegates.
// functions are ordered by increasing address
// delegates are ordered by bound object, then bound member pointer.
//
struct storage {
    static constexpr size_t sbo_align =
        alignof(std::tuple<void*, erased_mem_fn>);
    static constexpr size_t sbo_size = sizeof(std::tuple<void*, erased_mem_fn>);

    // difference between size of function pointer and size of member pointer.
    // used to make padding explicit and participate in object representation.
    static constexpr size_t pad_size =
        sizeof(erased_mem_fn) - sizeof(erased_fn);

    // layout facilitates `memcmp` for supported cases
    union {
        struct {
            void* _obj;              // bound-object or null
            union {
                struct {
                    erased_fn _func; // bound function pointer
                    std::byte _pad[pad_size];
                };
                erased_mem_fn _mfn;  // bound member function pointer
            };
        };
        alignas(sbo_align) std::byte _buf[sbo_size];
    };

    constexpr storage() : _buf{} {};

    template<typename T>
    static consteval bool is_local()
    {
        return sizeof(T) <= sbo_size && alignof(T) <= sbo_align &&
               std::is_nothrow_move_constructible_v<T>;
    }

    template<typename T>
    static consteval bool is_local_trivial()
    {
        return is_local<T>() && std::is_trivially_copyable_v<T>;
    }

    template<typename T, typename... Args>
    static consteval bool is_nothrow_init()
    {
        return is_local<T>() && std::is_nothrow_constructible_v<T, Args...>;
    }

    constexpr void* addr() noexcept { return _buf; }
    constexpr const void* addr() const noexcept { return _buf; }

    constexpr const std::byte* bytes() const noexcept
    {
        return reinterpret_cast<const std::byte*>(this);
    }

    template<typename T>
    constexpr const T* get() const noexcept
    {
        if constexpr (std::is_function_v<T>) {
            return reinterpret_cast<const T*>(_func);
        }
        else if constexpr (!is_local<T>()) {
            // allocated and _obj holds the pointer
            return static_cast<const T*>(_obj);
        }
        else {
            // local and _buf holds the value
            return static_cast<const T*>(addr());
        }
    }

    template<typename T>
    constexpr T* get() noexcept
    {
        if constexpr (std::is_function_v<T>) {
            return reinterpret_cast<T*>(_func);
        }
        else if constexpr (!is_local<T>()) {
            // allocated and _obj holds the pointer
            return static_cast<T*>(_obj);
        }
        else {
            // local and _buf holds the value
            return static_cast<T*>(addr());
        }
    }

    template<typename T, typename... Args>
        requires std::constructible_from<T, Args...>
    constexpr void init(Args&&... args) noexcept(is_nothrow_init<T, Args...>())
    {
        if constexpr (!is_local<T>()) {
            _obj = new T(std::forward<Args>(args)...);
        }
        else {
            ::new (_buf) T(std::forward<Args>(args)...);
        }
    }

    // initialize with function pointer.
    template<typename F>
        requires std::is_function_v<F>
    constexpr void init(F* f) noexcept
    {
        _func = erase_type(f);
    }

    // initialize with member function pointer and object pointer.
    template<typename M, typename O, typename T>
    constexpr void init(M O::* mptr, T* obj) noexcept
    {
        _obj = erase_type(obj);
        _mfn = erase_type(mptr);
    }

    // initialize with compile-time constant function pointer
    template<auto fun_ptr>
        requires is_function_ptr_v<decltype(fun_ptr)>
    constexpr void init() noexcept
    {
        // dont call through this because the function is a template
        // parameter, but store the address to enable comparisons.
        _func = erase_type(fun_ptr);
    }

    // initialize with compile-time constant pointer-to-member-fn
    // and a bound object
    template<auto mem_ptr, typename T>
        requires std::is_member_function_pointer_v<decltype(mem_ptr)>
    constexpr void init(T* obj) noexcept
    {
        _obj = erase_type(obj);
        // store (but don't call) for comparisons
        _mfn = erase_type(mem_ptr);
    }

    // zeroes the storage.
    constexpr void clear() noexcept { std::memset(this, 0, sizeof(storage)); }

    static constexpr const std::byte* key_of(const storage* self) noexcept
    {
        return self->_buf;
    }
};

static_assert(std::has_unique_object_representations_v<storage>,
              "thunk storage has unexpected padding");

struct thunk_ops_base {
    using destroyer_t = void (*)(storage*) noexcept;
    using mover_t = void (*)(storage*, storage*) noexcept;
    using copier_t = void (*)(storage*, const storage*);
    using keyer_t = const std::byte* (*)(const storage*) noexcept;

    destroyer_t destroy = &default_destroy;
    mover_t move_to = &default_move;
    copier_t copy_to = &default_copy;
    keyer_t key = &default_key;

    static constexpr void default_destroy(storage*) noexcept {};
    static constexpr void default_move(storage* dst, storage* src) noexcept
    {
        // default is same as copy.
        default_copy(dst, src);
    }
    static constexpr void default_copy(storage* dst, const storage* src)
    {
        // default does a trivial copy.
        new (dst) storage(*src);
    }
    static constexpr const std::byte* default_key(const storage*) noexcept
    {
        return nullptr;
    }
};

template<typename R, typename... Args>
struct thunk_ops : public thunk_ops_base {
    using invoker_t = R (*)(storage*, Args...);

    invoker_t invoke = nullptr;
};

inline static constexpr thunk_ops<void> empty_ops{};

// a function pointer template parameter is a trivially copyable type and only
// needs an invoker.
template<auto fn, typename R, typename... Args>
struct nttp_fn_ops {
    static R invoke(storage*, Args... args)
    {
        return std::invoke_r<R>(fn, std::forward<Args>(args)...);
    }

    static constexpr thunk_ops<R, Args...> table{
        {.key = &storage::key_of},
        &invoke,
    };
};

// a member function pointer template parameter is also trivially copyable
template<auto mem_f, typename T, typename R, typename... Args>
struct nttp_mfn_ops {
    static R invoke(storage* self, Args... args)
    {
        T* obj = self->get<T>();
        return std::invoke_r<R>(mem_f, obj, std::forward<Args>(args)...);
    }

    static constexpr thunk_ops<R, Args...> table{
        {.key = &storage::key_of},
        &invoke,
    };
};

// `T` is stored in `storage._buf`
template<typename T, typename R, typename... Args>
struct local_ops {

    static R invoke(storage* self, Args... args)
    {
        T* obj = self->get<T>();
        return std::invoke_r<R>(*obj, std::forward<Args>(args)...);
    }

    static void destroy(storage* self) noexcept
    {
        T* obj = self->get<T>();
        std::destroy_at(obj);
    }

    static void move_to(storage* dst, storage* src) noexcept
    {
        T* source = src->get<T>();
        T* target = dst->get<T>();
        new (target) T(std::move(*source));
        std::destroy_at(source);
    }

    static void copy_to(storage* dst, const storage* src)
    {
        const T* source = src->get<T>();
        T* target = dst->get<T>();
        new (target) T(*source);
    }

    static constexpr thunk_ops<R, Args...> table = {
        {
            .destroy = &destroy,
            .move_to = &move_to,
            .copy_to = &copy_to,
        },
        &invoke,
    };
};

// function pointers are trivial
template<typename R, typename... Args>
struct fnptr_ops {
    static R invoke(storage* self, Args&&... args)
    {
        auto* fn = self->get<R(Args...)>();
        return std::invoke_r<R>(fn, std::forward<Args>(args)...);
    }

    static constexpr thunk_ops<R, Args...> table = {
        {.key = &storage::key_of},
        &invoke,
    };
};

// a delegate (object pointer and member function pointer) is trivial.
template<typename M, typename T, typename R, typename... Args>
struct delegate_ops {
    static R invoke(storage* self, Args... args)
    {
        auto mfn = reinterpret_cast<M>(self->_mfn);
        auto obj = self->get<T>();
        return std::invoke_r<R>(mfn, obj, std::forward<Args>(args)...);
    }

    static constexpr thunk_ops<R, Args...> table{
        {
            .key = &storage::key_of,
        },
        &invoke,
    };
};

// ops for types that are allocated on the heap and thunk holds their pointer
template<typename T, typename R, typename... Args>
struct allocated_ops {
    static void destroy(storage* self) noexcept
    {
        T* obj = self->get<T>();
        delete obj;
    }

    static void move_to(storage* dst, storage* src) noexcept
    {
        dst->_obj = std::exchange(src->_obj, nullptr);
    }

    static void copy_to(storage* dst, const storage* src)
    {
        auto* source = src->get<T>();
        dst->_obj = new T(*source);
    }

    static R invoke(storage* self, Args... args)
    {
        T* obj = self->get<T>();
        return std::invoke_r<R>(*obj, std::forward<Args>(args)...);
    }

    static constexpr thunk_ops<R, Args...> table = {
        {
            .destroy = &destroy,
            .move_to = &move_to,
            .copy_to = &copy_to,
        },
        &invoke,
    };
};

template<typename = void>
class thunk;

template<>
class thunk<> {
public:
    thunk(nullptr_t) = delete;

protected:
    const thunk_ops_base* _ops;
    storage _store = {};

    ~thunk() = default;

    thunk() noexcept : _ops{&empty_ops} {}

    explicit thunk(const thunk_ops_base* ops) noexcept : _ops{ops} {}

    thunk(const thunk& other) : _ops{other._ops}
    {
        _ops->copy_to(&_store, &other._store);
    }
    thunk& operator=(const thunk& other)
    {
        if (this == &other) {
            return *this;
        }
        destroy();
        _ops = other._ops;
        _ops->copy_to(&this->_store, &other._store);
        return *this;
    }

    thunk(thunk&& other) noexcept : _ops{std::exchange(other._ops, &empty_ops)}
    {
        _ops->move_to(&this->_store, &other._store);
    }

    thunk& operator=(thunk&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        destroy();
        _ops = std::exchange(other._ops, &empty_ops);
        _ops->move_to(&this->_store, &other._store);
        return *this;
    }

    void destroy() noexcept { _ops->destroy(&_store); }
};

template<typename R, typename... Args>
class thunk<R(Args...)> : public thunk<> {

    using ops_t = thunk_ops<R, Args...>;

    const ops_t* ops() const { return static_cast<const ops_t*>(_ops); }

public:
    thunk() noexcept = default;

    thunk(const thunk&) = default;
    thunk& operator=(const thunk&) = default;
    thunk(thunk&&) = default;
    thunk& operator=(thunk&&) = default;

    void swap(thunk& other) noexcept
    {
        thunk tmp{std::move(other)};
        other = std::move(*this);
        *this = std::move(tmp);
    }

    thunk& operator=(nullptr_t) noexcept
    {
        destroy();
        _ops = &empty_ops;
        return *this;
    }

    ~thunk() { destroy(); }

    void reset() noexcept
    {
        destroy();
        _ops = &empty_ops;
    }
    bool empty() const noexcept { return _ops == &empty_ops; }

    template<typename T, typename VT = std::decay_t<T>>
        requires(!std::same_as<VT, thunk>) && std::constructible_from<VT, T> &&
                std::is_invocable_r_v<R, VT, Args...> &&
                std::is_invocable_r_v<R, VT&, Args...> && std::copyable<VT>
    thunk(T&& v)
    {
        if constexpr (storage::is_local<VT>()) {
            _ops = &local_ops<VT, R, Args...>::table;
        }
        else {
            _ops = &allocated_ops<VT, R, Args...>::table;
        }
        _store.init<VT>(std::forward<T>(v));
    };

    template<typename M, typename T>
        requires std::is_member_function_pointer_v<M> &&
                 std::is_invocable_r_v<R, M, T*, Args...>
    thunk(M mfn, T* obj) : thunk<>{&delegate_ops<M, T, R, Args...>::table}
    {
        _store.init(mfn, obj);
    }

    template<typename F>
        requires std::is_function_v<F>
    thunk(F* fun) : thunk<>{&fnptr_ops<R, Args...>::table}
    {
        _store.init(fun);
    }

    template<auto f>
        requires is_function_ptr_v<decltype(f)> &&
                 std::is_invocable_r_v<R, decltype(f), Args...>
    thunk(nontype_t<f>) : thunk<>{&nttp_fn_ops<f, R, Args...>::table}
    {
        _store.init<f>();
    }

    template<auto f, typename T>
        requires std::is_member_function_pointer_v<decltype(f)> &&
                 std::is_invocable_r_v<R, decltype(f), const T*, Args...>
    thunk(nontype_t<f>, const T* obj)
        : thunk<>{&nttp_mfn_ops<f, const T, R, Args...>::table}
    {
        _store.init<f>(obj);
    };

    template<auto f, typename T>
        requires std::is_member_function_pointer_v<decltype(f)> &&
                 std::is_invocable_r_v<R, decltype(f), T*, Args...>
    thunk(nontype_t<f>, T* obj)
        : thunk<>{&nttp_mfn_ops<f, T, R, Args...>::table}
    {
        _store.init<f>(obj);
    };

    // invoke
    R operator()(Args... args)
    {
        return ops()->invoke(&_store, std::forward<Args>(args)...);
    }

    // comparisons
    friend std::partial_ordering operator<=>(const thunk& lhs, const thunk& rhs)
    {
        auto* lkey = lhs._ops->key(&lhs._store);
        auto* rkey = rhs._ops->key(&rhs._store);

        if (lkey == nullptr || rkey == nullptr) {
            return std::partial_ordering::unordered;
        }

        auto v = std::memcmp(lkey, rkey, sizeof(storage));

        if (v > 0) return std::partial_ordering::greater;
        if (v < 0) return std::partial_ordering::less;
        return std::partial_ordering::equivalent;
    }

    friend bool operator==(const thunk& lhs, const thunk& rhs)
    {
        return std::is_eq(lhs <=> rhs);
    }
};

template<typename M, typename T>
thunk(M T::*, T*) -> thunk<thunk_signature_t<M T::*>>;

template<typename M, typename T>
thunk(M T::*, const T*) -> thunk<thunk_signature_t<M T::*>>;

template<typename F>
thunk(F) -> thunk<thunk_signature_t<decltype(&F::operator())>>;

template<typename R, typename... Args>
thunk(R (*)(Args...)) -> thunk<R(Args...)>;

template<typename R, typename... Args>
thunk(R (*)(Args...) noexcept) -> thunk<R(Args...)>;

template<auto f>
thunk(nontype_t<f>) -> thunk<std::remove_pointer_t<decltype(f)>>;

template<auto f, typename T>
thunk(nontype_t<f>, T*) -> thunk<thunk_signature_t<decltype(f)>>;

template<typename C>
thunk(C*) -> thunk<thunk_signature_t<decltype(&C::operator())>>;

inline void foo() {}

TEST_CASE("one")
{
    MESSAGE("ayy");
    thunk x(foo);
    x();
    x.empty();
    thunk<>& y = x;
    y = nullptr;
}
} // namespace kerix

#endif
