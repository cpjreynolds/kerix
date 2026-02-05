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

#ifndef KERIX_KERIX_HPP
#define KERIX_KERIX_HPP

#include <compare>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <tuple>
#include <algorithm>

#include <oneapi/tbb/concurrent_queue.h>

namespace kerix {

using std::scoped_lock;
using std::shared_lock;
using std::shared_mutex;
using std::shared_ptr;
using std::tuple;
using std::vector;
using std::weak_ptr;
using namespace std::ranges;
using tbb::concurrent_queue;

using uint128_t = unsigned __int128;

class expired_slot : public std::runtime_error {
public:
    expired_slot() noexcept : runtime_error{message} {};

private:
    static constexpr const char* message = "attempt to call expired slot";
};

class bad_thunk : public std::runtime_error {
public:
    bad_thunk() noexcept : runtime_error{message} {};

private:
    static constexpr const char* message = "attempt to call null thunk";
};

template<typename = void>
struct thunk;

/*
 * Type-erased thunk.
 */
template<>
struct thunk<void> {
#if defined(KERIX_LITTLE_ENDIAN)
    union {
        uint128_t value;
        struct {
            void* fnptr;
            void* iptr;
        };
        struct {
            uintptr_t fnptr_v;
            uintptr_t iptr_v;
        };
    };
    template<typename T, typename R, typename... Args>
    constexpr thunk(R (*fnptr)(void*, Args...), T* iptr) noexcept
        : fnptr{reinterpret_cast<void*>(fnptr)},
          iptr{reinterpret_cast<void*>(iptr)} {};

    template<typename R, typename... Args>
    constexpr thunk(R (*fnptr)(void*, Args...), nullptr_t) noexcept
        : fnptr{reinterpret_cast<void*>(fnptr)},
          iptr{nullptr} {};
#elif defined(KERIX_BIG_ENDIAN)
    union {
        uint128_t value;
        struct {
            void* iptr;
            void* fnptr;
        };
        struct {
            uintptr_t iptr_v;
            uintptr_t fnptr_v;
        };
    };
    template<typename T, typename R, typename... Args>
    constexpr thunk(R (*fnptr)(void*, Args...), T* iptr) noexcept
        : iptr{reinterpret_cast<void*>(iptr)},
          fnptr{reinterpret_cast<void*>(fnptr)} {};

    template<typename R, typename... Args>
    constexpr slot(R (*fnptr)(void*, Args...), nullptr_t) noexcept
        : fnptr{reinterpret_cast<void*>(fnptr)},
          iptr{nullptr} {};
#endif
    constexpr thunk() noexcept : value{0} {};

    explicit operator bool() const noexcept { return value != 0; }

    friend constexpr std::strong_ordering operator<=>(const thunk& lhs,
                                                      const thunk& rhs)
    {
        return lhs.value <=> rhs.value;
    }

    friend constexpr bool operator==(const thunk& lhs, const thunk& rhs)
    {
        return lhs.value == rhs.value;
    }

    friend constexpr auto operator<=>(const thunk& lhs, const void* rhs)
    {
        return lhs.iptr <=> rhs;
    }

    friend constexpr bool operator==(const thunk& lhs, const void* rhs)
    {
        return lhs.iptr == rhs;
    }
};

template<typename R, typename... Ts>
struct thunk<R(Ts...)> : public thunk<void> {
private:
    using thunk<void>::thunk;
    using fn_type = R (*)(void*, Ts&&...);

    template<auto mem_ptr, typename T>
        requires std::is_pointer_v<T>
    static consteval fn_type make_thunk()
    {
        return [](void* iptr, Ts&&... args) {
            return (reinterpret_cast<T>(iptr)->*mem_ptr)(
                std::forward<Ts>(args)...);
        };
    }

    template<typename C>
        requires std::is_pointer_v<C>
    static consteval fn_type make_thunk()
    {
        return [](void* iptr, Ts&&... args) {
            return reinterpret_cast<C>(iptr)->operator()(
                std::forward<Ts>(args)...);
        };
    }

    template<auto fun_ptr>
    static consteval fn_type make_thunk()
    {
        return [](void*, Ts&&... args) {
            return (*fun_ptr)(std::forward<Ts>(args)...);
        };
    }

public:
    constexpr thunk(const thunk<void>& tk) noexcept : thunk<void>{tk} {};

    template<auto fun_ptr>
        requires std::is_invocable_r_v<R, decltype(fun_ptr), Ts...>
    static constexpr thunk bind()
    {
        return {make_thunk<fun_ptr>(), nullptr};
    }

