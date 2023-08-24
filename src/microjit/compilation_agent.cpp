//
// Created by cycastic on 8/21/23.
//

#include "compilation_agent.h"

_NO_DISCARD_ static _ALWAYS_INLINE_ size_t get_time_us(){
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return static_cast<size_t>(duration.count());
}

microjit::MicroJITCompiler::CompilationResult
microjit::CompilationHandler::compile(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return compiler->compile(p_func);
}


bool microjit::SingleUnsafeCompilationHandler::function_compiled(const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    return (function_map.find((size_t)(p_func->host)) != function_map.end());
}

microjit::CompilationHandler::VirtualStackFunction
microjit::SingleUnsafeCompilationHandler::get_or_create(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    try {
        return function_map.at((size_t)p_func->host);
    } catch (const std::out_of_range&){
        return recompile(p_func);
    }
}

microjit::CompilationHandler::VirtualStackFunction
microjit::SingleUnsafeCompilationHandler::recompile(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto result = compile(p_func);
    if (result.error) return nullptr;
    auto ret = (VirtualStackFunction)result.assembly->callback;
    function_map[(size_t)p_func->host] = ret;
    return ret;
}

bool
microjit::SingleUnsafeCompilationHandler::remove_function(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
#ifdef DEBUG_ENABLED
    if (!function_compiled(p_func)) return false;
#endif
    function_map.erase((size_t)p_func->host);
    return true;
}

bool
microjit::SingleUnsafeCompilationHandler::remove_function(const void* p_host) {
    if (function_map.find((size_t)(p_host)) == function_map.end()) return false;
    auto cb = function_map.at((size_t)p_host);
    runtime->get_asmjit_runtime().release(cb);
    function_map.erase((size_t)p_host);
    return true;
}

