//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_MANAGED_THREAD_H
#define MICROJIT_EXPERIMENT_MANAGED_THREAD_H

#include <thread>
#include <memory>
#include <future>
#include <functional>
#include "def.h"

class ManagedThread {
public:
    typedef uint64_t ID;
    static uint64_t thread_id_hash(const std::thread::id& p_native_id);
private:

    static ID main_thread_id;

    std::thread thread{};
    ID id = thread_id_hash(std::thread::id());
    template<class T>
    _ALWAYS_INLINE_ void start_internal(const std::function<T>& p_func){
        // Current ID is not this thread's ID
        if (id != thread_id_hash(std::thread::id())){
            thread.detach();
            std::thread empty_thread;
            thread.swap(empty_thread);
        }
        std::thread new_thread(p_func);
        thread.swap(new_thread);
        id = thread_id_hash(thread.get_id());
    }
public:
    ManagedThread() = default;
    ~ManagedThread();

    template<typename Callable, typename...Args>
    _ALWAYS_INLINE_ void start(Callable&& f, Args&&... args){
        std::function<decltype(f(args...))()> func =
                std::bind(std::forward<Callable>(f), std::forward<Args>(args)...);
        start_internal(func);
    }
    template<typename T, typename Callable, typename...Args>
    _ALWAYS_INLINE_ void start_method(T* p_instance, Callable&& f, Args&&... args) {
        std::function<decltype((p_instance->*f)(args...))()> func =
                std::bind(std::forward<Callable>(f), p_instance, std::forward<Args>(args)...);
        start_internal(func);
    }
    template<typename T, typename Callable, typename...Args>
    _ALWAYS_INLINE_ void start_method(const T* p_instance, Callable&& f, Args&&... args) {
        std::function<decltype((p_instance->*f)(args...))()> func =
                std::bind(std::forward<Callable>(f), p_instance, std::forward<Args>(args)...);
        start_internal(func);
    }

    _NO_DISCARD_ _ALWAYS_INLINE_ ID get_id() const { return id; }
    _NO_DISCARD_ bool is_started() const;
    _NO_DISCARD_ bool is_alive() const;
    _NO_DISCARD_ bool is_finished() const;
    void join();

    static ID this_thread_id();
    static _ALWAYS_INLINE_ void yield() { std::this_thread::yield(); }
    static _ALWAYS_INLINE_ void sleep(const size_t& p_microseconds) { std::this_thread::sleep_for(std::chrono::microseconds(p_microseconds)); }
};

#endif //MICROJIT_EXPERIMENT_MANAGED_THREAD_H
