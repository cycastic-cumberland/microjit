//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_COMPILATION_AGENT_H
#define MICROJIT_EXPERIMENT_COMPILATION_AGENT_H

#include "decaying_weighted_cache.h"
#include "instructions.h"
#include "utils.h"
#include "thread_pool.h"
#include "command_queue.h"
#include "lock.h"
#include "jit.h"

namespace microjit
{
    struct HandlerStub {
        const std::function<void()> eviction_handler;
        explicit HandlerStub(const std::function<void()>& p_handler) : eviction_handler(p_handler){}
        ~HandlerStub() { eviction_handler(); }
    };
    enum CompilationAgentHandlerType {
        SINGLE_UNSAFE,
        MULTI_QUEUED,
        MULTI_POOLED
    };
    struct CompilationAgentSettings {
        CompilationAgentHandlerType type;
        size_t cache_capacity;
        double decay_per_invocation;
        double decay_rate;
        double decay_frequency;
        double cleanup_frequency;
        uint8_t initial_compiler_thread_count;
    };
    class CompilationHandler {
    protected:
        Ref<MicroJITCompiler> compiler;
        MicroJITCompiler::CompilationResult compile(const Ref<RectifiedFunction>& p_func);
        Ref<MicroJITRuntime> runtime;
        CompilationAgentSettings settings;
        explicit CompilationHandler(const CompilationAgentSettings& p_settings, const Ref<MicroJITCompiler>& p_compiler, const Ref<MicroJITRuntime>& p_runtime)
            : settings(p_settings), compiler(p_compiler), runtime(p_runtime) {}
    public:
        typedef void(*VirtualStackFunction)(VirtualStack*);
        virtual ~CompilationHandler() = default;
        virtual bool function_compiled(const Ref<RectifiedFunction> &p_func) const = 0;
        virtual VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) = 0;
        virtual VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) = 0;
        virtual bool remove_function(const Ref<RectifiedFunction>& p_func) = 0;
        virtual bool remove_function(const void* p_host) = 0;
        virtual void change_settings(const CompilationAgentSettings& p_new_settings) { settings = p_new_settings; }
        virtual void register_heat(const void* p_host) = 0;
    };
    class SingleUnsafeCompilationHandler : public CompilationHandler {
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
    public:
        SingleUnsafeCompilationHandler(const CompilationAgentSettings& p_settings, const Ref<MicroJITCompiler>& p_compiler, const Ref<MicroJITRuntime>& p_runtime)
            : CompilationHandler(p_settings, p_compiler, p_runtime) {}

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
        void register_heat(const void* p_host) override {  }
    };
    class CommandQueueCompilationHandler : public CompilationHandler {
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
//        DecayingWeightedCache<size_t, Box<HandlerStub>> function_cache;
        ManagedThread garbage_collector{};
        bool is_terminated = false;
        mutable CommandQueue queue{};
    private:
        bool function_compiled_internal(const Ref<RectifiedFunction> &p_func) const;
        VirtualStackFunction get_or_create_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction recompile_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction compile_from_scratch(const Ref<RectifiedFunction> &p_func);
        bool remove_function_internal(const void* p_host);
        void register_heat_internal(const void* p_host);
    public:
        CommandQueueCompilationHandler(const CompilationAgentSettings& p_settings, const Ref<MicroJITCompiler>& p_compiler, const Ref<MicroJITRuntime>& p_runtime);
        ~CommandQueueCompilationHandler() override;

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
        void register_heat(const void* p_host) override;
    };
    class ThreadPoolCompilationHandler : public CompilationHandler {
    public:
        typedef Ref<MicroJITCompiler> (*compiler_spawner)(const Ref<MicroJITRuntime>&);
    private:
        Ref<MicroJITRuntime> runtime;
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
//        DecayingWeightedCache<size_t, Box<HandlerStub>> function_cache;
        ManagedThread garbage_collector{};
        bool is_terminated = false;
        const compiler_spawner spawner;
        mutable ThreadPool pool;
        mutable RWLock lock{};
    private:
        bool function_compiled_internal(const Ref<RectifiedFunction> &p_func) const;
        VirtualStackFunction get_or_create_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction recompile_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction compile_from_scratch(const Ref<RectifiedFunction> &p_func);
        bool remove_function_internal(const void* p_host);
        void register_heat_internal(const void* p_host);
    public:
        ThreadPoolCompilationHandler() = delete;
        explicit ThreadPoolCompilationHandler(const CompilationAgentSettings& p_settings, compiler_spawner p_spawner, const Ref<MicroJITRuntime>& p_runtime);
        ~ThreadPoolCompilationHandler() override;

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
        void register_heat(const void* p_host) override;
    };
    template <class CompilerTy>
    class CompilationAgent {
    public:
    private:
        
        static Ref<MicroJITCompiler> create_compiler(const Ref<MicroJITRuntime>& p_runtime) {
            return Ref<CompilerTy>::make_ref(p_runtime).template c_style_cast<MicroJITCompiler>();
        }
        CompilationHandler* handler{};
        Ref<MicroJITRuntime> runtime{Ref<MicroJITRuntime>::make_ref()};
    public:
        explicit CompilationAgent(const CompilationAgentSettings& p_settings){
            switch (p_settings.type) {
                case SINGLE_UNSAFE:
                    handler = new SingleUnsafeCompilationHandler(p_settings, create_compiler(runtime), runtime);
                    break;
                case MULTI_QUEUED:
                    handler = new CommandQueueCompilationHandler(p_settings, create_compiler(runtime), runtime);
                    break;
                case MULTI_POOLED:
                    handler = new ThreadPoolCompilationHandler(p_settings, create_compiler, runtime);
                    break;
            }
        }
        ~CompilationAgent(){
            delete handler;
        }
        bool function_compiled(const Ref<RectifiedFunction> &p_func) const {
            return handler->function_compiled(p_func);
        }
        CommandQueueCompilationHandler::VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) {
            return handler->get_or_create(p_func);
        }
        CommandQueueCompilationHandler::VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) {
            return handler->recompile(p_func);
        }
        bool remove_function(const Ref<RectifiedFunction>& p_func) {
            return handler->remove_function(p_func);
        }
        bool remove_function(const void* p_host) {
            return handler->remove_function(p_host);
        }
        void register_heat(const void* p_host){
            // TODO: Fill this in
        }
        void register_heat(const Ref<RectifiedFunction>& p_func){
            register_heat(p_func->host);
        }
    };
}


#endif //MICROJIT_EXPERIMENT_COMPILATION_AGENT_H
