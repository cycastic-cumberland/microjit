//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_ORCHESTRATOR_H
#define MICROJIT_ORCHESTRATOR_H

#include <stdexcept>
#include "instructions.h"
#include "utils.h"
#include "thread_pool.h"
#include "compilation_agent.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "jit_x86_64.h"
#endif

namespace microjit {
    struct VirtualStackSettings {
        size_t vstack_default_size = 1024 * 1024 * 8;
        size_t vstack_buffer_size = 128;
        uint8_t starting_pool_size = 4;
    };
    template<class CompilerTy, class RefCounter = ThreadSafeObject>
    class OrchestratorComponent : public RefCounter {
    public:
        typedef void(*VirtualStackFunction)(VirtualStack*);

        template<typename R, typename ...Args>
        struct FunctionInstance : public RefCounter {
        private:
            typedef OrchestratorComponent<CompilerTy, RefCounter> Host;
        private:
            const OrchestratorComponent* parent;
            const Ref<Function<R, Args...>> function;
            mutable std::function<R(VirtualStack*, Args...)> compiled_function{};
            mutable VirtualStackFunction real_compiled_function{};
            const VirtualStackSettings& settings;
        private:
            template<typename R2, typename ...Args2>
            void compile_internal(std::function<R2(VirtualStack*, Args2...)>* p_func) const;
            template<typename ...Args2>
            void compile_internal(std::function<void(VirtualStack*, Args2...)>* p_func) const;
            template<typename R2, typename ...Args2>
            void compile_internal(std::function<R2(VirtualStack*, Args2...)>* p_func, VirtualStackFunction p_cb) const;
            template<typename ...Args2>
            void compile_internal(std::function<void(VirtualStack*, Args2...)>* p_func, VirtualStackFunction p_cb) const;
        public:
            explicit FunctionInstance(const OrchestratorComponent* p_orchestrator);
            ~FunctionInstance() override = default;

            Ref<Function<R, Args...>> get_function() const { return function; }
            R call(Args&&... args) const;
            R call_with_vstack(VirtualStack* p_stack, Args&&... args) const;
            void recompile() const;
            void detach();
            _ALWAYS_INLINE_ std::function<R(VirtualStack*, Args...)> get_full_compiled_function() const {
                compile_internal(&compiled_function);
                return compiled_function;
            }
            _ALWAYS_INLINE_ std::function<R(Args...)> get_compiled_function() const {
                auto f = get_full_compiled_function();
                auto size = settings.vstack_default_size;
                auto buffer = settings.vstack_buffer_size;
                return [f, size, buffer](Args&&... args) -> R {
                    auto stack = Box<VirtualStack>::make_box(size, buffer);
                    return f(stack.ptr(), std::forward<Args>(args)...);
                };
            }
        };
        template<typename R, typename ...Args>
        struct InstanceWrapper {
        private:
            Ref<FunctionInstance<R, Args...>> instance;
        public:
            explicit InstanceWrapper(const Ref<FunctionInstance<R, Args...>>& p_instance) : instance(p_instance) {}
            _NO_DISCARD_ FunctionInstance<R, Args...>* ptr() { return instance.ptr(); }
            _NO_DISCARD_ const FunctionInstance<R, Args...>* ptr() const { return instance.ptr(); }
            _NO_DISCARD_ FunctionInstance<R, Args...>* operator->() { return ptr(); }
            _NO_DISCARD_ const FunctionInstance<R, Args...>* operator->() const { return ptr(); }
            _NO_DISCARD_ FunctionInstance<R, Args...>* operator*() { return ptr(); }
            _NO_DISCARD_ const FunctionInstance<R, Args...>* operator*() const { return ptr(); }
            void recompile() const { instance->recompile(); }
            void detach() { instance->detach(); }
            std::function<R(VirtualStack*, Args...)> get_full_compiled_function() const {
                return instance->get_full_compiled_function();
            }
            std::function<R(Args...)> get_compiled_function() const {
                return instance->get_compiled_function();
            }

