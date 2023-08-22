//
// Created by cycastic on 8/21/23.
//

#include "compilation_agent.h"

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
#ifdef DEBUG_ENABLED
    if (function_map.find((size_t)(p_host)) == function_map.end()) return false;
#endif
    function_map.erase((size_t)p_host);
    return true;
}

bool
microjit::CommandQueueCompilationHandler::function_compiled(const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    auto promise = queue.dispatch_method(this, &CommandQueueCompilationHandler::function_compiled_internal, p_func);
    promise.wait();
    return promise.get();
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::get_or_create(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto promise = queue.dispatch_method(this, &CommandQueueCompilationHandler::get_or_create_internal, p_func);
    promise.wait();
    return promise.get();
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::recompile(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto promise = queue.dispatch_method(this, &CommandQueueCompilationHandler::recompile_internal, p_func);
    promise.wait();
    return promise.get();
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
        return recompile_internal(p_func);
    }
}

microjit::CompilationHandler::VirtualStackFunction
microjit::CommandQueueCompilationHandler::recompile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto result = compile(p_func);
    if (result.error) return nullptr;
    auto ret = (VirtualStackFunction)result.assembly->callback;
    function_map[(size_t)p_func->host] = ret;
    return ret;
}

bool microjit::CommandQueueCompilationHandler::remove_function_internal(const void* p_host) {
#ifdef DEBUG_ENABLED
    if (function_map.find((size_t)(p_host)) == function_map.end()) return false;
#endif
    function_map.erase((size_t)p_host);
    return true;
}

bool microjit::CommandQueueCompilationHandler::remove_function(const void* p_host) {
    auto promise = queue.dispatch_method(this, &CommandQueueCompilationHandler::remove_function_internal, p_host);
    promise.wait();
    return promise.get();
}

bool
microjit::CommandQueueCompilationHandler::remove_function(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    return remove_function(p_func->host);
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
    try {
        ReadLockGuard guard(lock);
        return function_map.at((size_t)p_func->host);
    } catch (const std::out_of_range&){
        return recompile_internal(p_func);
    }
}

static thread_local microjit::Ref<microjit::MicroJITCompiler> thread_specific_compiler = microjit::Ref<microjit::MicroJITCompiler>::null();

microjit::CompilationHandler::VirtualStackFunction
microjit::ThreadPoolCompilationHandler::recompile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    if (thread_specific_compiler.is_null())
        thread_specific_compiler = spawner(runtime);
    auto result = thread_specific_compiler->compile(p_func);
    if (result.error) return nullptr;
    auto ret = (VirtualStackFunction)result.assembly->callback;
    {
        WriteLockGuard guard(lock);
        function_map[(size_t)p_func->host] = ret;
    }
    return ret;
}

bool microjit::ThreadPoolCompilationHandler::remove_function_internal(const void* p_host) {
    WriteLockGuard guard(lock);
    function_map.erase((size_t)p_host);
    return true;
}