    template<auto mem_ptr, typename T>
        requires std::is_invocable_r_v<R, decltype(mem_ptr), T*, Ts...>
    static constexpr thunk bind(T* obj)
    {
        return {make_thunk<mem_ptr, decltype(obj)>(), obj};
    }

    template<auto mem_ptr, typename T>
        requires std::is_invocable_r_v<R, decltype(mem_ptr), const T*, Ts...>
    static constexpr thunk bind(const T* obj)
    {
        return {make_thunk<mem_ptr, decltype(obj)>(), obj};
    }

    template<typename C>
        requires std::is_invocable_r_v<R, C, Ts...>
    static constexpr thunk bind(C* obj)
    {
        return {make_thunk<decltype(obj)>(), obj};
    }

    template<typename C>
        requires std::is_invocable_r_v<R, const C, Ts...>
    static constexpr thunk bind(const C* obj)
    {
        return {make_thunk<decltype(obj)>(), obj};
    }

protected:
    template<typename... Args>
    R call(Args&&... args) const
    {
        if constexpr (KERIX_DEBUG) {
            if (!bool(*this)) {
                throw bad_thunk{};
            }
        }
        return reinterpret_cast<fn_type>(fnptr)(iptr,
                                                static_cast<Ts&&>(args)...);
    }
};

// holds a thunk and info on which signals the slot is connected to
template<typename>
class slot;

// holds info on connected slots.
template<typename>
class signal;

// holds a thunk and info on which signals the slot is connected to
template<typename R, typename... Ts>
class slot<R(Ts...)> : public thunk<R(Ts...)> {
public:
    using thunk<R(Ts...)>::thunk;

    slot() = delete;
    slot(const slot&) = default;
    slot& operator=(const slot&) = default;
    slot(slot&&) = default;
    slot& operator=(slot&&) = default;

    template<typename... Args>
    R operator()(Args&&... args) const
    {
        const auto lk = lock();
        return thunk<R(Ts...)>::call(std::forward<Args>(args)...);
    }

    bool expired() const
    {
        for (const auto& p : trackers) {
            if (p.expired())
                return true;
        }
        return false;
    }

    slot& track(const weak_ptr<void>& p)
    {
        trackers.push_back(p);
        return *this;
    }

    // adds another slot's tracking list to this one.
    template<typename Q>
    slot& track(const slot<Q>& other)
    {
        for (const auto& p : other.block->trackers) {
            trackers.push_back(p);
        }
        return *this;
    }

    // tracks a signal
    template<typename Q>
    slot& track(const signal<Q>& other)
    {
        trackers.push_back(other.block);
        return *this;
    }

private:
    vector<shared_ptr<void>> lock() const
    {
        vector<shared_ptr<void>> objs(trackers.size());

        for (const auto& t : trackers) {
            if (auto p = t.lock()) {
                objs.push_back(std::move(p));
            }
            else {
                throw expired_slot{};
            }
        }
        return objs;
    }

    vector<weak_ptr<void>> trackers;
};

class signal_block_base {
public:
    virtual void do_disconnect(const thunk<>&) = 0;
    virtual bool contains(const thunk<>&) = 0;
    virtual void add_tracker(const thunk<>&, const weak_ptr<void>&) = 0;

protected:
    ~signal_block_base() {}
};

// represents the connection between a signal and slot.
class connection {
public:
    constexpr connection() noexcept {};

    bool connected() const
    {
        if (auto p = _sig.lock()) {
            return p->contains(_tk);
        }
        else {
            return false;
        }
    }
    void disconnect() const
    {
        if (auto p = _sig.lock()) {
            p->do_disconnect(_tk);
        }
    }

    connection& track(const weak_ptr<void>& p)
    {
        if (auto blk = _sig.lock()) {
            blk->add_tracker(_tk, p);
        }
        return *this;
    }

    friend bool operator==(const connection& lhs, const connection& rhs)
    {
        return lhs._sig.lock() == rhs._sig.lock() && lhs._tk == rhs._tk;
    }

    friend bool operator!=(const connection& lhs, const connection& rhs)
    {
        return !(lhs == rhs);
    }

    friend bool operator<(const connection& lhs, const connection& rhs)
    {
        return std::owner_less<void>{}(lhs._sig, rhs._sig) && lhs._tk < rhs._tk;
    }

private:
    template<typename>
    friend class signal;

    connection(const weak_ptr<signal_block_base>& p, const thunk<>& tk) noexcept
        : _sig{p},
          _tk{tk} {};