bool
microjit::CommandQueueCompilationHandler::function_compiled(const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    return queue.sync_method(this, &CommandQueueCompilationHandler::function_compiled_internal, p_func);
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::get_or_create(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return queue.sync_method(this, &CommandQueueCompilationHandler::get_or_create_internal, p_func);
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::recompile(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return queue.sync_method(this, &CommandQueueCompilationHandler::recompile_internal, p_func);
}

bool microjit::CommandQueueCompilationHandler::function_compiled_internal(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    return (function_map.find((size_t)(p_func->host)) != function_map.end());
}

microjit::CompilationHandler::VirtualStackFunction microjit::CommandQueueCompilationHandler::get_or_create_internal(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    try {
        return function_map.at((size_t)p_func->host);
    } catch (const std::out_of_range&){
        return compile_from_scratch(p_func);
    }
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::recompile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto result = compile(p_func);
    if (result.error) return nullptr;
    auto ret = (VirtualStackFunction)result.assembly->callback;
    function_map[(size_t) p_func->host] = ret;
    return ret;
}

microjit::CompilationHandler::VirtualStackFunction microjit::CommandQueueCompilationHandler::compile_from_scratch(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto host_loc = (size_t)p_func->host;
    auto stub = Box<HandlerStub>::make_box([this, host_loc]() -> void {
        // Will be called inside garbage collector, so no need for locks
        function_map.erase(host_loc);
    });
//    function_cache.push(host_loc, stub);
    return recompile_internal(p_func);
}

bool microjit::CommandQueueCompilationHandler::remove_function_internal(const void* p_host) {
    if (function_map.find((size_t)(p_host)) == function_map.end()) return false;
    auto cb = function_map.at((size_t)p_host);
    runtime->get_asmjit_runtime().release(cb);
    function_map.erase((size_t)p_host);
    return true;
}

bool microjit::CommandQueueCompilationHandler::remove_function(const void* p_host) {
    return queue.sync_method(this, &CommandQueueCompilationHandler::remove_function_internal, p_host);
}

bool
microjit::CommandQueueCompilationHandler::remove_function(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return remove_function(p_func->host);
}

microjit::CommandQueueCompilationHandler::CommandQueueCompilationHandler(const microjit::CompilationAgentSettings &p_settings,
                                                                         const microjit::Ref<microjit::MicroJITCompiler> &p_compiler,
                                                                         const microjit::Ref<microjit::MicroJITRuntime> &p_runtime)
        : /*function_cache(settings.cache_capacity, settings.decay_rate),*/
          CompilationHandler(p_settings, p_compiler, p_runtime) {

}

microjit::CommandQueueCompilationHandler::~CommandQueueCompilationHandler() {
//    is_terminated = true;
//    garbage_collector.join();
}

void microjit::CommandQueueCompilationHandler::register_heat_internal(const void *p_host) {
//    function_cache.at((size_t)p_host)->heat += settings.decay_per_invocation;
}

void microjit::CommandQueueCompilationHandler::register_heat(const void *p_host) {
    queue.dispatch_method(this, &CommandQueueCompilationHandler::register_heat_internal, p_host);
}

bool microjit::ThreadPoolCompilationHandler::function_compiled(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    return function_compiled_internal(p_func);
}

microjit::CompilationHandler::VirtualStackFunction
microjit::ThreadPoolCompilationHandler::get_or_create(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto promise = pool.queue_task_method(ThreadPool::MEDIUM, this, &ThreadPoolCompilationHandler::get_or_create_internal, p_func);
    promise.wait();
    return promise.get();
}

microjit::CompilationHandler::VirtualStackFunction
microjit::ThreadPoolCompilationHandler::recompile(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto promise = pool.queue_task_method(ThreadPool::MEDIUM, this, &ThreadPoolCompilationHandler::recompile_internal, p_func);
    promise.wait();
    return promise.get();
}

bool microjit::ThreadPoolCompilationHandler::remove_function(const void* p_host) {
    return remove_function_internal(p_host);
}

bool microjit::ThreadPoolCompilationHandler::remove_function(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return remove_function(p_func->host);
}

bool microjit::ThreadPoolCompilationHandler::function_compiled_internal(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    ReadLockGuard guard(lock);
    return (function_map.find((size_t)(p_func->host)) != function_map.end());
}

microjit::CompilationHandler::VirtualStackFunction microjit::ThreadPoolCompilationHandler::get_or_create_internal(
        const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    // First try to get the function
    // If available and valid, release lock and return
    // If available but is null, another thread is compiling this function, wait for it
    // If not available, create a placeholder entry and wait for the function to be compiled
    // Inside the try block, the lock must be write-locked
    try {
        VirtualStackFunction re;
        while (true) {
            lock.write_lock();
            re = function_map.at((size_t)p_func->host);
            lock.write_unlock();
            if (re) return re;
            std::this_thread::yield();
        }
    } catch (const std::out_of_range&){
        function_map[(size_t) p_func->host] = nullptr;
        lock.write_unlock();
        return compile_from_scratch(p_func);
    }
}

static thread_local microjit::Ref<microjit::MicroJITCompiler> thread_specific_compiler = microjit::Ref<microjit::MicroJITCompiler>::null();

microjit::CompilationHandler::VirtualStackFunction
microjit::ThreadPoolCompilationHandler::recompile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto result = thread_specific_compiler->compile(p_func);
    if (result.error) return nullptr;
    auto ret = (VirtualStackFunction)result.assembly->callback;
    {
        WriteLockGuard guard(lock);
        function_map[(size_t) p_func->host] = ret;
    }
    return ret;
}

microjit::CompilationHandler::VirtualStackFunction
microjit::ThreadPoolCompilationHandler::compile_from_scratch(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
//    auto host_loc = (size_t)p_func->host;
//    auto stub = Box<HandlerStub>::make_box([this, host_loc]() -> void {
//        // Will be called inside garbage collector, so no need for locks
//        function_map.erase(host_loc);
//    });
//    {
//        WriteLockGuard guard(lock);
//        function_cache.push(host_loc, stub);
//    }
    return recompile_internal(p_func);
}

bool microjit::ThreadPoolCompilationHandler::remove_function_internal(const void* p_host) {
    // The choice to not remove the function_cache entry is deliberate
    // Those entries are going to be handled by the garbage collector anyway
    WriteLockGuard guard(lock);
    if (function_map.find((size_t)(p_host)) == function_map.end()) return false;
    auto cb = function_map.at((size_t)p_host);
    // Need not locking the runtime
    runtime->get_asmjit_runtime().release(cb);
    function_map.erase((size_t)p_host);
    return true;
}

microjit::ThreadPoolCompilationHandler::~ThreadPoolCompilationHandler() {
//    is_terminated = true;
//    garbage_collector.join();
}

// No more checking for compiler every time!
static std::function<void()> construct_compiler(microjit::ThreadPoolCompilationHandler::compiler_spawner p_spawner,
                                                const microjit::Ref<microjit::MicroJITRuntime> &p_runtime){
    auto runtime = p_runtime;
    auto packed = [p_spawner, runtime]() -> void {
        thread_specific_compiler = p_spawner(runtime);
    };
    return packed;
}

microjit::ThreadPoolCompilationHandler::ThreadPoolCompilationHandler(const CompilationAgentSettings& p_settings,
        microjit::ThreadPoolCompilationHandler::compiler_spawner p_spawner,
        const microjit::Ref<microjit::MicroJITRuntime> &p_runtime)
        : CompilationHandler(p_settings, Ref<MicroJITCompiler>::null(), p_runtime),
          spawner(p_spawner), /*function_cache(settings.cache_capacity, settings.decay_rate),*/
          pool(settings.initial_compiler_thread_count, construct_compiler(p_spawner, p_runtime)) {
    runtime = Ref<MicroJITRuntime>::make_ref();
    // This feature is not well thought our right now...
//    garbage_collector.start([this]() -> void {
//        static constexpr auto step = std::chrono::microseconds(100);
//        const auto decay_us = size_t((1.0 / settings.decay_frequency) * 1'000'000.0);
//        const auto cleanup_us = size_t((1.0 / settings.cleanup_frequency) * 1'000'000.0);
//        auto now = get_time_us();
//        auto last_decay = now;
//        auto last_cleanup = now;
//        while (!is_terminated){
//            // This is to prevent either frequency is set too low and block the application exit
//            std::this_thread::sleep_for(std::chrono::microseconds(step));
//            now = get_time_us();
//            {
//                WriteLockGuard guard(lock);
//                if (now - last_decay > decay_us) {
//                    last_decay = now;
//                    function_cache.decay(cleanup_us);
//                }
//                if (now - last_cleanup > cleanup_us) {
//                    last_cleanup = now;
//                    function_cache.cleanup();
//                }
//            }
//        }
//    });
}

void microjit::ThreadPoolCompilationHandler::register_heat_internal(const void *p_host) {
//    ReadLockGuard guard(lock);
//    auto entry = function_cache.at((size_t)p_host);
//    std::lock_guard<std::mutex> inner_guard(entry->lock);
//    entry->heat += settings.decay_per_invocation;
}

void microjit::ThreadPoolCompilationHandler::register_heat(const void *p_host) {
    pool.queue_task_method(ThreadPool::LOW, this, &ThreadPoolCompilationHandler::register_heat_internal, p_host);
}
