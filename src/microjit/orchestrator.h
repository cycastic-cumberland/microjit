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
        typedef void(*VirtualStackFunction)(uint8_t*);

        template<typename R, typename ...Args>
        struct FunctionInstance : public TRefCounter {
        private:
            typedef OrchestratorComponent<TCompiler, TRefCounter> Host;
            class InstanceTrampoline {
            private:
                void (**actual_trampoline)(uint8_t*);
                void (*recompile_cb)(const void*);
                const void* host;
                friend class FunctionInstance;

                InstanceTrampoline(const void* p_host, void (*p_recompile_cb)(const void*),
                                   void (**p_actual_trampoline)(uint8_t*))
                        : host(p_host), recompile_cb(p_recompile_cb),
                          actual_trampoline(p_actual_trampoline){}

                template<typename T>
                static _ALWAYS_INLINE_ void move_argument(const T &p_arg, uint8_t **p_stack){
                    constexpr bool trivially_copy_constructable = std::is_trivially_copy_constructible_v<T>;
                    auto new_addr = (uint8_t*)((size_t)(*p_stack) - sizeof(T));
                    *p_stack = new_addr;
                    if constexpr (trivially_copy_constructable){
                        *(T*)(new_addr) = p_arg;
                    } else
                        new (new_addr) T(p_arg);
                }
                template<typename T>
                static _ALWAYS_INLINE_ void destruct_argument(uint8_t **p_stack){
                    constexpr bool trivially_destructible = std::is_trivially_destructible_v<T>;
                    auto ptr = *p_stack;
                    if constexpr (!trivially_destructible)
                        ((T*)ptr)->~T();
                    *p_stack = (uint8_t*)((size_t)ptr + sizeof(T));
                }

                template<typename T>
                static _ALWAYS_INLINE_ void destruct_return(uint8_t **p_stack){
                    destruct_argument<T>(p_stack);
                }
                static R call_internal(const InstanceTrampoline* p_self, Args&&... args);
            public:
                R call_final(Args&&... args) const {
                    static constexpr R* dummy = nullptr;
                    return call_internal(this, std::forward<Args>(args)...);
                }
            };
            
        private:
            const OrchestratorComponent* parent;
            const Ref<Function<R, Args...>> function;
            Ref<RectifiedFunction> rectified_function{};
            const Ref<JitFunctionTrampoline> jit_trampoline;
            const InstanceTrampoline instance_trampoline;
            mutable VirtualStackFunction real_compiled_function{};
        private:
            void compile_internal() const;

            _NO_DISCARD_ _ALWAYS_INLINE_ bool is_compiled() const { return real_compiled_function != nullptr; }
            static void static_recompile(const FunctionInstance* p_self);
        public:
            explicit FunctionInstance(const OrchestratorComponent* p_orchestrator);

            Ref<Function<R, Args...>> get_function() const { return function; }
            R call(Args... args) const;
            void recompile() const;
            void detach();
            _ALWAYS_INLINE_ std::function<R(Args...)> get_compiled_function_compat() const {
                return [this](Args&&... args) -> R {
                    return instance_trampoline.call_final(std::forward<Args>(args)...);
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
              instance_trampoline(this, (void(*)(const void*))static_recompile, &real_compiled_function) {
        rectified_function = function->rectify();
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
        return instance_trampoline.call_final(std::forward<Args>(args)...);
    }


    template<class CompilerTy, class RefCounter>
    typename OrchestratorComponent<CompilerTy, RefCounter>::VirtualStackFunction
    OrchestratorComponent<CompilerTy, RefCounter>::InstanceHub::fetch_function(const Ref<RectifiedFunction> &p_func) const {
        return parent->fetch_function(p_func);
    }

    template<class TCompiler, class TRefCounter>
    template<typename R, typename... Args>
    R OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...>::InstanceTrampoline::call_internal(
            const InstanceTrampoline *p_self, Args &&... args) {
        p_self->recompile_cb(p_self->host);
        constexpr auto args_space_size = calculate_args_space<R, Args...>();
        uint8_t args_space[args_space_size];
        auto space_ptr = (uint8_t*)args_space;
        space_ptr = (decltype(space_ptr))((size_t)space_ptr + args_space_size);
        (move_argument<Args>(args, &space_ptr), ...);
        space_ptr = (uint8_t*)args_space;
        (*p_self->actual_trampoline)(space_ptr);
        if constexpr (!std::is_void_v<R>) {
            auto re = *((R*)space_ptr);
            // After copying the return value, destroy its stack entry
            destruct_return<R>(&space_ptr);
            if constexpr (sizeof...(Args)){
                (destruct_argument<Args>(&space_ptr), ...);
            }
            return re;
        } else {
            if constexpr (sizeof...(Args)){
                (destruct_argument<Args>(&space_ptr), ...);
            }
        }
    }
    template<class TCompiler, class TRefCounter>
    template<typename R, typename... Args>
    void OrchestratorComponent<TCompiler, TRefCounter>::FunctionInstance<R, Args...>::compile_internal() const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));
        if (is_compiled()) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(rectified_function);
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
