//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_COMPILATION_AGENT_H
#define MICROJIT_EXPERIMENT_COMPILATION_AGENT_H

#include "instructions.h"
#include "utils.h"
#include "thread_pool.h"
#include "command_queue.h"
#include "lock.h"
#include "jit.h"

namespace microjit
{
    class CompilationHandler {
    protected:
        Ref<MicroJITCompiler> compiler;
        MicroJITCompiler::CompilationResult compile(const Ref<RectifiedFunction>& p_func);

        explicit CompilationHandler(const Ref<MicroJITCompiler>& p_compiler) : compiler(p_compiler) {}
    public:
        typedef void(*VirtualStackFunction)(VirtualStack*);
        virtual ~CompilationHandler() = default;
        virtual bool function_compiled(const Ref<RectifiedFunction> &p_func) const = 0;
        virtual VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) = 0;
        virtual VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) = 0;
        virtual bool remove_function(const Ref<RectifiedFunction>& p_func) = 0;
        virtual bool remove_function(const void* p_host) = 0;
    };
    class SingleUnsafeCompilationHandler : public CompilationHandler {
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
    public:
        explicit SingleUnsafeCompilationHandler(const Ref<MicroJITCompiler>& p_compiler) : CompilationHandler(p_compiler) {}

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
    };
    class CommandQueueCompilationHandler : public CompilationHandler {
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
        mutable CommandQueue queue{};

    private:
        bool function_compiled_internal(const Ref<RectifiedFunction> &p_func) const;
        VirtualStackFunction get_or_create_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction recompile_internal(const Ref<RectifiedFunction> &p_func);
        bool remove_function_internal(const void* p_host);
    public:
        explicit CommandQueueCompilationHandler(const Ref<MicroJITCompiler>& p_compiler) : CompilationHandler(p_compiler) {}

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
    };
    class ThreadPoolCompilationHandler : public CompilationHandler {
    public:
        typedef Ref<MicroJITCompiler> (*compiler_spawner)(const Ref<MicroJITRuntime>&);
    private:
        Ref<MicroJITRuntime> runtime;
        std::unordered_map<size_t, CompilationHandler::VirtualStackFunction> function_map{};
        const compiler_spawner spawner;
        mutable ThreadPool pool{};
        mutable RWLock lock{};
    private:
        bool function_compiled_internal(const Ref<RectifiedFunction> &p_func) const;
        VirtualStackFunction get_or_create_internal(const Ref<RectifiedFunction> &p_func);
        VirtualStackFunction recompile_internal(const Ref<RectifiedFunction> &p_func);
        bool remove_function_internal(const void* p_host);
    public:
        ThreadPoolCompilationHandler() = delete;
        explicit ThreadPoolCompilationHandler(compiler_spawner p_spawner, const Ref<MicroJITRuntime>& p_runtime)
            : CompilationHandler(Ref<MicroJITCompiler>::null()), spawner(p_spawner), runtime(p_runtime) {
            runtime = Ref<MicroJITRuntime>::make_ref();
        }

        bool function_compiled(const Ref<RectifiedFunction> &p_func) const override;
        VirtualStackFunction get_or_create(const Ref<RectifiedFunction> &p_func) override;
        VirtualStackFunction recompile(const Ref<RectifiedFunction> &p_func) override;
        bool remove_function(const Ref<RectifiedFunction>& p_func) override;
        bool remove_function(const void* p_host) override;
    };
    enum CompilationAgentHandlerType {
        SINGLE_UNSAFE,
        MULTI_QUEUED,
        MULTI_POOLED
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
        explicit CompilationAgent(CompilationAgentHandlerType p_type){
            switch (p_type) {
                case SINGLE_UNSAFE:
                    handler = new SingleUnsafeCompilationHandler(create_compiler(runtime));
                    break;
                case MULTI_QUEUED:
                    handler = new CommandQueueCompilationHandler(create_compiler(runtime));
                    break;
                case MULTI_POOLED:
                    handler = new ThreadPoolCompilationHandler(create_compiler, runtime);
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
    };
}


#endif //MICROJIT_EXPERIMENT_COMPILATION_AGENT_H
