//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_COMMAND_QUEUE_H
#define MICROJIT_EXPERIMENT_COMMAND_QUEUE_H

#include <queue>
#include <iostream>
#include "managed_thread.h"

namespace microjit {
    class CommandQueue {
    private:
        bool is_terminated;
        std::mutex conditional_mutex{};
        std::condition_variable conditional_lock{};
        std::queue<std::function<void()>> task_queue{};
        ManagedThread server;

        template<class T>
        _ALWAYS_INLINE_ auto dispatch_internal(const std::function<T>& p_func){
            auto task_ptr = std::make_shared<std::packaged_task<T>>(p_func);
            {
                std::unique_lock<decltype(conditional_mutex)> lock(conditional_mutex);
                task_queue.push([task_ptr]() {
                    (*task_ptr)();
                });
            }
            conditional_lock.notify_one();
            return task_ptr->get_future();
        }
    public:
        CommandQueue() : server(), is_terminated(false) {
            server.start([this]() {
                while (true){
                    std::function<void()> func;
                    {
                        std::unique_lock<decltype(conditional_mutex)> lock(conditional_mutex);
                        conditional_lock.wait(lock, [this] { return !task_queue.empty() || is_terminated; });
                        if (is_terminated) return;
                        if (task_queue.empty()) continue;
                        func = task_queue.front();
                        task_queue.pop();
                    }
                    try {
                        func();
                    } catch (const std::exception& e){
                        std::cerr << "Exception thrown in command queue "
                                  << std::this_thread::get_id() << ": " << e.what() << "\n";
                    }
                }
            });
        }
        ~CommandQueue() {
            is_terminated = true;
            conditional_lock.notify_all();
            server.join();
        }
        _ALWAYS_INLINE_ ManagedThread::ID get_server_id() { return server.get_id(); }

        template<typename F, typename...Args>
        auto dispatch(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
            std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            return dispatch_internal(func);
        }
        template<typename T, typename F, typename...Args>
        auto dispatch_method(T* p_instance, F&& f, Args&&... args) -> std::future<decltype((p_instance->*f)(args...))> {
            std::function<decltype((p_instance->*f)(args...))()> func = std::bind(std::forward<F>(f), p_instance, std::forward<Args>(args)...);
            return dispatch_internal(func);
        }
        template<typename T, typename F, typename...Args>
        auto dispatch_method(const T* p_instance, F&& f, Args&&... args) -> std::future<decltype((p_instance->*f)(args...))> {
            std::function<decltype((p_instance->*f)(args...))()> func = std::bind(std::forward<F>(f), p_instance, std::forward<Args>(args)...);
            return dispatch_internal(func);
        }
    };
}

#endif //MICROJIT_EXPERIMENT_COMMAND_QUEUE_H
