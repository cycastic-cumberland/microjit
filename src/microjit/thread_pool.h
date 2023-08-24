//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_THREAD_POOL_H
#define MICROJIT_EXPERIMENT_THREAD_POOL_H

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>

#include "managed_thread.h"
#include "priority_queue.h"

namespace microjit {
    template <class T>
    class GroupTaskPromise {
    private:
        const uint8_t promise_count;
        std::future<T>* promises;
        template<class TA>
        static _ALWAYS_INLINE_ void generic_destructor(const uint8_t& p_count, TA* p_array){
            for (size_t i = 0; i < p_count; i++){
                p_array[i].~TA();
            }
        }
    public:
        GroupTaskPromise(const uint8_t& p_count, std::future<T>* p_promises)
                : promise_count(p_count), promises(p_promises) {}
        GroupTaskPromise(const GroupTaskPromise& p_other) : promise_count(p_other.promise_count), promises(p_other.promises) {}
        ~GroupTaskPromise() {
            generic_destructor(promise_count, promises);
            free(promises);
        }
        void wait(){
            for (size_t i = 0; i < promise_count; i++)
                promises[i].wait();
        }
    };

    class ThreadPool {
    public:
        enum Priority : unsigned char {
            SYSTEM = 0,
            HIGH = 1,
            MEDIUM = 2,
            LOW = 3,
        };
    private:
        struct ManagerThread {
        private:
            bool is_terminated{false};
            ManagedThread thread{};
            std::queue<ManagedThread*> disposal_targets{};
            std::mutex mutex{};
            std::condition_variable condition{};
        public:
            ManagerThread(){
                thread.start([this]() -> void {
                    while (true){
                        ManagedThread* disposal_target{};
                        {
                            std::unique_lock<decltype(mutex)> lock(mutex);
                            condition.wait(lock, [this] { return !disposal_targets.empty() || is_terminated; });
                            if (is_terminated) return;
                            disposal_target = disposal_targets.front();
                            disposal_targets.pop();
                        }
                        disposal_target->join();
                        delete disposal_target;
                    }
                });
            }
            void join() {
                static const auto wait_time = std::chrono::microseconds(100);
                condition.notify_all();
                while (!disposal_targets.empty())
                    ManagedThread::sleep(100);
                is_terminated = true;
                condition.notify_all();
                thread.join();
            }
            void queue_for_disposal(ManagedThread* p_thread){
                std::unique_lock<decltype(mutex)> lock(mutex);
                disposal_targets.push(p_thread);
                condition.notify_one();
            }
        };
        const std::function<void()> prologue;
        const std::function<void()> epilogue;
        const uint8_t initial_capacity;
        bool is_cleaning_up{false};
        ManagerThread manager_thread{};
        uint8_t termination_flag{};
        mutable std::mutex pool_conditional_mutex{};
        std::condition_variable pool_conditional_lock{};
        std::unordered_map<ManagedThread::ID, ManagedThread*> threads_map{};
        PriorityQueue<std::function<void()>> task_queue{};

        ManagedThread::ID allocate_worker_internal(){
            if (is_cleaning_up) return 0;
            auto worker = new ManagedThread();
            worker->start([this, worker]() -> void {
                if (prologue) prologue();
                while (true){
                    std::function<void()> func;
                    bool dequeued;
                    {
                        std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
                        pool_conditional_lock.wait(lock, [this] { return !task_queue.empty() || termination_flag > 0; });
                        if (termination_flag > 0) {
                            termination_flag--;
                            threads_map.erase(worker->get_id());
                            manager_thread.queue_for_disposal(worker);
                            if (epilogue) epilogue();
                            return;
                        }
                        dequeued = task_queue.try_pop(func);
                    }
                    if (dequeued) func();
                }
            });
            auto id = worker->get_id();
            threads_map[id] = worker;
            return id;
        }
        void terminate_worker_internal(){
            if (threads_map.size() <= termination_flag) return;
            termination_flag++;
            pool_conditional_lock.notify_one();
        }
        void init() {
            for (int i = 0; i < initial_capacity; ++i) {
                allocate_worker_internal();
            }
        }
        template<typename T>
        _ALWAYS_INLINE_ auto queue_task_internal(Priority p_priority, const std::function<T>& p_func){
            auto task_ptr = std::make_shared<std::packaged_task<T>>(p_func);

            {
                std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
                task_queue.push([task_ptr]() {
                    (*task_ptr)();
                }, p_priority);
            }
            pool_conditional_lock.notify_one();

            // Return future from promise
            return task_ptr->get_future();
        }
        template<typename T>
        _ALWAYS_INLINE_ auto queue_group_task_internal(Priority p_priority, const uint8_t& p_thread_count, const std::function<T(uint8_t, uint8_t)>& p_func){
            uint8_t allocation_thread_count = p_thread_count;
            auto promises = (std::future<T>*)malloc(sizeof(std::future<void>) * allocation_thread_count);
            {
                std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
                for (uint8_t i = 0; i < allocation_thread_count; i++) {
                    auto task_ptr = std::make_shared<std::packaged_task<T(uint8_t, uint8_t)>>(p_func);
                    task_queue.push([task_ptr, i, allocation_thread_count]() -> void {
                        (*task_ptr)(i, allocation_thread_count);
                    }, p_priority);
                    new (&promises[i]) std::future<T>(task_ptr->get_future());
                    pool_conditional_lock.notify_one();
                }
            }
            return GroupTaskPromise(allocation_thread_count, promises);
        }
    public:
        _ALWAYS_INLINE_ ManagedThread::ID allocate_worker(){
            std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
            return allocate_worker_internal();
        }
        _ALWAYS_INLINE_ void batch_allocate_workers(const uint8_t& p_worker_count){
            std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
            for (auto i = 0; i < p_worker_count; i++)
                allocate_worker_internal();
        }
        _ALWAYS_INLINE_ void terminate_worker(){
            std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
            terminate_worker_internal();
        }
        _ALWAYS_INLINE_ void batch_terminate_workers(const uint8_t& p_worker_count){
            std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
            for (auto i = 0; i < p_worker_count; i++)
                terminate_worker_internal();
        }
        _ALWAYS_INLINE_ size_t get_thread_count() const {
            std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
            return threads_map.size();
        }
        _ALWAYS_INLINE_ void terminate_all_workers() {
            {
                std::unique_lock<decltype(pool_conditional_mutex)> lock(pool_conditional_mutex);
                if (is_cleaning_up || threads_map.empty()) return;
                is_cleaning_up = true;
                termination_flag = 255;
                pool_conditional_lock.notify_all();
            }
            while (!threads_map.empty())
                ManagedThread::sleep(100);
            termination_flag = 0;
            is_cleaning_up = false;
        }

