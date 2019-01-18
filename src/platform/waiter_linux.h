#pragma once

#include <pthread.h>

#include <cstring>
#include <atomic>
#include <memory>
#include <type_traits>

#include "def.h"

#if __cplusplus >= 201703L
namespace std {

// deduction guides for std::unique_ptr
template <typename T, typename D>
unique_ptr(T* p, D&& d) -> unique_ptr<T, std::decay_t<D>>;

} // namespace std

namespace ipc {
namespace detail {

using std::unique_ptr;

#else /*__cplusplus < 201703L*/
namespace ipc {
namespace detail {

// deduction guides for std::unique_ptr
template <typename T, typename D>
constexpr auto unique_ptr(T* p, D&& d) {
    return std::unique_ptr<T, std::decay_t<D>> { p, std::forward<D>(d) };
}
#endif/*__cplusplus < 201703L*/

class waiter {
    pthread_mutex_t       mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t        cond_  = PTHREAD_COND_INITIALIZER;
    std::atomic<unsigned> counter_ { 0 };

public:
    using handle_t = void*;

private:
    constexpr static waiter* waiter_cast(handle_t h) {
        return static_cast<waiter*>(h);
    }

public:
    constexpr static handle_t invalid() {
        return nullptr;
    }

    handle_t open(char const * name) {
        if (name == nullptr || name[0] == '\0') return invalid();
        if (counter_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            // init mutex
            pthread_mutexattr_t mutex_attr;
            if (::pthread_mutexattr_init(&mutex_attr) != 0) {
                return invalid();
            }
            auto IPC_UNUSED_ guard_mutex_attr = unique_ptr(&mutex_attr, ::pthread_mutexattr_destroy);
            if (::pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0) {
                return invalid();
            }
            if (::pthread_mutex_init(&mutex_, &mutex_attr) != 0) {
                return invalid();
            }
            auto guard_mutex = unique_ptr(&mutex_, ::pthread_mutex_destroy);
            // init condition
            pthread_condattr_t cond_attr;
            if (::pthread_condattr_init(&cond_attr) != 0) {
                return invalid();
            }
            auto IPC_UNUSED_ guard_cond_attr = unique_ptr(&cond_attr, ::pthread_condattr_destroy);
            if (::pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) != 0) {
                return invalid();
            }
            if (::pthread_cond_init(&cond_, &cond_attr) != 0) {
                return invalid();
            }
            // no need to guard condition
            // release guards
            guard_mutex.release();
        }
        return this;
    }

    void close(handle_t h) {
        if (h == invalid()) return;
        auto w = waiter_cast(h);
        if (w->counter_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ::pthread_cond_destroy (&(w->cond_ ));
            ::pthread_mutex_destroy(&(w->mutex_));
        }
    }

    bool wait(handle_t h) {
        if (h == invalid()) return false;
        auto w = waiter_cast(h);
        if (::pthread_mutex_lock(&(w->mutex_)) != 0) {
            return false;
        }
        auto IPC_UNUSED_ guard = unique_ptr(&(w->mutex_), ::pthread_mutex_unlock);
        if (::pthread_cond_wait(&(w->cond_), &(w->mutex_)) != 0) {
            return false;
        }
        return true;
    }

    void notify(handle_t h) {
        if (h == invalid()) return;
        ::pthread_cond_signal(&(waiter_cast(h)->cond_));
    }

    void broadcast(handle_t h) {
        if (h == invalid()) return;
        ::pthread_cond_broadcast(&(waiter_cast(h)->cond_));
    }
};

} // namespace detail
} // namespace ipc
