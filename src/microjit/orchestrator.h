//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_ORCHESTRATOR_H
#define MICROJIT_ORCHESTRATOR_H

#include "instructions.h"
#include "utils.h"
#include "thread_pool.h"
#include "runtime_agent.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "jit_x86_64.h"
#endif

namespace microjit {
    template<class TCompiler, class TRefCounter = ThreadSafeObject>
    class OrchestratorComponent : public TRefCounter {
    public:
        typedef void(*VirtualStackFunction)(VirtualStack*);

        template<typename R, typename ...Args>
        struct FunctionInstance : public TRefCounter {
        private:
            typedef OrchestratorComponent<TCompiler, TRefCounter> Host;
            class InstanceTrampoline {
            private:
                void (**actual_trampoline)(VirtualStack*);
                void (*recompile_cb)(const void*);
                const void* host;
                friend class FunctionInstance;

                InstanceTrampoline(const void* p_host, void (*p_recompile_cb)(const void*),
                                   void (**p_actual_trampoline)(VirtualStack*))
                        : host(p_host), recompile_cb(p_recompile_cb),
                          actual_trampoline(p_actual_trampoline){}

                template<typename T>
                static _ALWAYS_INLINE_ void move_argument(const T &p_arg, VirtualStack *p_stack){
                    static constexpr bool trivially_copy_constructable = std::is_trivially_copy_constructible_v<T>;
                    auto stack_ptr = p_stack->rsp();
                    *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) - sizeof(T));
                    if constexpr (trivially_copy_constructable){
                        *(T*)(*stack_ptr) = p_arg;
                    } else
                        new (*stack_ptr) T(p_arg);
                }
                template<typename T>
                static _ALWAYS_INLINE_ void destruct_argument(const T &, VirtualStack *p_stack){
                    static constexpr bool trivially_destructible = std::is_trivially_destructible_v<T>;
                    auto stack_ptr = p_stack->rsp();
                    auto ptr = *stack_ptr;
                    if constexpr (!trivially_destructible)
                        ((T*)ptr)->~T();
                    *stack_ptr = (VirtualStack::StackPtr)((size_t)ptr + sizeof(T));
                }

                template<typename T>
                static _ALWAYS_INLINE_ void destruct_return(VirtualStack *p_stack){
                    static constexpr bool trivially_destructible = std::is_trivially_destructible_v<T>;
                    auto stack_ptr = p_stack->rsp();
                    auto ptr = *stack_ptr;
                    if constexpr (!trivially_destructible)
                        ((T*)ptr)->~T();
                    *stack_ptr = (VirtualStack::StackPtr)((size_t)ptr + sizeof(T));
                }

//                static void call_internal(const void*, const InstanceTrampoline* p_self, VirtualStack* p_stack, Args&&... args);

                static R call_internal(const InstanceTrampoline* p_self, VirtualStack* p_stack, Args&&... args);
            public:
                R call_final(VirtualStack* p_stack, Args&&... args) const {
                    static constexpr R* dummy = nullptr;
                    return call_internal(this, p_stack, std::forward<Args>(args)...);
                }
            };
            
        private:
            const OrchestratorComponent* parent;
            const Ref<Function<R, Args...>> function;
            VirtualStack* virtual_stack;
            const Ref<JitFunctionTrampoline> jit_trampoline;
            const InstanceTrampoline instance_trampoline;
            mutable VirtualStackFunction real_compiled_function{};
        private:
            void compile_internal() const;

            _NO_DISCARD_ _ALWAYS_INLINE_ bool is_compiled() const { return real_compiled_function != nullptr; }
            static void static_recompile(const FunctionInstance* p_self);
        public:
            explicit FunctionInstance(const OrchestratorComponent* p_orchestrator);
            ~FunctionInstance() override {
                delete virtual_stack;
            }