        template<typename F, typename...Args>
        auto queue_group_task(Priority p_priority, const uint8_t& p_thread_count, F&& f, Args&&... args){
            using namespace std::placeholders;
            uint8_t u8;
            std::function<decltype(f(u8, u8, args...))(uint8_t, uint8_t)> func =
                    std::bind(std::forward<F>(f), _1, _2, std::forward<Args>(args)...);
            return queue_group_task_internal(p_priority, p_thread_count, func);
        }

        template<typename T, typename F, typename...Args>
        auto queue_group_task_method(Priority p_priority, const uint8_t& p_thread_count, T* p_instance, F&& f, Args&&... args){
            using namespace std::placeholders;
            uint8_t u8;
            std::function<decltype((p_instance->*f)(u8, u8, args...))(uint8_t, uint8_t)> func =
                    std::bind(std::forward<F>(f), p_instance, _1, _2, std::forward<Args>(args)...);
            return queue_group_task_internal(p_priority, p_thread_count, func);
        }

        template<typename T, typename F, typename...Args>
        auto queue_group_task_method(Priority p_priority, const uint8_t& p_thread_count, const T* p_instance, F&& f, Args&&... args){
            using namespace std::placeholders;
            uint8_t u8;
            std::function<decltype((p_instance->*f)(u8, u8, args...))(uint8_t, uint8_t)> func =
                    std::bind(std::forward<F>(f), p_instance, _1, _2, std::forward<Args>(args)...);
            return queue_group_task_internal(p_priority, p_thread_count, func);
        }

        template<typename F, typename...Args>
        auto queue_task(Priority p_priority, F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
            std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            return queue_task_internal(p_priority, func);
        }

        template<typename T, typename F, typename...Args>
        auto queue_task_method(Priority p_priority, T* p_instance, F&& f, Args&& ...args) -> std::future<decltype((p_instance->*f)(args...))> {
            std::function<decltype((p_instance->*f)(args...))()> func = std::bind(std::forward<F>(f), p_instance, std::forward<Args>(args)...);
            return queue_task_internal(p_priority, func);
        }

        template<typename T, typename F, typename...Args>
        auto queue_task_method(Priority p_priority, const T* p_instance, F&& f, Args&& ...args) -> std::future<decltype((p_instance->*f)(args...))> {
            std::function<decltype((p_instance->*f)(args...))()> func = std::bind(std::forward<F>(f), p_instance, std::forward<Args>(args)...);
            return queue_task_internal(p_priority, func);
        }

        explicit ThreadPool(const uint8_t& p_threads = 3, const std::function<void()>& p_prologue = std::function<void()>(),
                            const std::function<void()>& p_epilogue = std::function<void()>())
                : initial_capacity(p_threads), prologue(p_prologue), epilogue(p_epilogue) {
            init();
        }

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool(ThreadPool &&) = delete;
        ~ThreadPool() {
            terminate_all_workers();
            manager_thread.join();
        }

        ThreadPool & operator=(const ThreadPool &) = delete;
        ThreadPool & operator=(ThreadPool &&) = delete;
    };
}


#endif //MICROJIT_EXPERIMENT_THREAD_POOL_H