    weak_ptr<signal_block_base> _sig;
    thunk<> _tk;
};

class scoped_connection : public connection {
public:
    scoped_connection() noexcept {};
    scoped_connection& operator=(const scoped_connection& rhs) noexcept
    {
        disconnect();
        connection::operator=(rhs);
        return *this;
    }

    scoped_connection(scoped_connection&& other) noexcept
        : connection(std::move(other)) {};

    scoped_connection(connection&& other) noexcept
        : connection(std::move(other)) {};

    scoped_connection& operator=(scoped_connection&& other) noexcept
    {
        if (&other == this)
            return *this;
        disconnect();
        connection::operator=(std::move(other));
        return *this;
    }
    scoped_connection& operator=(connection&& other) noexcept
    {
        if (&other == this)
            return *this;
        disconnect();
        connection::operator=(std::move(other));
        return *this;
    }

    ~scoped_connection() { disconnect(); }
};

template<typename>
class signal_block;

template<typename R, typename... Ts>
class signal_block<R(Ts...)> final : public signal_block_base {
    template<typename>
    friend class slot;

    using slot_type = slot<R(Ts...)>;

    mutable shared_mutex mtx;
    vector<slot<R(Ts...)>> slots;
    concurrent_queue<tuple<Ts...>> evqueue;

public:
    template<typename... Args>
    void fire(Args&&... args) const
    {
        shared_lock lk{mtx};
        for (auto& fn : slots) {
            fn(std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void do_post(Args&&... args)
    {
        evqueue.emplace(static_cast<Ts&&>(args)...);
    }

    void do_flush()
    {
        shared_lock lk{this->mtx};
        tuple<Ts...> event;
        while (evqueue.try_pop(event)) {
            for (auto& fn : this->slots) {
                std::apply(fn, event);
            }
        }
    }

    template<typename Accumulate>
    void do_flush_accumulate(Accumulate&& acc)
    {
        shared_lock lk{this->mtx};
        tuple<Ts...> event;
        while (evqueue.try_pop(event)) {
            for (auto& fn : this->slots) {
                acc(std::apply(fn, event));
            }
        }
    }

    template<typename Accumulate, typename... Args>
    void fire_accumulate(Accumulate&& acc, Args&&... args) const
    {
        shared_lock lk{mtx};
        for (auto& fn : slots) {
            acc(fn(std::forward<Args>(args)...));
        }
    }

    void do_connect(const slot_type& slt)
    {
        scoped_lock lk{mtx};
        slots.emplace(upper_bound(slots, slt), slt);
    }

    void do_connect(const thunk<>& tk)
    {
        scoped_lock lk{mtx};
        slots.emplace(upper_bound(slots, tk), tk);
    }

    void do_disconnect(const void* ptr)
    {
        scoped_lock lk{mtx};
        auto [b, e] = equal_range(slots, ptr, std::less{});
        slots.erase(b, e);
    }

    void do_disconnect(const thunk<>& tk) final override
    {
        scoped_lock lk{mtx};
        auto [b, e] = equal_range(slots, tk);
        slots.erase(b, e);
    }

    bool contains(const thunk<>& tk) final override
    {
        shared_lock lk{mtx};
        auto [b, e] = equal_range(slots, tk);
        return b != e;
    }

    void add_tracker(const thunk<>& tk, const weak_ptr<void>& p) final override
    {
        shared_lock lk{mtx};
        auto [b, e] = equal_range(slots, tk);
        b->track(p);
    }
};

template<typename R, typename... Ts>
class signal<R(Ts...)> {
    using slot_type = slot<R(Ts...)>;
    using thunk_type = thunk<R(Ts...)>;
    using block_type = signal_block<R(Ts...)>;

public:
    // immediate notification of all slots.
    template<typename... Args>
    void operator()(Args&&... args) const
    {
        block->fire(std::forward<Args>(args)...);
    }

    // immediate notification of all slots and accumulate return values.
    template<typename Accumulate, typename... Args>
    void accumulate(Accumulate&& acc, Args&&... args) const
    {
        block->fire_accumulate(std::forward<Accumulate>(acc),
                               std::forward<Args>(args)...);
    }

    // post an event to the internal queue.
    template<typename... Args>
    void post(Args&&... args)
    {
        block->do_post(std::forward<Args>(args)...);
    }

    // flush the event queue to all slots.
    void flush() { block->do_flush(); }

    // flush the event queue to all slots and accumulate return values.
    template<typename Accumulate>
    void flush(Accumulate&& acc)
    {
        block->do_flush_accumulate(std::forward<Accumulate>(acc));
    }

    // connects an already constructed slot object.
    connection connect(const slot_type& s)
    {
        block->do_connect(s);
        return connection{block, s};
    }

    // connects an object and member function
    template<auto mem_ptr, typename T>
        requires std::is_invocable_r_v<R, decltype(mem_ptr), T*, Ts...>
    connection connect(T* obj)
    {
        const auto tk = thunk_type::template bind<mem_ptr>(obj);
        block->do_connect(tk);
        return connection{block, tk};
    }

    // connects a const object and member function
    template<auto mem_ptr, typename T>
        requires std::is_invocable_r_v<R, decltype(mem_ptr), const T*, Ts...>
    connection connect(const T* obj)
    {
        const auto tk = thunk_type::template bind<mem_ptr>(obj);
        block->do_connect(tk);
        return connection{block, tk};
    }

    // connects an object and member function
    template<typename T, R (T::*mem_ptr)(Ts...)>
    connection connect(T* obj)
    {
        return connect<mem_ptr>(obj);
    }
    // connects an object and member function
    template<typename T, R (T::*mem_ptr)(Ts...) noexcept>
    connection connect(T* obj)
    {
        return connect<mem_ptr>(obj);
    }
    // connects a const object and member function
    template<typename T, R (T::*mem_ptr)(Ts...) const>
    connection connect(const T* obj)
    {
        return connect<mem_ptr>(obj);
    }
    // connects a const object and member function
    template<typename T, R (T::*mem_ptr)(Ts...) const noexcept>
    connection connect(const T* obj)
    {
        return connect<mem_ptr>(obj);
    }

    // connects a function pointer
    template<R (*fun_ptr)(Ts...)>
    connection connect()
    {
        const auto tk = thunk_type::template bind<fun_ptr>();
        block->do_connect(tk);
        return connection{block, tk};
    }
    // connects a function pointer
    template<R (*fun_ptr)(Ts...) noexcept>
    connection connect()
    {
        const auto tk = thunk_type::template bind<fun_ptr>();
        block->do_connect(tk);
        return connection{block, tk};
    }

    // connects a callable object.
    template<typename C>
        requires std::is_invocable_r_v<R, C, Ts...>
    connection connect(C* callable)
    {
        const auto tk = thunk_type::template bind<C>(callable);
        block->do_connect(tk);
        return connection{block, tk};
    }
    // connects a const callable object.
    template<typename C>
        requires std::is_invocable_r_v<R, const C, Ts...>
    connection connect(const C* callable)
    {
        const auto tk = thunk_type::template bind<C>(callable);
        block->do_connect(tk);
        return connection{block, tk};
    }

    // disconnect a slot.
    void disconnect(const slot_type& slt) { block->do_disconnect(slt); }

    // member function disconnect
    template<auto mem_ptr, typename T>
        requires std::is_invocable_r_v<R, decltype(mem_ptr), T*, Ts...>
    void disconnect(const T* obj)
    {
        block->do_disconnect(slot_type::template bind<mem_ptr>(obj));
    }
    // member function disconnect
    template<typename T, R (T::*mem_ptr)(Ts...)>
    void disconnect(const T* obj)
    {
        disconnect<mem_ptr>(obj);
    }
    // member function disconnect
    template<typename T, R (T::*mem_ptr)(Ts...) noexcept>
    void disconnect(const T* obj)
    {
        disconnect<mem_ptr>(obj);
    }
    // member function disconnect
    template<typename T, R (T::*mem_ptr)(Ts...) const>
    void disconnect(const T* obj)
    {
        disconnect<mem_ptr>(obj);
    }
    // member function disconnect
    template<typename T, R (T::*mem_ptr)(Ts...) const noexcept>
    void disconnect(const T* obj)
    {
        disconnect<mem_ptr>(obj);
    }

    // function pointer disconnect.
    template<R (*fun_ptr)(Ts...)>
    void disconnect()
    {
        block->do_disconnect(slot_type::template bind<fun_ptr>());
    }

    // function pointer disconnect.
    template<R (*fun_ptr)(Ts...) noexcept>
    void disconnect()
    {
        block->do_disconnect(slot_type::template bind<fun_ptr>());
    }

    // disconnects a callable or all members of an instance
    template<typename T>
    void disconnect(const T* obj)
    {
        block->do_disconnect(obj);
    }

    // disconnects all slots
    void disconnect_all() { block->slots.clear(); }

private:
    shared_ptr<block_type> block = std::make_shared<block_type>();
};

template<typename>
class observer;

inline void foo() {}

} // namespace kerix

#endif