            Ref<Function<R, Args...>> get_function() const { return function; }
            R call(Args... args) const;
            void recompile() const;
            void detach();
            _ALWAYS_INLINE_ std::function<R(Args...)> get_compiled_function_compat() const {
                return [this](Args&&... args) -> R {
                    return instance_trampoline.call_final(virtual_stack, std::forward<Args>(args)...);
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
            Ref<FunctionInstance<R, Args...>> unwrap() const { return instance; }

            std::function<R(Args...)> get_compiled_function() const {
                return instance->get_compiled_function_compat();
            }

            _ALWAYS_INLINE_ R call(Args&&... args) const {
                return instance->call(std::forward<Args>(args)...);
            }
            _ALWAYS_INLINE_ R operator()(Args... args) const {
                return call(std::forward<Args>(args)...);
            }
        };
        struct InstanceHub {
        private:
            OrchestratorComponent* parent;
        public:
            explicit InstanceHub(OrchestratorComponent* p_orchestrator) : parent(p_orchestrator) {}
            _NO_DISCARD_ VirtualStackFunction fetch_function(const Ref<RectifiedFunction> &p_func) const;
            _NO_DISCARD_ const CompilationAgentSettings& get_settings() const;
            template<typename R, typename ...Args>
            void detach_instance(const FunctionInstance<R, Args...>* p_instance, const void* p_func) const;
            void register_heat(const Ref<RectifiedFunction>& p_func) const;
        };
    private:
        friend struct InstanceHub;

        const InstanceHub hub;
        CompilationAgentSettings agent_settings;
        Ref<TCompiler> compiler{};
        Ref<MicroJITRuntime> runtime{};
        RuntimeAgent<TCompiler> agent;

        std::unordered_map<size_t, Ref<TRefCounter>> instance_map{};
    private:
        const CompilationAgentSettings& get_settings() const { return agent_settings; }
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
            instance_map[(size_t)(instance.ptr())] = instance.template c_style_cast<TRefCounter>();
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
        void register_heat(const Ref<RectifiedFunction>& p_func){
            agent.register_heat(p_func);
        }
        static constexpr auto default_settings = CompilationAgentSettings{CompilationAgentHandlerType::SINGLE_UNSAFE,
                                                                        6, 1024 * 4, 8};
    public:
        explicit OrchestratorComponent(const CompilationAgentSettings& p_settings)
            : hub(this), agent(p_settings), agent_settings(p_settings) {
            runtime = Ref<MicroJITRuntime>::make_ref();
            compiler = Ref<TCompiler>::make_ref(runtime);
        }
        OrchestratorComponent(): OrchestratorComponent(default_settings) {}
        template<typename R, typename ...Args>
        _ALWAYS_INLINE_ InstanceWrapper<R, Args...> create_instance() {
            return create_instance_internal<R, Args...>();
        }
        template<typename R, typename ...Args>
        _ALWAYS_INLINE_ InstanceWrapper<R, Args...> create_instance_from_model(const std::function<R(Args...)>&) {
            return create_instance_internal<R, Args...>();
        }
    };

    template<class CompilerTy, class RefCounter>
    void OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::register_heat(const Ref<RectifiedFunction> &p_func) const {
        parent->register_heat(p_func);
    }

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
              jit_trampoline(BaseTrampoline::create_jit_trampoline(this, static_recompile, &real_compiled_function)),
              instance_trampoline(this, (void(*)(const void*))static_recompile, &real_compiled_function),
              virtual_stack(new VirtualStack((p_orchestrator)->get_settings().virtual_stack_size, 0)) {
        function->get_trampoline() = jit_trampoline;
    }

    template<class CompilerTy, class RefCounter>
    const CompilationAgentSettings &OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::get_settings() const {
        return parent->get_settings();
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::recompile() const {
        real_compiled_function = nullptr;
        compile_internal();
    }

    template<class TCompiler, class TRefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...>::static_recompile(
            const OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...> *p_self) {
        p_self->compile_internal();
    }

    template<class CompilerTy, class RefCounter>
    template<typename R, typename... Args>
    R OrchestratorComponent<CompilerTy, RefCounter>::FunctionInstance<R, Args...>::call(Args... args) const {
        // return (get_compiled_function_compat())(std::forward<Args>(args)...);
        return instance_trampoline.call_final(virtual_stack, std::forward<Args>(args)...);
    }


    template<class CompilerTy, class RefCounter>
    typename OrchestratorComponent<CompilerTy, RefCounter>::VirtualStackFunction
    OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::fetch_function(const Ref<RectifiedFunction> &p_func) const {
        return parent->fetch_function(p_func);
    }

    template<class TCompiler, class TRefCounter>
    template<typename R, typename... Args>
    R OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...>::InstanceTrampoline::call_internal(
            const InstanceTrampoline *p_self, VirtualStack *p_stack, Args &&... args) {
        p_self->recompile_cb(p_self->host);
        auto** vrsp_ptr = p_stack->rsp();
        auto** vrbp_ptr = p_stack->rbp();
        auto old_rsp = *vrsp_ptr;
        auto old_rbp = *vrbp_ptr;
        (move_argument<Args>(args, p_stack), ...);
        VirtualStack::StackPtr return_slot;
        if constexpr (!std::is_void_v<R>){
            return_slot = (VirtualStack::StackPtr)((size_t)*vrsp_ptr - sizeof(R));
            (*vrsp_ptr) = return_slot;
        } else {
            return_slot = *vrsp_ptr;
        }
        (*vrbp_ptr) = return_slot;
        (*p_self->actual_trampoline)(p_stack);
        if constexpr (!std::is_void_v<R>) {
            auto re = *(R*)return_slot;
            // After copying the return value, destroy its stack entry
            destruct_return<R>(p_stack);
            if (sizeof...(Args)){
                (destruct_argument<Args>(args, p_stack), ...);
            }
            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
            return re;
        } else {
            if (sizeof...(Args)){
                *vrsp_ptr = return_slot;
                (destruct_argument<Args>(args, p_stack), ...);
            }
            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }
    }
    template<class TCompiler, class TRefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...>::compile_internal() const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));
        if (is_compiled()) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(function->rectify());
        real_compiled_function = cb;
    }
}
#if defined(__x86_64__) || defined(_M_X64)
    typedef microjit::OrchestratorComponent<microjit::MicroJITCompiler_x86_64, microjit::ThreadSafeObject> x86_64Orchestrator;
    typedef x86_64Orchestrator MicroJITOrchestrator;
#endif

namespace microjit {
    template<typename ...Args>
    static _ALWAYS_INLINE_ Ref<MicroJITOrchestrator> orchestrator(Args&&...args) { return Ref<MicroJITOrchestrator>::make_ref(args...); }
}
#endif //MICROJIT_ORCHESTRATOR_H
