#ifndef KERIX_THUNK_HPP
#define KERIX_THUNK_HPP

#ifdef _MSC_VER
#pragma pointers_to_members(full_generality)
#endif

#include <compare>
#include <concepts>
#include <functional>
#include <print>
#include <type_traits>

#include <boost/compat/function_ref.hpp>
#include <boost/compat/move_only_function.hpp>
#include <kerix/types.hpp>

namespace kerix {

static_assert(
    sizeof(void*) == sizeof(void (*)()),
    "data and function pointer size mismatch. you have a wild platform.");

template<typename>
struct delegate;

template<typename F, typename T>
struct delegate<F T::*> {
    T* _obj;
    F T::* _mfn;
};

struct storage {
    static constexpr size_t sbo_align = alignof(delegate<erased_mem_fn>);
    static constexpr size_t sbo_size = sizeof(delegate<erased_mem_fn>);

    // layout facilitates `memcmp` for supported cases
    union {
        struct {
            void* _obj;          // bound-object or null
            union {
                erased_fn _func; // bound function pointer, or invoker address
                erased_mem_fn _mfn; // bound mem fn
            };
        };
        alignas(sbo_align) unsigned char _buf[sbo_size];
    };

    constexpr storage() : _buf{} {};

    template<typename T>
    static consteval bool is_local()
    {
        return sizeof(T) <= sizeof(storage) && alignof(T) <= alignof(storage) &&
               std::is_nothrow_move_constructible_v<T>;
    }

    template<typename T>
    static consteval bool is_local_trivial()
    {
        return is_local<T> && std::is_trivially_copyable_v<T>;
    }

    template<typename T, typename... Args>
    static consteval bool is_nothrow_init()
    {
        return is_local<T>() && std::is_nothrow_constructible_v<T, Args...>;
    }

    constexpr void* addr() noexcept { return _buf; }