            _ALWAYS_INLINE_ R call(Args&&... args) const {
                return instance->call(std::forward<Args>(args)...);
            }
            _ALWAYS_INLINE_ R call_with_vstack(VirtualStack* p_stack, Args&&... args) const {
                return instance->call_with_vstack(p_stack, std::forward<Args>(args)...);
            }
            _ALWAYS_INLINE_ R operator()(Args&&... args) const {
                return call(std::forward<Args>(args)...);
            }
        };
        struct InstanceHub {
        private:
            OrchestratorComponent* parent;
        public:
            explicit InstanceHub(OrchestratorComponent* p_orchestrator) : parent(p_orchestrator) {}
            _NO_DISCARD_ VirtualStackFunction fetch_function(const Ref<RectifiedFunction> &p_func) const;
            _NO_DISCARD_ const VirtualStackSettings& get_settings() const;
            template<typename R, typename ...Args>
            void detach_instance(const FunctionInstance<R, Args...>* p_instance, const void* p_func) const;
        };
    private:
        friend struct InstanceHub;

        const InstanceHub hub;
        VirtualStackSettings settings;
        Ref<CompilerTy> compiler{};
        Ref<MicroJITRuntime> runtime{};
        CompilationAgent<CompilerTy> agent;

        std::unordered_map<size_t, Ref<RefCounter>> instance_map{};
    private:
        const VirtualStackSettings& get_settings() const { return settings; }
        MicroJITCompiler::CompilationResult compile(const Ref<RectifiedFunction>& p_func) {
            return compiler->compile(p_func);
        }
        bool has_function(const Ref<RectifiedFunction> &p_func) const {
            return agent.function_compiled(p_func);
        }
        VirtualStackFunction fetch_function(const Ref<RectifiedFunction> &p_func) {
            return agent.get_or_create(p_func);
        }
        template<typename R, typename ...Args>
        _ALWAYS_INLINE_ InstanceWrapper<R, Args...> create_instance_internal() {
            auto instance = Ref<FunctionInstance<R, Args...>>::make_ref(this);
            instance_map[(size_t)(instance.ptr())] = instance.template c_style_cast<RefCounter>();
            return InstanceWrapper<R, Args...>(instance);
        }
        void rectified_detach_instance(const void* p_instance, const void* p_func){
            instance_map.erase((size_t)p_instance);
            agent.remove_function(p_func);
        }
        template<typename R, typename ...Args>
        void detach_instance(const FunctionInstance<R, Args...>* p_instance, const void* p_func){
            rectified_detach_instance(p_instance, p_func);
        }
    public:
        explicit OrchestratorComponent(CompilationAgentHandlerType p_type = CompilationAgentHandlerType::SINGLE_UNSAFE)
            : hub(this), agent(p_type) {
            runtime = Ref<MicroJITRuntime>::make_ref();
            compiler = Ref<CompilerTy>::make_ref(runtime);
        }
        template<typename R, typename ...Args>
        _ALWAYS_INLINE_ InstanceWrapper<R, Args...> create_instance() {
            return create_instance_internal<R, Args...>();
        }
        template<typename R, typename ...Args>
        _ALWAYS_INLINE_ InstanceWrapper<R, Args...> create_instance_from_model(const std::function<R(Args...)>&) {
            return create_instance_internal<R, Args...>();
        }
        // _NO_DISCARD_ VirtualStackSettings& edit_vstack_settings() { return settings; }
    };

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::detach() {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        instance_hub.detach_instance(this, function.ptr());
        parent = nullptr;
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::detach_instance(
            const OrchestratorComponent::FunctionInstance<R, Args...> *p_instance, const void *p_func) const {
        parent->detach_instance(p_instance, p_func);
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::FunctionInstance(
            const OrchestratorComponent *p_orchestrator)
            : parent(p_orchestrator), function{Ref<Function<R, Args...>>::make_ref()},
              settings(const_cast<OrchestratorComponent*>(p_orchestrator)->get_settings()) {
        function->get_trampoline() = [this](VirtualStack* p_stack) -> void {
            compile_internal(&compiled_function);
            real_compiled_function(p_stack);
        };
    }

    template<class CompilerTy, class RefCounter>
    const VirtualStackSettings &OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::get_settings() const {
        return parent->get_settings();
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::recompile() const {
        std::function<R(VirtualStack*, Args...)> dummy{};
        compiled_function.swap(dummy);
        compile_internal(&compiled_function);
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    R OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::call_with_vstack(VirtualStack *p_stack,
                                                                                        Args &&... args) const {
        return (get_full_compiled_function())(p_stack, std::forward<Args>(args)...);
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    R OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::call(Args &&... args) const {
        return (get_compiled_function())(std::forward<Args>(args)...);
    }

    template<typename T>
    static _ALWAYS_INLINE_ void move_argument(const T &p_arg, VirtualStack *p_stack){
        auto stack_ptr = p_stack->rsp();
        *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) - sizeof(T));
        new (*stack_ptr) T(p_arg);
    }
    template<typename T>
    static _ALWAYS_INLINE_ void destruct_argument(const T &, VirtualStack *p_stack){
        static const bool trivially_destructed = std::is_trivially_destructible<T>::value;
        auto stack_ptr = p_stack->rsp();
        if (!trivially_destructed)
            ((T*)*stack_ptr)->~T();
        *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) + sizeof(T));
    }

    template<typename T>
    static _ALWAYS_INLINE_ void destruct_return(VirtualStack *p_stack){
        static const bool trivially_destructed = std::is_trivially_destructible<T>::value;
        auto stack_ptr = p_stack->rsp();
        if (!trivially_destructed)
            ((T*)*stack_ptr)->~T();
        *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) + sizeof(T));
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    template<typename... Args2>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::compile_internal(
            std::function<void(VirtualStack *, Args2...)> *p_func,
            OrchestratorComponent::VirtualStackFunction p_cb) const {
        std::function<void(VirtualStack*, Args2...)> new_func = [p_cb](VirtualStack* p_stack, Args2&& ...args) -> void {
            auto old_rbp = *p_stack->rbp();
            auto old_rsp = *p_stack->rsp();
            (move_argument<Args2>(args, p_stack), ...);
            auto args_end = *p_stack->rsp();
            *p_stack->rbp() = *p_stack->rsp();
            p_cb(p_stack);
            if (sizeof...(Args)){
                *p_stack->rsp() = args_end;
                (destruct_argument<Args2>(args, p_stack), ...);
            }
            *p_stack->rbp() = old_rbp;
            *p_stack->rsp() = old_rsp;
        };
        p_func->swap(new_func);
        real_compiled_function = p_cb;
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    template<typename R2, typename... Args2>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::compile_internal(
            std::function<R2(VirtualStack *, Args2...)> *p_func,
            OrchestratorComponent::VirtualStackFunction p_cb) const {
        std::function<R2(VirtualStack*, Args2...)> new_func = [p_cb](VirtualStack* p_stack, Args2&& ...args) -> R2 {
            auto old_rbp = *p_stack->rbp();
            auto old_rsp = *p_stack->rsp();
            (move_argument<Args2>(args, p_stack), ...);
            const auto return_slot = (VirtualStack::StackPtr)((size_t)*p_stack->rsp() - sizeof(R2));
            (*p_stack->rbp()) = return_slot;
            (*p_stack->rsp()) = return_slot;
            p_cb(p_stack);
            auto re = *(R2*)return_slot;
            // After copying the return value, destroy its stack entry
            destruct_return<R2>(p_stack);
            if (sizeof...(Args)){
                (destruct_argument<Args2>(args, p_stack), ...);
            }
            *p_stack->rbp() = old_rbp;
            *p_stack->rsp() = old_rsp;
            return re;
        };
        p_func->swap(new_func);
        real_compiled_function = p_cb;
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    template<typename R2, typename... Args2>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::compile_internal(
            std::function<R2(VirtualStack *, Args2...)> *p_func) const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));

        if (p_func->operator bool()) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(function->rectify());
        compile_internal(p_func, cb);
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    template<typename... Args2>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::compile_internal(
            std::function<void(VirtualStack *, Args2...)> *p_func) const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));

        if (p_func->operator bool()) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(function->rectify());
        compile_internal(p_func, cb);
    }

    template<class CompilerTy, class RefCounter>
    typename OrchestratorComponent<CompilerTy, RefCounter>::VirtualStackFunction
    OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::fetch_function(const Ref<RectifiedFunction> &p_func) const {
        return parent->fetch_function(p_func);
    }

}
#if defined(__x86_64__) || defined(_M_X64)
    typedef microjit::OrchestratorComponent<microjit::MicroJITCompiler_x86_64, microjit::ThreadSafeObject> x86_64Orchestrator;
    typedef x86_64Orchestrator MicroJITOrchestrator;
#endif

namespace microjit {
    static _ALWAYS_INLINE_ Ref<MicroJITOrchestrator> orchestrator() { return Ref<MicroJITOrchestrator>::make_ref(); }
}
#endif //MICROJIT_ORCHESTRATOR_H