    template<typename T>
    constexpr T* get() noexcept
    {
        if constexpr (std::is_function_v<T>) {
            static_assert(false);
        }
        else if constexpr (is_function_ptr_v<T>) {
            return reinterpret_cast<T*>(&_func);
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

    template<typename F>
        requires std::is_function_v<F>
    constexpr void init(F* f) noexcept
    {
        _func = erase_type(f);
    }

    template<typename M, typename O, typename T>
    constexpr void init(M O::* mptr, T* obj) noexcept
    {
        _obj = erase_type(obj);
        _mfn = erase_type(mptr);
    }

    template<auto fun_ptr>
        requires is_function_ptr_v<decltype(fun_ptr)>
    constexpr void init() noexcept
    {
        _func = erase_type(fun_ptr);
    }

    template<auto fun_ptr, typename T>
        requires is_function_ptr_v<decltype(fun_ptr)>
    constexpr void init(T* obj) noexcept
    {
        _obj = erase_type(obj);
        init<fun_ptr>();
    }
};

class bad_thunk : public std::bad_function_call {};

template<typename>
class thunk;

enum class op_t {
    move,
    copy,
    destroy,
    get_key,
    get_obj,
};

// performs move/copy/destructor/comparison on type-erased storage
struct manager {

    using signature = storage*(op_t op, storage* dst, storage* src);

    template<typename T>
    static storage* local(op_t op, storage* target, storage* source)
    {
        T* src = source->get<T>();
        T* tgt = target->get<T>();

        switch (op) {
        case op_t::move:
            ::new (tgt) T(std::move(*src));
            std::destroy_at(src);
            break;
        case op_t::copy:
            ::new (tgt) T(*src);
            break;
        case op_t::destroy:
            std::destroy_at(tgt);
            break;
        case op_t::get_key:
        case op_t::get_obj:
            break;
        };
        return nullptr;
    }

    static storage* trivial(op_t op, storage* target, storage* source)
    {
        switch (op) {
        case op_t::move:
        case op_t::copy:
            ::new (target) storage(*source);
            break;
        case op_t::destroy:
        case op_t::get_key:
        case op_t::get_obj:
            break;
        };
        return nullptr;
    }

    static storage* trivial_key(op_t op, storage* target, storage* source)
    {
        switch (op) {
        case op_t::move:
        case op_t::copy:
            ::new (target) storage(*source);
            return nullptr;
        case op_t::get_key:
            return target;
        case op_t::destroy:
        case op_t::get_obj:
            return nullptr;
        };
    }

    static storage* trivial_objkey(op_t op, storage* target, storage* source)
    {
        switch (op) {
        case op_t::move:
        case op_t::copy:
            ::new (target) storage(*source);
            return nullptr;
        case op_t::destroy:
            return nullptr;
        case op_t::get_key:
        case op_t::get_obj:
            return target;
        };
    }

    template<typename T>
    static storage* allocated(op_t op, storage* target, storage* source)
    {
        auto src = source->get<T>();
        switch (op) {
        case op_t::move:
            target->_obj = source->_obj;
            break;
        case op_t::copy:
            target->_obj = new T(*src);
            break;
        case op_t::destroy:
            delete src;
            break;
        case op_t::get_key:
        case op_t::get_obj:
            break;
        };
        return nullptr;
    }
};

template<typename>
struct invoker;

// invokes the type-erased storage thunk
template<typename R, typename... Args>
struct invoker<R(Args...)> {
    using signature = R(storage*, Args...);

    template<auto f>
    static R nttp_f(storage*, Args... args)
    {
        return std::invoke_r<R>(f, std::forward<Args>(args)...);
    }

    template<auto mem_f, typename T>
    static R nttp_m(storage* s, Args... args)
    {
        T* obj = s->get<T>();
        return std::invoke_r<R>(mem_f, obj, std::forward<Args>(args)...);
    }

    template<typename T>
    static R regular(storage* s, Args... args)
    {
        T* obj = s->get<T>();
        return std::invoke_r<R>(*obj, std::forward<Args>(args)...);
    }

    template<typename M, typename T>
    static R delegation(storage* s, Args... args)
    {
        auto mfn = reinterpret_cast<M>(s->_mfn);
        auto obj = s->get<T>();
        return std::invoke_r<R>(mfn, obj, std::forward<Args>(args)...);
    }

    template<typename T>
    static consteval auto* get()
    {
        return invoker::regular<T>;
    }

    template<typename M, typename T>
    consteval auto* get()
    {
        return invoker::delegation<M, T>;
    }
    template<auto f>
    consteval auto* get()
    {
        return invoker::template nttp_f<f>;
    }

    template<auto f, typename T>
    consteval auto* get()
    {
        return invoker::template nttp_m<f, T>;
    }
};

struct foo {
    int merp(int) const;

    int meep(int);
};

template<typename>
struct ttt;

template<typename M, typename T>
struct ttt<M T::*> {
    using type = M;
};

static constexpr foo xyy;

int merp(int);

template<typename R, typename... Args>
class thunk<R(Args...)> {
    using invoker_t = invoker<R(Args...)>;
    using manager_t = manager;

    storage _store;
    invoker_t::signature* _inv;
    manager_t::signature* _mgr;

public:
    template<typename T, typename VT = std::decay_t<T>>
        requires(!std::same_as<VT, thunk>) && std::constructible_from<VT, T>
    thunk(T&& v)
        : _inv{invoker_t::template regular<VT>},
          _mgr{&manager::trivial}
    {
        _store.init<VT>(std::forward<T>(v));
    };

    template<typename M, typename T>
        requires std::is_member_function_pointer_v<M> &&
                     std::is_invocable_r_v<R, M, T*, Args...>
    thunk(M mfn, T* obj)
        : _inv{invoker_t::template delegation<M, T>},
          _mgr{&manager::trivial_objkey}
    {
        _store.init(mfn, obj);
    }

    // template<typename F>
    // requires std::is_function_v<F> && std::is_invocable_r_v<R, F, Args...>
    // thunk(F* fun) : _inv{&invoker_t::func},
    // _mgr{&manager::trivial_key}
    // {
    // _store.init<decltype(fun)>(fun);
    // }

    template<auto f>
        requires is_function_ptr_v<decltype(f)> &&
                     std::is_invocable_r_v<R, decltype(f), Args...>
    thunk(nontype_t<f>)
        : _inv{&invoker_t::template nttp_f<f>},
          _mgr{&manager::trivial_key}
    {
        auto ifn = &invoker_t::template nttp_f<f>;
        _store.init<ifn>();
    }

    template<auto f, typename T>
        requires std::is_member_function_pointer_v<decltype(f)> &&
                     std::is_invocable_r_v<R, decltype(f), const T*, Args...>
    thunk(nontype_t<f>, const T* obj)
        : _inv{&invoker_t::template nttp_m<f, const T>},
          _mgr{&manager_t::trivial_objkey}
    {
        auto ifn = &invoker_t::template nttp_m<f, const T>(obj);
        _store.init<ifn>(obj);
    };

    template<auto f, typename T>
        requires std::is_member_function_pointer_v<decltype(f)> &&
                     std::is_invocable_r_v<R, decltype(f), T*, Args...>
    thunk(nontype_t<f>, T* obj)
        : _inv{&invoker_t::template nttp_m<f, T>},
          _mgr{&manager_t::trivial_objkey}
    {
        auto ifn = &invoker_t::template nttp_m<f, T>(obj);
        _store.init<ifn>(obj);
    };

    R operator()(Args... args)
    {
        return _inv(&_store, std::forward<Args>(args)...);
    }
};

template<typename M, typename T>
thunk(M T::*, T*) -> thunk<thunk_signature_t<M T::*>>;

template<typename M, typename T>
thunk(M T::*, const T*) -> thunk<thunk_signature_t<M T::*>>;

// template<typename T, typename R, typename... Args>
// thunktion(R (T::*)(Args...), T*) -> thunktion<R(Args...)>;

template<typename F>
thunk(F) -> thunk<thunk_signature_t<decltype(&F::operator())>>;

template<typename R, typename... Args>
thunk(R (*)(Args...)) -> thunk<R(Args...)>;

constexpr int baz(int)
{
    return 1;
}

void bar()
{
    foo fff;
    storage s;
    auto* fp = &invoker<int(void)>::delegation<int (foo::*)(void), foo>;
    fp(&s);

    std::unique_ptr<int> xy(new int(1));
    auto fwun = [x = std::move(xy)](int n) {
        std::print("{}", n);
        return n;
    };

    auto yf = invoker<int(int)>::get<std::decay_t<int(int)>>();

    thunk fgc{&baz};

    // auto yy = auto(fwun);

    thunk<int(int)> tk{&foo::merp, &fff};
    thunk txx{&foo::merp, &fff};

    thunk txk{std::move(fwun)};

    int i = txk(123);
    int x = baz(1);
}

struct cbl {
    void operator()(int, int);
    void operator()(int);
};

template<auto f>
thunk(nontype_t<f>) -> thunk<std::remove_pointer_t<decltype(f)>>;

template<auto f, typename T>
thunk(nontype_t<f>, T*) -> thunk<thunk_signature_t<decltype(f)>>;

template<typename C>
thunk(C*) -> thunk<thunk_signature_t<decltype(&C::operator())>>;

} // namespace kerix

#endif
